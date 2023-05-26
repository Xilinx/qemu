/*
 * QEMU model of Xilinx PMX eFuse device cache MMIO
 * XLNX_PMX_EFUSE_CACHE.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/nvram/xlnx-pmx-efuse.h"

#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"

#define MR_SIZE (3 * 256 * 4)

static uint32_t pmx_efuse_cache_u32(XlnxPmxEFuseCache *s, unsigned bit)
{
    return xlnx_pmx_efuse_read_row(s->efuse, bit, NULL);
}

static uint64_t pmx_efuse_cache_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxPmxEFuseCache *s = XLNX_PMX_EFUSE_CACHE(opaque);
    unsigned int w0 = QEMU_ALIGN_DOWN(addr * 8, 32);
    unsigned int w1 = QEMU_ALIGN_DOWN((addr + size - 1) * 8, 32);

    uint64_t u64 = 0;

    assert(w0 == w1 || (w0 + 32) == w1);

    u64 = pmx_efuse_cache_u32(s, w1);
    if (w0 < w1) {
        u64 <<= 32;
        u64 |= pmx_efuse_cache_u32(s, w0);
    }

    /* If 'addr' unaligned, the guest is always assumed to be little-endian. */
    addr &= 3;
    if (addr) {
        u64 >>= 8 * addr;
    }

    return u64 & MAKE_64BIT_MASK(0, (8 * size));
}

static void pmx_efuse_cache_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    XlnxPmxEFuseCache *s = XLNX_PMX_EFUSE_CACHE(opaque);
    g_autofree char *path = object_get_canonical_path(OBJECT(s));

    /* No Register Writes allowed */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: efuse cache registers are read-only",
                  path);
}

static const MemoryRegionOps pmx_efuse_cache_ops = {
    .read = pmx_efuse_cache_read,
    .write = pmx_efuse_cache_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pmx_efuse_cache_realize(DeviceState *dev, Error **errp)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(dev);
    g_autofree char *path = object_get_canonical_path(OBJECT(s));

    if (!s->efuse) {
        error_setg(errp, "%s: XLN-EFUSE not connected", path);
        return;
    }
}

static void pmx_efuse_cache_sysmon_data_source(Object *obj,
                                               XlnxEFuseSysmonData *data)
{
    XlnxPmxEFuseCache *s = XLNX_PMX_EFUSE_CACHE(obj);

    if (!xlnx_efuse_get_sysmon(s->efuse, data) && data) {
        memset(data, 0, sizeof(*data));
    }
}

static void pmx_efuse_cache_init(Object *obj)
{
    XlnxPmxEFuseCache *s = XLNX_PMX_EFUSE_CACHE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &pmx_efuse_cache_ops, s,
                          TYPE_XLNX_PMX_EFUSE_CACHE, MR_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static Property pmx_efuse_cache_props[] = {
    DEFINE_PROP_LINK("efuse",
                     XlnxPmxEFuseCache, efuse,
                     TYPE_XLNX_EFUSE, XlnxEFuse *),

    DEFINE_PROP_END_OF_LIST(),
};

static void pmx_efuse_cache_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XlnxEFuseSysmonDataSourceClass *esdc;

    dc->realize = pmx_efuse_cache_realize;
    device_class_set_props(dc, pmx_efuse_cache_props);

    esdc = XLNX_EFUSE_SYSMON_DATA_SOURCE_CLASS(klass);
    esdc->get_data = pmx_efuse_cache_sysmon_data_source;
}

static const TypeInfo pmx_efuse_cache_info = {
    .name          = TYPE_XLNX_PMX_EFUSE_CACHE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxPmxEFuseCache),
    .class_init    = pmx_efuse_cache_class_init,
    .instance_init = pmx_efuse_cache_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_XLNX_EFUSE_SYSMON_DATA_SOURCE },
        { }
    }
};

static void pmx_efuse_cache_register_types(void)
{
    type_register_static(&pmx_efuse_cache_info);
}

type_init(pmx_efuse_cache_register_types)
