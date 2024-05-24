/*
 * QEMU model of AMD/Xilinx ASU True Random Number Generator
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/misc/xlnx-versal-asu-trng.h"

#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#ifndef XLNX_ASU_TRNG_ERR_DEBUG
#define XLNX_ASU_TRNG_ERR_DEBUG 0
#endif

/* 3 sub-regions */
REG32(ASU_TRNG_OUT, 0x00000)  /* Output in autoproc mode */
REG32(ASU_TRNG_CTL, 0x10000)  /* Control */
REG32(ASU_TRNG_RNG, 0x11000)  /* Generator */
    FIELD(CTRL, PRNGSTART, 5, 1)
    FIELD(CTRL, TRSSEN, 2, 1)

#define ASU_TRNG_OUT_R_MAX    (0x1000 / 4)

/* Control registers */
REG32(INTR_STS, 0x0)
    FIELD(INTR_STS, TRNG_FULL, 16, 1)
    FIELD(INTR_STS, TRNG_AC, 8, 1)
    FIELD(INTR_STS, TRNG_INT, 0, 1)
REG32(INTR_EN, 0x4)
    FIELD(INTR_EN, TRNG_FULL, 16, 1)
    FIELD(INTR_EN, TRNG_AC, 8, 1)
    FIELD(INTR_EN, TRNG_INT, 0, 1)
REG32(INTR_DIS, 0x8)
    FIELD(INTR_DIS, TRNG_FULL, 16, 1)
    FIELD(INTR_DIS, TRNG_AC, 8, 1)
    FIELD(INTR_DIS, TRNG_INT, 0, 1)
REG32(INTR_MASK, 0xc)
    FIELD(INTR_MASK, TRNG_FULL, 16, 1)
    FIELD(INTR_MASK, TRNG_AC, 8, 1)
    FIELD(INTR_MASK, TRNG_INT, 0, 1)
REG32(INTR_TRIG, 0x10)
    FIELD(INTR_TRIG, TRNG_FULL, 16, 1)
    FIELD(INTR_TRIG, TRNG_AC, 8, 1)
    FIELD(INTR_TRIG, TRNG_INT, 0, 1)
REG32(ECO, 0x14)
REG32(NRN_AVAIL, 0x18)
    FIELD(NRN_AVAIL, NUM, 0, 6)
REG32(RESET, 0x1c)
    FIELD(RESET, VAL, 0, 1)
REG32(OSC_EN, 0x20)
    FIELD(OSC_EN, VAL, 0, 1)
REG32(AUTOPROC, 0x28)
    FIELD(AUTOPROC, CODE, 0, 1)
REG32(NRNPS, 0x2c)
    FIELD(NRNPS, NUM, 0, 10)
REG32(TRNG_SLV_ERR_CTRL, 0x30)
    FIELD(TRNG_SLV_ERR_CTRL, ENABLE, 0, 1)
REG32(TRNG_XRESP, 0x34)
    FIELD(TRNG_XRESP, XRESP, 0, 2)

#define ASU_TRNG_CTL_R_MAX (R_TRNG_XRESP + 1)
QEMU_BUILD_BUG_ON(ASU_TRNG_CTL_R_MAX * 4 != sizeof_field(XlnxAsuTRng, regs));

enum {
    ASU_TRNG_FIFO_DEPTH = 32, /* words */
};

static bool asu_trng_is_autoproc(XlnxAsuTRng *s)
{
    return ARRAY_FIELD_EX32(s->regs, AUTOPROC, CODE);
}

static bool asu_trng_core_accessible(Object *dev, bool wr)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(dev);

    /* All accesses ignored if held in reset */
    return !ARRAY_FIELD_EX32(s->regs, RESET, VAL);
}

static bool asu_trng_trss_avail(Object *dev)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(dev);
    return ARRAY_FIELD_EX32(s->regs, OSC_EN, VAL);
}

static void intr_update_irq(XlnxAsuTRng *s)
{
    bool pending = s->regs[R_INTR_STS] & ~s->regs[R_INTR_MASK];
    qemu_set_irq(s->irq_intr, pending);
}

static void intr_sts_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(reg->opaque);
    intr_update_irq(s);
}

static uint64_t intr_en_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_INTR_MASK] &= ~val;
    intr_update_irq(s);
    return 0;
}

static uint64_t intr_dis_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_INTR_MASK] |= val;
    intr_update_irq(s);
    return 0;
}

static uint64_t intr_trig_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_INTR_STS] |= val;
    intr_update_irq(s);
    return 0;
}

static void intr_update_trng_int(Object *dev, bool pending)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(dev);

    ARRAY_FIELD_DP32(s->regs, INTR_STS, TRNG_INT, pending);
    intr_update_irq(s);
}

