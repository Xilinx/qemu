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
#include "hw/register-dep.h"
#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "qemu/log.h"

#ifndef XILINX_LPD_SLCR_SECURE_ERR_DEBUG
#define XILINX_LPD_SLCR_SECURE_ERR_DEBUG 0
#endif

#define TYPE_XILINX_LPD_SLCR_SECURE "xlnx.lpd-slcr-secure"

#define XILINX_LPD_SLCR_SECURE(obj) \
     OBJECT_CHECK(LPDSLCRSecure, (obj), TYPE_XILINX_LPD_SLCR_SECURE)

DEP_REG32(CTRL, 0x4)
    DEP_FIELD(CTRL, SLVERR_ENABLE, 1, 0)
DEP_REG32(ISR, 0x8)
    DEP_FIELD(ISR, ADDR_DECODE_ERR, 1, 0)
DEP_REG32(IMR, 0xc)
    DEP_FIELD(IMR, ADDR_DECODE_ERR, 1, 0)
DEP_REG32(IER, 0x10)
    DEP_FIELD(IER, ADDR_DECODE_ERR, 1, 0)
DEP_REG32(IDR, 0x14)
    DEP_FIELD(IDR, ADDR_DECODE_ERR, 1, 0)
DEP_REG32(ITR, 0x18)
    DEP_FIELD(ITR, ADDR_DECODE_ERR, 1, 0)
DEP_REG32(SLCR_RPU, 0x20)
    DEP_FIELD(SLCR_RPU, TZ_R5_0, 1, 0)
    DEP_FIELD(SLCR_RPU, TZ_R5_1, 1, 1)
DEP_REG32(SLCR_ADMA, 0x24)
    DEP_FIELD(SLCR_RPU, TZ, 1, 0)
DEP_REG32(SAFETY_CHK, 0x30)
DEP_REG32(SLCR_USB, 0x34)
    DEP_FIELD(SLCR_USB, TZ_USB3_0, 1, 0)
    DEP_FIELD(SLCR_USB, TZ_USB3_1, 1, 1)

#define R_MAX (R_SLCR_USB + 1)

typedef struct LPDSLCRSecure {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_isr;

    uint32_t regs[R_MAX];
    DepRegisterInfo regs_info[R_MAX];
} LPDSLCRSecure;

static void isr_update_irq(LPDSLCRSecure *s)
{
    bool pending = s->regs[R_ISR] & ~s->regs[R_IMR];
    qemu_set_irq(s->irq_isr, pending);
}

static void isr_postw(DepRegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    isr_update_irq(s);
}

static uint64_t ier_prew(DepRegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] &= ~val;
    isr_update_irq(s);
    return 0;
}

static uint64_t idr_prew(DepRegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IMR] |= val;
    isr_update_irq(s);
    return 0;
}

static uint64_t itr_prew(DepRegisterInfo *reg, uint64_t val64)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_ISR] |= val;
    isr_update_irq(s);
    return 0;
}

static DepRegisterAccessInfo lpd_slcr_secure_regs_info[] = {
    { .name = "CTRL",  .decode.addr = A_CTRL,
    },{ .name = "ISR",  .decode.addr = A_ISR,
        .w1c = 0x1,
        .post_write = isr_postw,
    },{ .name = "IMR",  .decode.addr = A_IMR,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "IER",  .decode.addr = A_IER,
        .pre_write = ier_prew,
    },{ .name = "IDR",  .decode.addr = A_IDR,
        .pre_write = idr_prew,
    },{ .name = "ITR",  .decode.addr = A_ITR,
        .pre_write = itr_prew,
    },{ .name = "SLCR_RPU",   .decode.addr = A_SLCR_RPU,
    },{ .name = "SLCR_ADMA",  .decode.addr = A_SLCR_ADMA,
    },{ .name = "SAFETY_CHK", .decode.addr = A_SAFETY_CHK,
    },{ .name = "SLCR_USB",   .decode.addr = A_SLCR_USB,
    }
};

static void lpd_slcr_secure_reset(DeviceState *dev)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        dep_register_reset(&s->regs_info[i]);
    }

    isr_update_irq(s);
}

static uint64_t lpd_slcr_secure_read(void *opaque, hwaddr addr, unsigned size)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(opaque);
    DepRegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: read from %" HWADDR_PRIx "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr);
        return 0;
    }
    return dep_register_read(r);
}

static void lpd_slcr_secure_write(void *opaque, hwaddr addr, uint64_t value,
                                  unsigned size)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(opaque);
    DepRegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: write to %" HWADDR_PRIx "=%" PRIx64 "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr, value);
        return;
    }
    dep_register_write(r, value, ~0);
}

static const MemoryRegionOps lpd_slcr_secure_ops = {
    .read = lpd_slcr_secure_read,
    .write = lpd_slcr_secure_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void lpd_slcr_secure_realize(DeviceState *dev, Error **errp)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(lpd_slcr_secure_regs_info); ++i) {
        DepRegisterInfo *r =
                &s->regs_info[lpd_slcr_secure_regs_info[i].decode.addr / 4];

        *r = (DepRegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    lpd_slcr_secure_regs_info[i].decode.addr / 4],
            .data_size = sizeof(uint32_t),
            .access = &lpd_slcr_secure_regs_info[i],
            .debug = XILINX_LPD_SLCR_SECURE_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
    }
}

static void lpd_slcr_secure_init(Object *obj)
{
    LPDSLCRSecure *s = XILINX_LPD_SLCR_SECURE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &lpd_slcr_secure_ops, s,
                          TYPE_XILINX_LPD_SLCR_SECURE, R_MAX * 4);
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
