/*
 * Includes Interrupt,error and Isolation registers
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/registerfields.h"
#include "hw/misc/amd-cpm6-int-wrap-top-csr.h"
#include "trace.h"

REG32(ECO_0, 0x78)
REG32(ECO_1, 0x7c)

REG32(SCRATCH_0, 0x200)
/* ... */
REG32(SCRATCH_9, 0x224)

static uint64_t cpm6_int_wrap_top_csr_read(void *opaque, hwaddr addr,
                                           unsigned int size)
{
    Cpm6IntWrapTopCsrState *s = CPM6_INT_WRAP_TOP_CSR(opaque);
    size_t offset;
    uint64_t ret;

    switch (addr) {
    case A_ECO_0:
        ret = s->eco_0;
        break;
    case A_ECO_1:
        ret = s->eco_1;
        break;
    case A_SCRATCH_0 ... A_SCRATCH_9:
        offset = (addr - A_SCRATCH_0) / sizeof(uint32_t);
        ret = s->scratch[offset];
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_CPM6_INT_WRAP_TOP_CSR ": read to unimplemented "
                      "register at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_cpm6_int_wrap_top_csr_read(addr, ret, size);
    return ret;
}

static void cpm6_int_wrap_top_csr_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned int size)
{
    Cpm6IntWrapTopCsrState *s = CPM6_INT_WRAP_TOP_CSR(opaque);
    size_t offset;

    trace_cpm6_int_wrap_top_csr_write(addr, value, size);

    switch (addr) {
    case A_ECO_0:
        s->eco_0 = value;
        break;
    case A_ECO_1:
        s->eco_1 = value;
        break;
    case A_SCRATCH_0 ... A_SCRATCH_9:
        offset = (addr - A_SCRATCH_0) / sizeof(uint32_t);
        s->scratch[offset] = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_CPM6_INT_WRAP_TOP_CSR ": write to unimplemented "
                      "register at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}


static const MemoryRegionOps cpm6_int_wrap_top_csr_ops = {
    .read = cpm6_int_wrap_top_csr_read,
    .write = cpm6_int_wrap_top_csr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void cpm6_int_wrap_top_csr_reset_enter(Object *obj, ResetType type)
{
    Cpm6IntWrapTopCsrState *s = CPM6_INT_WRAP_TOP_CSR(obj);

    s->eco_0 = 0x0;
    s->eco_1 = 0x0;
    memset(s->scratch, 0, sizeof(s->scratch));
}

static void cpm6_int_wrap_top_csr_realize(DeviceState *dev, Error **errp)
{
    Cpm6IntWrapTopCsrState *s = CPM6_INT_WRAP_TOP_CSR(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &cpm6_int_wrap_top_csr_ops,
                          s, TYPE_CPM6_INT_WRAP_TOP_CSR,
                          CPM6_INT_WRAP_TOP_CSR_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void cpm6_int_wrap_top_csr_class_init(ObjectClass *klass,
                                             const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = cpm6_int_wrap_top_csr_realize;
    rc->phases.enter = cpm6_int_wrap_top_csr_reset_enter;
}

static const TypeInfo cpm6_int_wrap_top_csr_info = {
    .name = TYPE_CPM6_INT_WRAP_TOP_CSR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Cpm6IntWrapTopCsrState),
    .class_init = cpm6_int_wrap_top_csr_class_init,
};

static void cpm6_int_wrap_top_csr_register_types(void)
{
    type_register_static(&cpm6_int_wrap_top_csr_info);
}

type_init(cpm6_int_wrap_top_csr_register_types)