static void asu_trng_ctl_autoproc_enter(XlnxAsuTRng *s)
{
    s->trng.autoproc(&s->trng, (R_CTRL_PRNGSTART_MASK | R_CTRL_TRSSEN_MASK));

    /* FIFO depth is simulated as always full */
    s->regs[R_NRN_AVAIL] = ASU_TRNG_FIFO_DEPTH;
    ARRAY_FIELD_DP32(s->regs, INTR_STS, TRNG_FULL, 1);
    intr_update_irq(s);
}

static void asu_trng_ctl_autoproc_leave(XlnxAsuTRng *s)
{
    s->trng.autoproc(&s->trng, 0);

    s->regs[R_NRN_AVAIL] = 0;
    ARRAY_FIELD_DP32(s->regs, INTR_STS, TRNG_FULL, 0);
    ARRAY_FIELD_DP32(s->regs, INTR_STS, TRNG_AC, 1);
    intr_update_irq(s);
}

static uint64_t asu_trng_ctl_autoproc_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(reg->opaque);
    uint32_t *r32 = reg->data;
    uint32_t v_old = *r32;
    uint32_t to_1 = find_bits_to_1(v_old, val64);
    uint32_t to_0 = find_bits_to_0(v_old, val64);

    *r32 = val64;

    if (FIELD_EX32(to_1, AUTOPROC, CODE)) {
        asu_trng_ctl_autoproc_enter(s);
    }

    if (FIELD_EX32(to_0, AUTOPROC, CODE)) {
        asu_trng_ctl_autoproc_leave(s);
    }

    return *r32;
}

static uint64_t asu_trng_ctl_reset_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(reg->opaque);
    uint32_t v_old = register_data_32(reg);
    uint32_t to_1 = find_bits_to_1(v_old, val64);

    if (FIELD_EX32(to_1, RESET, VAL)) {
        s->trng.hard_rst(&s->trng);
    }

    return val64;
}

static const RegisterAccessInfo asu_trng_ctl_regs_info[] = {
    {   .name = "INTR_STS",  .addr = A_INTR_STS,
        .rsvd = 0xfffefefe,
        .w1c = 0x10101,
        .post_write = intr_sts_postw,
    },{ .name = "INTR_EN",  .addr = A_INTR_EN,
        .rsvd = 0xfffefefe,
        .ro = 0xfffefefe,
        .pre_write = intr_en_prew,
    },{ .name = "INTR_DIS",  .addr = A_INTR_DIS,
        .rsvd = 0xfffefefe,
        .ro = 0xfffefefe,
        .pre_write = intr_dis_prew,
    },{ .name = "INTR_MASK",  .addr = A_INTR_MASK,
        .reset = 0x10101,
        .rsvd = 0xfffefefe,
        .ro = 0xffffffff,
    },{ .name = "INTR_TRIG",  .addr = A_INTR_TRIG,
        .rsvd = 0xfffefefe,
        .ro = 0xfffefefe,
        .pre_write = intr_trig_prew,
    },{ .name = "ECO",  .addr = A_ECO,
    },{ .name = "NRN_AVAIL",  .addr = A_NRN_AVAIL,
        .ro = 0x3f,
    },{ .name = "RESET",  .addr = A_RESET,
        .reset = 0x1,
        .pre_write = asu_trng_ctl_reset_prew,
    },{ .name = "OSC_EN",  .addr = A_OSC_EN,
    },{ .name = "AUTOPROC",  .addr = A_AUTOPROC,
        .pre_write = asu_trng_ctl_autoproc_prew,
    },{ .name = "NRNPS",  .addr = A_NRNPS,
        .reset = 0xff,
    },{ .name = "TRNG_SLV_ERR_CTRL",  .addr = A_TRNG_SLV_ERR_CTRL,
        .reset = 0x1,
    },{ .name = "TRNG_XRESP",  .addr = A_TRNG_XRESP,
        .reset = 0x2,
    }
};

static void asu_trng_reset_enter(Object *obj, ResetType type)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(obj);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    intr_update_irq(s);

    s->trng.hard_rst(&s->trng);
}

static void asu_trng_autoproc_write(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size)
{
    /* Writes are silently ignored */
}

static void asu_trng_autoproc_get_be(XlnxAsuTRng *s, void *data, unsigned size)
{
    /* Return all 0 if not in auto-proc mode or in reset */
    if (!asu_trng_is_autoproc(s) || ARRAY_FIELD_EX32(s->regs, RESET, VAL)) {
        memset(data, 0, size);
    } else {
        s->trng.get_data(&s->trng, data, size);
    }
}

static uint64_t asu_trng_autoproc_rd64(void *opaque, hwaddr addr, unsigned size)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(opaque);
    uint8_t be[8];

    g_assert(size <= sizeof(be));
    asu_trng_autoproc_get_be(s, be, size);
    return xlnx_prng_ldn_be_p(be, size);
}

