/*
 * Model of Xilinx ZynqMP PUF Operation Services
 *
 * Copyright (c) 2020 Xilinx Inc
 *
 * See:
 *   UG1085, v2.1, p.269, PUF Operations
 *   UG1085, v2.1, p.277, PUF Operation permissions in eFUSE
 *   UG1085, v2.1, p.282, PUF Helper-data locations in eFUSE
 *   https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_eps_zynqmp_hw.h#L1111
 *
 * The model is to enable QEMU to support XilSKey ZynqMP PUF software
 * (xilskey_puf_registration.c and xilskey_puf_regeneration.c).
 *
 * However, the fictitious helper-data from the registration model, by design,
 * are very much "clonable", so that they can be readily sharable by different
 * QEMU invocatons by different users on different host systems.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"

#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"

#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#include "hw/misc/xlnx-zynqmp-pufhd.h"

#ifndef ZYNQMP_PUFOP_ERR_DEBUG
#define ZYNQMP_PUFOP_ERR_DEBUG 0
#endif

#define TYPE_ZYNQMP_PUFOP "xlnx,zynqmp-pufop"

#define ZYNQMP_PUFOP(obj) \
     OBJECT_CHECK(Zynqmp_PUFOP, (obj), TYPE_ZYNQMP_PUFOP)

REG32(PUF_CMD, 0x00)
    FIELD(PUF_CMD, CMD, 0, 4)
REG32(PUF_CFG0, 0x04)
REG32(PUF_CFG1, 0x08)
REG32(PUF_SHUT, 0x0c)
    FIELD(PUF_SHUT, SOSET, 24, 8)
    FIELD(PUF_SHUT, SOPEN, 0, 24)
REG32(PUF_STATUS, 0x10)
    FIELD(PUF_STATUS, OVERFLOW, 28, 2)
    FIELD(PUF_STATUS, AUX, 4, 24)
    FIELD(PUF_STATUS, KEY_RDY, 3, 1)
    FIELD(PUF_STATUS, KEY_ZERO, 1, 1)
    FIELD(PUF_STATUS, SYN_WRD_RDY, 0, 1)
REG32(PUF_WORD, 0x18)

#define R_MAX  (R_PUF_WORD + 1)

/*
 * The starting row of PUF Helper-data in eFUSE, for calling
 * efuse_get_row().
 *
 * Per UG-1085 (v2.1, Aug 21, 2019, p.282), it is the 1st row
 * of page 2. Also, each page has 64 rows.
 *
 * In csu_fuse.c::zynqmp_efuse_rd_addr_postw(), the model
 * translates 'page 2' into 'page 1'.
 */
#define ZYNQMP_PUFHD_EFUSE_BASE_ROW         (64)

/*
 * Also, must enforce PUF operation policies specified by
 * the following bits from eFUSE (see same UG-1085, p.277):
 */
#define ZYNQMP_EFUSE_PUF_SYN_INVALID        (21 * 32 + 29)
#define ZYNQMP_EFUSE_PUF_REGISTER_DISABLE   (21 * 32 + 31)

/*
 * Model object.
 */
typedef struct Zynqmp_PUFOP {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    ZynqMPAESKeySink *puf_keysink;
    XLNXEFuse *efuse;

    DeviceState *puf_acc_err_sink;
    qemu_irq err_out;

    Zynqmp_PUFHD *pufhd;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} Zynqmp_PUFOP;

static void zynqmp_pufop_regis_start(Zynqmp_PUFOP *s)
{
    /* Enforce registration policy as stated in eFUSE */
    if (efuse_get_bit(s->efuse, ZYNQMP_EFUSE_PUF_REGISTER_DISABLE)) {
        qemu_log("warning: PUF-REGISTRATION: eFUSE PUF_REGISTER_DISABLE: 1\n");
        goto inval_request;
    }

    /* Check request parameters. */
    if (s->regs[R_PUF_CFG0] != PUF_CFG0_VALUE) {
        qemu_log("warning: PUF-REGISTRATION: Unsupported CFG0 %#02x\n",
                 s->regs[R_PUF_CFG0]);
        goto inval_request;
    }

    if (s->regs[R_PUF_CFG1] != PUF_CFG1_4K_MODE) {
        qemu_log("warning: PUF-REGISTRATION: Unsupported CFG1 %#02x\n",
                 s->regs[R_PUF_CFG1]);
        goto inval_request;
    }

    g_free(s->pufhd);
    s->pufhd = zynqmp_pufhd_new(s->puf_keysink);
    if (s->pufhd == NULL) {
        goto inval_request;
    }

    /*
     * Indicate readiness of the 1st helper-data word. However,
     * the actual 1st word should only be placed into PUF_WORD
     * when PUF_WORD register is read.
     */
    s->regs[R_PUF_STATUS] = PUF_STATUS_WRD_RDY;
    return;

 inval_request:
    qemu_irq_pulse(s->err_out);
}

