/*
 * CPM6 Clock and Reset Control Registers
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "hw/misc/amd-cpm6-crx.h"
#include "trace.h"

REG32(CPLL_CFG, 0x44)
    FIELD(CPLL_CFG, LOCK_DLY, 25, 7)

REG32(CPLL_CTRL, 0x40)
    FIELD(CPLL_CTRL, RESET, 0, 1)
    FIELD(CPLL_CTRL, BYPASS, 3, 1)
    FIELD(CPLL_CTRL, FBDIV, 8, 8)
    FIELD(CPLL_CTRL, RESERVED_2, 16, 2)

REG32(PLL_STATUS, 0x50)
    FIELD(PLL_STATUS, CPLL_LOCK, 0, 1)
    FIELD(PLL_STATUS, CPLL_STABLE, 2, 1)

static uint32_t pll_status_read(const AmdCpm6CrxState *s)
{
    bool bypass, reset;
    uint32_t ret = 0;

    bypass = FIELD_EX32(s->cpll_ctrl, CPLL_CTRL, BYPASS);
    reset = FIELD_EX32(s->cpll_ctrl, CPLL_CTRL, RESET);

    ret = FIELD_DP32(ret, PLL_STATUS, CPLL_LOCK, !reset);
    ret = FIELD_DP32(ret, PLL_STATUS, CPLL_STABLE, !reset || bypass);

    return ret;

}

static uint64_t amd_cpm6_crx_read(void *opaque, hwaddr addr, unsigned int size)
{
    AmdCpm6CrxState *s = AMD_CPM6_CRX(opaque);
    uint64_t ret;

    switch (addr) {
    case A_CPLL_CTRL:
        ret = s->cpll_ctrl;
        break;
    case A_CPLL_CFG:
        ret = s->cpll_cfg;
        break;
    case A_PLL_STATUS:
        ret = pll_status_read(s);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AMD_CPM6_CRX ": read to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_amd_cpm6_crx_read(addr, ret, size);
    return ret;
}

static void amd_cpm6_crx_write(void *opaque, hwaddr addr, uint64_t value,
                               unsigned int size)
{
    AmdCpm6CrxState *s = AMD_CPM6_CRX(opaque);

    trace_amd_cpm6_crx_write(addr, value, size);

    switch (addr) {
    case A_CPLL_CTRL:
        s->cpll_ctrl = value;
        break;
    case A_CPLL_CFG:
        s->cpll_cfg = value;
        break;

    case A_PLL_STATUS:
        /* read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AMD_CPM6_CRX ": write to read-only register"
                      " at offset 0x%" HWADDR_PRIx "\n",
                      addr);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AMD_CPM6_CRX ": write to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}


static const MemoryRegionOps amd_cpm6_crx_ops = {
    .read = amd_cpm6_crx_read,
    .write = amd_cpm6_crx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void amd_cpm6_crx_reset_enter(Object *obj, ResetType type)
{
    AmdCpm6CrxState *s = AMD_CPM6_CRX(obj);

    s->cpll_ctrl = FIELD_DP32(0, CPLL_CTRL, RESET, 1);
    s->cpll_ctrl = FIELD_DP32(s->cpll_ctrl, CPLL_CTRL, BYPASS, 1);
    s->cpll_ctrl = FIELD_DP32(s->cpll_ctrl, CPLL_CTRL, FBDIV, 0x48);
    s->cpll_ctrl = FIELD_DP32(s->cpll_ctrl, CPLL_CTRL, RESERVED_2, 1);

    s->cpll_cfg = FIELD_DP32(0, CPLL_CFG, LOCK_DLY, 1);
}

static void amd_cpm6_crx_realize(DeviceState *dev, Error **errp)
{
    AmdCpm6CrxState *s = AMD_CPM6_CRX(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &amd_cpm6_crx_ops,
                          s, TYPE_AMD_CPM6_CRX,
                          AMD_CPM6_CRX_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void amd_cpm6_crx_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = amd_cpm6_crx_realize;
    rc->phases.enter = amd_cpm6_crx_reset_enter;
}

static const TypeInfo amd_cpm6_crx_info = {
    .name = TYPE_AMD_CPM6_CRX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AmdCpm6CrxState),
    .class_init = amd_cpm6_crx_class_init,
};

static void amd_cpm6_crx_register_types(void)
{
    type_register_static(&amd_cpm6_crx_info);
}

type_init(amd_cpm6_crx_register_types)