static MemTxResult asu_trng_autoproc_access(MemoryTransaction *tr)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(tr->opaque);

    if (tr->rw) {
        /* Writes are always silently ignored */
        return MEMTX_OK;
    }

    if (tr->size <= 8) {
        /* Data up to 8 bytes is as value */
        tr->data.u64 = asu_trng_autoproc_rd64(s, tr->addr, tr->size);
    } else {
        /* Convert to array of 32-bit words */
        asu_trng_autoproc_get_be(s, tr->data.p8, tr->size);
        xlnx_prng_be32_to_cpus(tr->data.p8, tr->size);
    }

    return MEMTX_OK;
}

static const MemoryRegionOps asu_trng_autoproc_ops = {
    .read = asu_trng_autoproc_rd64,
    .write = asu_trng_autoproc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,

    /* Need Xilinx extension to provide mmio size > 8 */
    .access = asu_trng_autoproc_access,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 256,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 256,
    },
};

static const MemoryRegionOps asu_trng_ctl_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void asu_trng_unrealize(DeviceState *dev)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(dev);

    object_property_set_bool(OBJECT(&s->trng), "realized", false, &error_fatal);
    object_unref(&s->trng);
}

static void asu_trng_realize(DeviceState *dev, Error **errp)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(dev);

    object_property_set_bool(OBJECT(&s->trng), "realized", true, errp);
}

static MemoryRegion *asu_trng_mr_rename(MemoryRegion *mr,
                                        const char *bn, const char *suffix)
{
    g_autofree char *new_name = g_strjoin(NULL, bn, suffix, NULL);

    /* Save enough to call memory_region_init_io */
    const void *ops = mr->ops;
    void *opaque = mr->opaque;
    Object *owner = memory_region_owner(mr);
    uint64_t mr_size = memory_region_size(mr);

    /* Finalize it */
    object_unparent(OBJECT(mr));

    /* Recreate it with new name */
    memory_region_init_io(mr, owner, ops, opaque, new_name, mr_size);

    return mr;
}

static MemoryRegion *asu_trng_init_generator(XlnxAsuTRng *s)
{
    object_initialize_child(OBJECT(s), "trng",
                            &s->trng, TYPE_XLNX_TRNG1_R2);
    g_assert(s->trng.iomem);

    s->trng.intr_update = intr_update_trng_int;
    s->trng.accessible = asu_trng_core_accessible;
    s->trng.trss_avail = asu_trng_trss_avail;
    s->trng.seed_life = &s->regs[R_NRNPS];

    return s->trng.iomem;
}

static void asu_trng_init(Object *obj)
{
    XlnxAsuTRng *s = XLNX_ASU_TRNG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    MemoryRegion *ctl_mr, *rng_mr;
    const char *mrn_base;
    uint64_t io_sz;

    reg_array =
        register_init_block32(DEVICE(obj), asu_trng_ctl_regs_info,
                              ARRAY_SIZE(asu_trng_ctl_regs_info),
                              s->regs_info, s->regs,
                              &asu_trng_ctl_ops,
                              XLNX_ASU_TRNG_ERR_DEBUG,
                              ASU_TRNG_CTL_R_MAX * 4);

    ctl_mr = &reg_array->mem;
    rng_mr = asu_trng_init_generator(s);

    mrn_base = memory_region_name(ctl_mr);
    ctl_mr = asu_trng_mr_rename(ctl_mr, mrn_base, "-ctl");
    rng_mr = asu_trng_mr_rename(rng_mr, mrn_base, "-rng");

    io_sz = A_ASU_TRNG_RNG + memory_region_size(rng_mr);
    memory_region_init_io(&s->iomem, obj, &asu_trng_autoproc_ops,
                          s, TYPE_XLNX_ASU_TRNG, io_sz);
    memory_region_add_subregion(&s->iomem, A_ASU_TRNG_CTL, ctl_mr);
    memory_region_add_subregion(&s->iomem, A_ASU_TRNG_RNG, rng_mr);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq_intr);
}

static Property asu_trng_props[] = {
    DEFINE_PROP_UINT64("forced-prng", XlnxAsuTRng, trng.entropy.trss_seed, 0),
    DEFINE_PROP_STRING("prng-type", XlnxAsuTRng, trng.prng.type),

    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_asu_trng = {
    .name = TYPE_XLNX_ASU_TRNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxAsuTRng, ASU_TRNG_CTL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void asu_trng_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_asu_trng;
    dc->realize = asu_trng_realize;
    dc->unrealize = asu_trng_unrealize;
    rc->phases.enter = asu_trng_reset_enter;

    device_class_set_props(dc, asu_trng_props);
}

static const TypeInfo asu_trng_info = {
    .name          = TYPE_XLNX_ASU_TRNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxAsuTRng),
    .class_init    = asu_trng_class_init,
    .instance_init = asu_trng_init,
};

static void asu_trng_register_types(void)
{
    type_register_static(&asu_trng_info);
}

type_init(asu_trng_register_types)
