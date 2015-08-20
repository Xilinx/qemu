/*
 * QEMU model of Ronaldo CSU Core Functionality
 *
 * For the most part, a dummy device model.
 *
 * Copyright (c) 2013 Peter Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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

#include "hw/sysbus.h"
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"

#ifndef RONALDO_CSU_CORE_ERR_DEBUG
#define RONALDO_CSU_CORE_ERR_DEBUG 0
#endif

#define TYPE_RONALDO_CSU_CORE "xlnx,zynqmp-csu-core"

#define RONALDO_CSU_CORE(obj) \
     OBJECT_CHECK(RonaldoCSUCore, (obj), TYPE_RONALDO_CSU_CORE)

REG32(ISR, 0x00)
REG32(IMR, 0x04)
REG32(IER, 0x08)
REG32(IDR, 0x0c)

#define R_IXR_APB_SLVERR    (1 << 31)
#define R_IXR_TMR_FATAL     (1 << 0)
#define R_IXR_RSVD          (0x7ffffffe)

REG32(MULTI_BOOT, 0x10)

REG32(VERSION, 0x44)
    FIELD(VERSION, PLATFORM, 4, 12)
    FIELD(VERSION, RTL, 8, 4)
    FIELD(VERSION, PS, 4, 0)

/* We don't model CSU ROM SHA digest which owns offset 0x60, and the
 * early versions of CSU Core have the versions register at offset
 * 0x60. We mirror the version register there, rather than try and
 * version the core.
 */

REG32(VERSION_EARLY, 0x60)
    FIELD(VERSION_EARLY, PLATFORM, 4, 12)
    FIELD(VERSION_EARLY, RTL, 8, 4)
    FIELD(VERSION_EARLY, PS, 4, 0)

#define R_MAX ((R_VERSION_EARLY) + 1)

typedef struct RonaldoCSUCore RonaldoCSUCore;

struct RonaldoCSUCore {
    SysBusDevice busdev;
    MemoryRegion iomem;

    qemu_irq irq;

    uint8_t rtl_version;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};


static void ronaldo_csu_core_update_irq(RonaldoCSUCore *s)
{
    qemu_set_irq(s->irq, !!(s->regs[R_ISR] & ~s->regs[R_IMR]));
}

/* FIXME: Does the multi Boot register survive reset? */

static void ronaldo_csu_core_reset(DeviceState *dev)
{
    RonaldoCSUCore *s = RONALDO_CSU_CORE(dev);
    int i;
 
    for (i = 0; i < R_MAX; ++i) {
        /*FIXME: Exclude bootmode and multiboot and reset reason */
        register_reset(&s->regs_info[i]);
    }
    AF_DP32(s->regs, VERSION, RTL, s->rtl_version);
    AF_DP32(s->regs, VERSION_EARLY, RTL, s->rtl_version);
}

static uint64_t int_enable_pre_write(RegisterInfo *reg, uint64_t val)
{
    RonaldoCSUCore *s = RONALDO_CSU_CORE(reg->opaque);
    uint32_t v32 = val;

    s->regs[R_IMR] &= ~v32;
    ronaldo_csu_core_update_irq(s);
    return 0;
}

static uint64_t int_disable_pre_write(RegisterInfo *reg, uint64_t val)
{
    RonaldoCSUCore *s = RONALDO_CSU_CORE(reg->opaque);
    uint32_t v32 = val;

    s->regs[R_IMR] |= v32;
    ronaldo_csu_core_update_irq(s);
    return 0;
}

static const RegisterAccessInfo ronaldo_csu_core_regs_info[] = {
    {   .name = "Interrupt Status",             .decode.addr = A_ISR,
            .rsvd = R_IXR_RSVD,
            .w1c = ~R_IXR_RSVD,
    },{ .name = "Interrupt Mask",               .decode.addr = A_IMR,
            .rsvd = R_IXR_RSVD,
            .ro = ~0,
    },{ .name = "Interrupt Enable",             .decode.addr = A_IER,
            .rsvd = R_IXR_RSVD,
            .pre_write = int_enable_pre_write,
    },{ .name = "Interrupt Disable",            .decode.addr = A_IDR,
            .rsvd = R_IXR_RSVD,
            .pre_write = int_disable_pre_write,
    },{ .name = "Multi Boot",                   .decode.addr = A_MULTI_BOOT,
    },{ .name = "Version",                      .decode.addr = A_VERSION,
            .ro = ~0,
            .reset = 0x3 << R_VERSION_PLATFORM_SHIFT,
    },{ .name = "Version (RTL 3.1 / earlier)",  .decode.addr = A_VERSION_EARLY,
            .ro = ~0,
            .reset = 0x3 << R_VERSION_PLATFORM_SHIFT,
    }
};

static const MemoryRegionOps ronaldo_csu_core_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void ronaldo_csu_core_realize(DeviceState *dev, Error **errp)
{
    RonaldoCSUCore *s = RONALDO_CSU_CORE(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < ARRAY_SIZE(ronaldo_csu_core_regs_info); ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    ronaldo_csu_core_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &ronaldo_csu_core_regs_info[i],
            .debug = RONALDO_CSU_CORE_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(dev), &ronaldo_csu_core_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, r->access->decode.addr, &r->mem);
    }
    return;
}

static void ronaldo_csu_core_init(Object *obj)
{
    RonaldoCSUCore *s = RONALDO_CSU_CORE(obj);

    memory_region_init(&s->iomem, obj, "MMIO", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

/* FIXME: With no regs we are actually stateless. Although post load we need
 * to call notify() to start up the fire-hose of zeros again.
 */

static const VMStateDescription vmstate_ronaldo_csu_core = {
    .name = "ronaldo_csu_core",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, RonaldoCSUCore, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property ronaldo_csu_core_props [] = {
    DEFINE_PROP_UINT8("rtl-version", RonaldoCSUCore, rtl_version, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ronaldo_csu_core_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = ronaldo_csu_core_props;
    dc->reset = ronaldo_csu_core_reset;
    dc->realize = ronaldo_csu_core_realize;
    dc->vmsd = &vmstate_ronaldo_csu_core;
}

static const TypeInfo ronaldo_csu_core_info = {
    .name          = TYPE_RONALDO_CSU_CORE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RonaldoCSUCore),
    .class_init    = ronaldo_csu_core_class_init,
    .instance_init = ronaldo_csu_core_init,
};

static void ronaldo_csu_core_register_types(void)
{
    type_register_static(&ronaldo_csu_core_info);
}

type_init(ronaldo_csu_core_register_types)
