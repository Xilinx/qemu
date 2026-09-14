/*
 * This block implements the standard PCSR interface for Reset/Initialization of
 * CPM
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "hw/misc/amd-cpm6-pcsr.h"
#include "trace.h"

REG32(MASK, 0x0)
REG32(PCR, 0x4)
    FIELD(PCR, SCAN_CLEAR_TRIGGER, 11, 1)

REG32(PSR, 0x8)
    FIELD(PSR, PCSRLOCK, 0, 1)
    FIELD(PSR, SCAN_CLEAR_DONE, 1, 1)
    FIELD(PSR, SCAN_CLEAR_PASS, 2, 1)
    FIELD(PSR, CALDONE, 4, 1)

REG32(LOCK, 0xc)
    FIELD(LOCK, LOCK, 0, 32)

#define PCSR_MAGIC_VALUE 0xf9e8d7c6

static uint64_t amd_cpm6_pcsr_read(void *opaque, hwaddr addr, unsigned int size)
{
    AmdCpm6PcsrState *s = AMD_CPM6_PCSR(opaque);
    uint64_t ret;

    switch (addr) {
    case A_MASK:
        ret = s->mask;
        break;
    case A_PCR:
        ret = s->pcr;
        break;
    case A_PSR:
        ret = FIELD_DP32(0, PSR, PCSRLOCK, s->lock);
        ret = FIELD_DP32(ret, PSR, SCAN_CLEAR_PASS,
                         FIELD_EX32(s->pcr, PCR, SCAN_CLEAR_TRIGGER));
        ret = FIELD_DP32(ret, PSR, SCAN_CLEAR_DONE,
                         FIELD_EX32(s->pcr, PCR, SCAN_CLEAR_TRIGGER));
        ret = FIELD_DP32(ret, PSR, CALDONE, true);
        break;
    case A_LOCK:
        ret = s->lock;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AMD_CPM6_PCSR ": read to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_amd_cpm6_pcsr_read(addr, ret, size);
    return ret;
}

static void amd_cpm6_pcsr_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    AmdCpm6PcsrState *s = AMD_CPM6_PCSR(opaque);

    trace_amd_cpm6_pcsr_write(addr, value, size);

    if ((addr != A_LOCK) && s->lock) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AMD_CPM6_PCSR ": write at 0x%" HWADDR_PRIx
                      " while locked\n", addr);
        return;
    }

    switch (addr) {
    case A_MASK:
        s->mask = value;
        break;
    case A_PCR:
        s->pcr = value & s->mask;
        break;
    case A_LOCK:
        s->lock = (value != PCSR_MAGIC_VALUE);
        break;

    case A_PSR:
        /* read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AMD_CPM6_PCSR ": write to read-only register"
                      " at offset 0x%" HWADDR_PRIx "\n",
                      addr);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AMD_CPM6_PCSR ": write to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}


static const MemoryRegionOps amd_cpm6_pcsr_ops = {
    .read = amd_cpm6_pcsr_read,
    .write = amd_cpm6_pcsr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void amd_cpm6_pcsr_reset_enter(Object *obj, ResetType type)
{
    AmdCpm6PcsrState *s = AMD_CPM6_PCSR(obj);

    s->mask = 0x1fe;
    s->pcr = 0x1fe;
    s->lock = 0x1;
}

static void amd_cpm6_pcsr_reset_hold(Object *obj, ResetType type)
{
}

static void amd_cpm6_pcsr_realize(DeviceState *dev, Error **errp)
{
    AmdCpm6PcsrState *s = AMD_CPM6_PCSR(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &amd_cpm6_pcsr_ops,
                          s, TYPE_AMD_CPM6_PCSR,
                          AMD_CPM6_PCSR_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void amd_cpm6_pcsr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = amd_cpm6_pcsr_realize;
    rc->phases.enter = amd_cpm6_pcsr_reset_enter;
    rc->phases.hold = amd_cpm6_pcsr_reset_hold;
}

static const TypeInfo amd_cpm6_pcsr_info = {
    .name = TYPE_AMD_CPM6_PCSR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AmdCpm6PcsrState),
    .class_init = amd_cpm6_pcsr_class_init,
};

static void amd_cpm6_pcsr_register_types(void)
{
    type_register_static(&amd_cpm6_pcsr_info);
}

type_init(amd_cpm6_pcsr_register_types)
