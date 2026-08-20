/*
 * AMD DDR Memory Device
 *
 * Copyright (c) Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/misc/amd-ddr-memory.h"

static void amd_ddr_memory_realize(DeviceState *dev, Error **errp)
{
    AMDDDRMemory *s = AMD_DDR_MEMORY(dev);
    g_autofree char *name = NULL;
#ifdef CONFIG_POSIX
    g_autoptr(GString) filename = NULL;
#endif

    if (s->size == 0) {
        error_setg(errp, "ddr-memory: size must be non-zero");
        return;
    }

    if (s->shared == ON_OFF_AUTO_AUTO) {
#ifdef CONFIG_POSIX
        s->shared = machine_path ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
#else
        s->shared = ON_OFF_AUTO_OFF;
#endif
    }

    name = g_strdup_printf("_ddr@0x%" PRIx64, (uint64_t)s->address);

    switch (s->shared) {
    case ON_OFF_AUTO_OFF:
        memory_region_init_ram(&s->mr, OBJECT(dev), name, s->size, errp);
        break;

    case ON_OFF_AUTO_ON:
#ifdef CONFIG_POSIX
        filename = g_string_new("");

        g_string_append_printf(filename, "%s%cqemu-memory-%s",
                               machine_path ?: ".", G_DIR_SEPARATOR, name);
        memory_region_init_ram_from_file(&s->mr, OBJECT(dev), name, s->size, 0,
                                         RAM_SHARED, filename->str, 0, errp);
#else
        error_setg(errp,
                   "ddr-memory: shared memory backend is not supported on this host");
#endif
        break;

    default:
        g_assert_not_reached();
    }
}

static const Property amd_ddr_memory_props[] = {
    DEFINE_PROP_UINT64("address", AMDDDRMemory, address, 0),
    DEFINE_PROP_UINT64("size", AMDDDRMemory, size, 0),
    DEFINE_PROP_ON_OFF_AUTO("shared", AMDDDRMemory, shared, ON_OFF_AUTO_AUTO),
};

static void amd_ddr_memory_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = amd_ddr_memory_realize;
    device_class_set_props(dc, amd_ddr_memory_props);
}

static const TypeInfo amd_ddr_memory_info = {
    .parent             = TYPE_DEVICE,
    .name               = TYPE_AMD_DDR_MEMORY,
    .instance_size      = sizeof(AMDDDRMemory),
    .class_init         = amd_ddr_memory_class_init,
};

static void ddr_register_types(void)
{
    type_register_static(&amd_ddr_memory_info);
}

type_init(ddr_register_types)
