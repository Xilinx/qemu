/*
 * gtmpw_control_core_gtmpw_ublaze_top
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "hw/misc/amd-cpm6-gtmpw-npi.h"
#include "trace.h"

REG32(PCSR_MASK, 0x0)

REG32(PCSR_CONTROL, 0x4)
    FIELD(PCSR_CONTROL, MEM_CLEAR_TRIGGER, 18, 1)
    FIELD(PCSR_CONTROL, BISR_TRIGGER, 29, 1)

REG32(PCSR_STATUS, 0x8)
    FIELD(PCSR_STATUS, PCSRLOCK, 0, 1)
    FIELD(PCSR_STATUS, CALDONE, 4, 1)
    FIELD(PCSR_STATUS, MEM_CLEAR_DONE, 6, 1)
    FIELD(PCSR_STATUS, MEM_CLEAR_PASS, 7, 1)
    FIELD(PCSR_STATUS, BISR_DONE, 19, 1)
    FIELD(PCSR_STATUS, BISR_PASS, 20, 1)
    FIELD(PCSR_STATUS, GTPOWERGOOD, 21, 1)

REG32(PCSR_LOCK, 0xc)
    FIELD(PCSR_LOCK, STATE, 0, 1)

#define GTMPW_LOCK_MAGIC 0xf9e8d7c6

static uint64_t amd_cpm6_gtmpw_npi_read(void *opaque, hwaddr addr,
                                        unsigned int size)
{
    AmdCpm6GtmpwNpiState *s = AMD_CPM6_GTMPW_NPI(opaque);
    uint64_t ret;

    switch (addr) {
    case A_PCSR_MASK:
        ret = s->pcsr_mask;
        break;
    case A_PCSR_CONTROL:
        ret = s->pcsr_control;
        break;
    case A_PCSR_STATUS:
        ret = R_PCSR_STATUS_CALDONE_MASK;
        ret |= R_PCSR_STATUS_GTPOWERGOOD_MASK;
        ret = FIELD_DP32(ret, PCSR_STATUS, PCSRLOCK, s->lock);
        ret = FIELD_DP32(ret, PCSR_STATUS, MEM_CLEAR_DONE,
                         FIELD_EX32(s->pcsr_control, PCSR_CONTROL,
                                    MEM_CLEAR_TRIGGER));
        ret = FIELD_DP32(ret, PCSR_STATUS, MEM_CLEAR_PASS,
                         FIELD_EX32(s->pcsr_control, PCSR_CONTROL,
                                    MEM_CLEAR_TRIGGER));
        ret = FIELD_DP32(ret, PCSR_STATUS, BISR_DONE,
                         FIELD_EX32(s->pcsr_control, PCSR_CONTROL,
                                    BISR_TRIGGER));
        ret = FIELD_DP32(ret, PCSR_STATUS, BISR_PASS,
                         FIELD_EX32(s->pcsr_control, PCSR_CONTROL,
                                    BISR_TRIGGER));
        break;
    case A_PCSR_LOCK:
        ret = s->lock;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AMD_CPM6_GTMPW_NPI ": read to unimplemented register"
                      " at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_amd_cpm6_gtmpw_npi_read(addr, ret, size);
    return ret;
}

static MemTxResult amd_cpm6_gtmpw_npi_write(void *opaque, hwaddr addr,
                                            uint64_t value, unsigned int size,
                                            MemTxAttrs attrs)
{
    AmdCpm6GtmpwNpiState *s = AMD_CPM6_GTMPW_NPI(opaque);

    trace_amd_cpm6_gtmpw_npi_write(addr, value, size);

    if (addr != A_PCSR_LOCK && s->lock) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AMD_CPM6_GTMPW_NPI ": write at 0x%" HWADDR_PRIx
                      " while locked\n", addr);
        return MEMTX_ACCESS_ERROR;
    }

    switch (addr) {
    case A_PCSR_CONTROL:
        s->pcsr_control = value & s->pcsr_mask;
        break;
    case A_PCSR_MASK:
        s->pcsr_mask = value;
        break;
    case A_PCSR_LOCK:
        s->lock = (value != GTMPW_LOCK_MAGIC);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AMD_CPM6_GTMPW_NPI ": write to unimplemented "
                      "register at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return MEMTX_OK;
}


static const MemoryRegionOps amd_cpm6_gtmpw_npi_ops = {
    .read = amd_cpm6_gtmpw_npi_read,
    .write_with_attrs = amd_cpm6_gtmpw_npi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void amd_cpm6_gtmpw_npi_reset_enter(Object *obj, ResetType type)
{
    AmdCpm6GtmpwNpiState *s = AMD_CPM6_GTMPW_NPI(obj);

    s->pcsr_control = 0x1000142;
    s->pcsr_mask = 0x0;
    s->lock = true;
}

static void amd_cpm6_gtmpw_npi_reset_hold(Object *obj, ResetType type)
{
}

static void amd_cpm6_gtmpw_npi_realize(DeviceState *dev, Error **errp)
{
    AmdCpm6GtmpwNpiState *s = AMD_CPM6_GTMPW_NPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &amd_cpm6_gtmpw_npi_ops,
                          s, TYPE_AMD_CPM6_GTMPW_NPI,
                          AMD_CPM6_GTMPW_NPI_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void amd_cpm6_gtmpw_npi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = amd_cpm6_gtmpw_npi_realize;
    rc->phases.enter = amd_cpm6_gtmpw_npi_reset_enter;
    rc->phases.hold = amd_cpm6_gtmpw_npi_reset_hold;
}

static const TypeInfo amd_cpm6_gtmpw_npi_info = {
    .name = TYPE_AMD_CPM6_GTMPW_NPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AmdCpm6GtmpwNpiState),
    .class_init = amd_cpm6_gtmpw_npi_class_init,
};

static void amd_cpm6_gtmpw_npi_register_types(void)
{
    type_register_static(&amd_cpm6_gtmpw_npi_info);
}

type_init(amd_cpm6_gtmpw_npi_register_types)