static void zynqmp_pufop_regen_start(Zynqmp_PUFOP *s)
{
    Zynqmp_PUFRegen hd_src;

    /*
     * Check to make sure PUF helper-data in eFUSE has not been
     * marked as invalidated.
     *
     * As expected by XilSKey, regen PUF-op service always use
     * PUF helper-data from eFUSE.
     */
    if (efuse_get_bit(s->efuse, ZYNQMP_EFUSE_PUF_SYN_INVALID)) {
        qemu_log("warning: PUF-REGENERATION: eFUSE PUF_SYN_INVALID: 1\n");
        goto err_out;
    }

    /* Check request parameters. */
    if (s->regs[R_PUF_CFG0] != PUF_CFG0_VALUE) {
        qemu_log("warning: PUF-REGENERATION: Unsupported CFG0 %#02x\n",
                 s->regs[R_PUF_CFG0]);
        goto err_out;
    }

    memset(&hd_src, 0, sizeof(hd_src));
    hd_src.source = Zynqmp_PUFRegen_EFUSE;
    hd_src.efuse.dev = s->efuse;
    hd_src.efuse.base_row = ZYNQMP_PUFHD_EFUSE_BASE_ROW;

    if (!zynqmp_pufhd_regen(&hd_src, s->puf_keysink, NULL)) {
        goto err_out;
    }

    return;

 err_out:
    qemu_irq_pulse(s->err_out);
}

static void zynqmp_pufop_reset(Zynqmp_PUFOP *s)
{
    uint8_t zero[256 / 8];

    memset(zero, 0, sizeof(zero));

    if (s->puf_keysink) {
        zynqmp_aes_key_update(s->puf_keysink, zero, sizeof(zero));
    }
}

static void zynqmp_pufop_cmd_post_write(RegisterInfo *reg, uint64_t val64)
{
    Zynqmp_PUFOP *s = ZYNQMP_PUFOP(reg->opaque);

    s->regs[R_PUF_CMD] = (uint32_t)val64;

    switch (s->regs[R_PUF_CMD]) {
    case PUF_CMD_REGISTRATION:
        zynqmp_pufop_regis_start(s);
        break;
    case PUF_CMD_REGENERATION:
        zynqmp_pufop_regen_start(s);
        break;
    case PUF_CMD_DEBUG_2:
        s->regs[R_PUF_STATUS] = PUF_STATUS_WRD_RDY;
        break;
    case PUF_CMD_RESET:
        zynqmp_pufop_reset(s);
        break;
    default:
        qemu_log("warning: Unsupported PUF-service request %#02x\n",
                 s->regs[R_PUF_CMD]);
        break;
    }
}

static void zynqmp_pufop_dbg2_next(Zynqmp_PUFOP *s)
{
    const uint32_t fake_data = 0xdbc0ffee;

    s->regs[R_PUF_WORD] = fake_data;
    s->regs[R_PUF_STATUS] = PUF_STATUS_WRD_RDY;
}

static uint64_t zynqmp_pufop_word_post_read(RegisterInfo *reg, uint64_t val)
{
    Zynqmp_PUFOP *s = ZYNQMP_PUFOP(reg->opaque);

    switch (s->regs[R_PUF_CMD]) {
    case PUF_CMD_REGISTRATION:
        zynqmp_pufhd_next(s->pufhd,
                          &s->regs[R_PUF_WORD], &s->regs[R_PUF_STATUS]);
        break;
    case PUF_CMD_DEBUG_2:
        zynqmp_pufop_dbg2_next(s);
        break;
    case PUF_CMD_REGENERATION:
        break;  /* PUF_WORD not used for regen */
    default:
        qemu_log("warning: Unsupported PUF-service request %#02x\n",
                 s->regs[R_PUF_CMD]);
        break;
    }

    val = s->regs[R_PUF_WORD];
    return val;
}

