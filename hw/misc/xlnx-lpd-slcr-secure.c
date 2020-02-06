/*
 * QEMU model of the LPDSLCRSecure
 *
 * Copyright (c) 2016 Greensocs.
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
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "qemu/log.h"
#include "hw/irq.h"

#ifndef XILINX_LPD_SLCR_SECURE_ERR_DEBUG
#define XILINX_LPD_SLCR_SECURE_ERR_DEBUG 0
#endif

#define TYPE_XILINX_LPD_SLCR_SECURE "xlnx.lpd-slcr-secure"

#define XILINX_LPD_SLCR_SECURE(obj) \
     OBJECT_CHECK(LPDSLCRSecure, (obj), TYPE_XILINX_LPD_SLCR_SECURE)

REG32(CTRL, 0x4)
    FIELD(CTRL, SLVERR_ENABLE, 0, 1)
REG32(ISR, 0x8)
    FIELD(ISR, ADDR_DECODE_ERR, 0, 1)
REG32(IMR, 0xc)
    FIELD(IMR, ADDR_DECODE_ERR, 0, 1)
REG32(IER, 0x10)
    FIELD(IER, ADDR_DECODE_ERR, 0, 1)
REG32(IDR, 0x14)
    FIELD(IDR, ADDR_DECODE_ERR, 0, 1)
REG32(ITR, 0x18)
    FIELD(ITR, ADDR_DECODE_ERR, 0, 1)
REG32(SLCR_RPU, 0x20)
    FIELD(SLCR_RPU, TZ_R5_1, 1, 1)
    FIELD(SLCR_RPU, TZ_R5_0, 0, 1)
REG32(SLCR_ADMA, 0x24)
    FIELD(SLCR_ADMA, TZ, 0, 8)
REG32(SAFETY_CHK, 0x30)
REG32(SLCR_USB, 0x34)
    FIELD(SLCR_USB, TZ_USB3_1, 1, 1)
    FIELD(SLCR_USB, TZ_USB3_0, 0, 1)

#define R_MAX (R_SLCR_USB + 1)

typedef struct LPDSLCRSecure {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_isr;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} LPDSLCRSecure;

static void isr_update_irq(LPDSLCRSecure *s)
{
    bool pending = s->regs[R_ISR] & ~s->regs[R_IMR];
    qemu_set_irq(s->irq_isr, pending);
}

static void isr_postw(RegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    isr_update_irq(s);
}

static uint64_t ier_prew(RegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] &= ~val;
    isr_update_irq(s);
    return 0;
}

static uint64_t idr_prew(RegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] |= val;
    isr_update_irq(s);
    return 0;
}

static uint64_t itr_prew(RegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_ISR] |= val;
    isr_update_irq(s);
    return 0;
}

static RegisterAccessInfo lpd_slcr_secure_regs_info[] = {
    { .name = "CTRL",  .addr = A_CTRL,
    },{ .name = "ISR",  .addr = A_ISR,
        .w1c = 0x1,
        .post_write = isr_postw,
    },{ .name = "IMR",  .addr = A_IMR,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "IER",  .addr = A_IER,
        .pre_write = ier_prew,
    },{ .name = "IDR",  .addr = A_IDR,
        .pre_write = idr_prew,
    },{ .name = "ITR",  .addr = A_ITR,
        .pre_write = itr_prew,
    },{ .name = "SLCR_RPU",   .addr = A_SLCR_RPU,
    },{ .name = "SLCR_ADMA",  .addr = A_SLCR_ADMA,
    },{ .name = "SAFETY_CHK", .addr = A_SAFETY_CHK,
    },{ .name = "SLCR_USB",   .addr = A_SLCR_USB,
    }
};

static void lpd_slcr_secure_reset(DeviceState *dev)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    isr_update_irq(s);
}

static const MemoryRegionOps lpd_slcr_secure_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void lpd_slcr_secure_realize(DeviceState *dev, Error **errp)
{
    /* Delete this if not necessary */
}

static void lpd_slcr_secure_init(Object *obj)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_LPD_SLCR_SECURE, R_MAX * 4);

    reg_array =
        register_init_block32(DEVICE(obj), lpd_slcr_secure_regs_info,
                              ARRAY_SIZE(lpd_slcr_secure_regs_info),
                              s->regs_info, s->regs,
                              &lpd_slcr_secure_ops,
                              XILINX_LPD_SLCR_SECURE_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_isr);
}

static const VMStateDescription vmstate_lpd_slcr_secure = {
    .name = TYPE_XILINX_LPD_SLCR_SECURE,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LPDSLCRSecure, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void lpd_slcr_secure_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = lpd_slcr_secure_reset;
    dc->realize = lpd_slcr_secure_realize;
    dc->vmsd = &vmstate_lpd_slcr_secure;
}

static const TypeInfo lpd_slcr_secure_info = {
    .name          = TYPE_XILINX_LPD_SLCR_SECURE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LPDSLCRSecure),
    .class_init    = lpd_slcr_secure_class_init,
    .instance_init = lpd_slcr_secure_init,
};

static void lpd_slcr_secure_register_types(void)
{
    type_register_static(&lpd_slcr_secure_info);
}

type_init(lpd_slcr_secure_register_types)