static const RegisterAccessInfo zynqmp_pufop_regs_info[] = {
    {   .name = "PUF_CMD",  .addr = A_PUF_CMD,
        .post_write = zynqmp_pufop_cmd_post_write,
    },{ .name = "PUF_CFG0",  .addr = A_PUF_CFG0,
        .reset = 0x2,
    },{ .name = "PUF_CFG1",  .addr = A_PUF_CFG1,
        .reset = 0x80080,
    },{ .name = "PUF_SHUT",  .addr = A_PUF_SHUT,
        .reset = 0x1000020,
    },{ .name = "PUF_STATUS",  .addr = A_PUF_STATUS,
        .ro = 0xffffffff,
    },{ .name = "PUF_WORD",  .addr = A_PUF_WORD,
        .ro = 0xffffffff,
        .post_read = zynqmp_pufop_word_post_read,
    },
};

static const MemoryRegionOps zynqmp_pufop_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void zynqmp_pufop_hook_err_out(DeviceState *dev)
{
    Zynqmp_PUFOP *s = ZYNQMP_PUFOP(dev);

    static const char gpio_name[] = "puf-acc-err";
    int gpio_indx = 0;

    if (!s->puf_acc_err_sink) {
        return;
    }

    qdev_init_gpio_out_named(dev, &s->err_out, gpio_name, 1);
    qdev_connect_gpio_out_named(dev, gpio_name, gpio_indx,
                                qdev_get_gpio_in_named(s->puf_acc_err_sink,
                                                       gpio_name, gpio_indx));
}

static void zynqmp_pufop_realize(DeviceState *dev, Error **errp)
{
    zynqmp_pufop_hook_err_out(dev);
}

static void zynqmp_pufop_init(Object *obj)
{
    Zynqmp_PUFOP *s = ZYNQMP_PUFOP(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_ZYNQMP_PUFOP, R_MAX * 4);
    reg_array = register_init_block32(DEVICE(obj), zynqmp_pufop_regs_info,
                                      ARRAY_SIZE(zynqmp_pufop_regs_info),
                                      s->regs_info, s->regs,
                                      &zynqmp_pufop_ops,
                                      ZYNQMP_PUFOP_ERR_DEBUG,
                                      R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x00, &reg_array->mem);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription zynqmp_pufop_vmstate = {
    .name = TYPE_ZYNQMP_PUFOP,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Zynqmp_PUFOP, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property zynqmp_pufop_props[] = {
    DEFINE_PROP_LINK("efuse",
                     Zynqmp_PUFOP, efuse,
                     TYPE_XLNX_EFUSE, XLNXEFuse *),

    DEFINE_PROP_LINK("zynqmp-aes-key-sink-puf",
                     Zynqmp_PUFOP, puf_keysink,
                     TYPE_ZYNQMP_AES_KEY_SINK, ZynqMPAESKeySink *),

    DEFINE_PROP_LINK("puf-acc-err-sink",
                     Zynqmp_PUFOP, puf_acc_err_sink,
                     TYPE_DEVICE, DeviceState *),

    DEFINE_PROP_END_OF_LIST(),
};

static void zynqmp_pufop_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = zynqmp_pufop_realize;
    device_class_set_props(dc, zynqmp_pufop_props);
    dc->vmsd = &zynqmp_pufop_vmstate;
}

static const TypeInfo zynqmp_pufop_info = {
    .name          = TYPE_ZYNQMP_PUFOP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Zynqmp_PUFOP),
    .class_init    = zynqmp_pufop_class_init,
    .instance_init = zynqmp_pufop_init,
    .interfaces = (InterfaceInfo[]) {
        { }
    }
};

static void zynqmp_pufop_register_types(void)
{
    type_register_static(&zynqmp_pufop_info);
}

type_init(zynqmp_pufop_register_types);
