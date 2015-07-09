/*
 * generic blob loader
 *
 * Copyright (C) 2014 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "sysemu/sysemu.h"
#include "config.h"
#include "hw/sysbus.h"
#include "hw/devices.h"
#include "hw/loader.h"
#include "hw/misc/blob-loader.h"
#include "qemu/error-report.h"
#include "sysemu/dma.h"
#include "hw/loader.h"
#include "elf.h"

#define BLOB_LOADER_CPU_NONE 0xff

static Property blob_loader_props[] = {
    DEFINE_PROP_UINT64("addr", BlobLoaderState, addr, 0),
    DEFINE_PROP_UINT64("data", BlobLoaderState, data, 0),
    DEFINE_PROP_UINT8("data-len", BlobLoaderState, data_len, 0),
    DEFINE_PROP_UINT8("cpu", BlobLoaderState, cpu_nr, BLOB_LOADER_CPU_NONE),
    DEFINE_PROP_BOOL("force-raw", BlobLoaderState, force_raw, false),
    DEFINE_PROP_STRING("file", BlobLoaderState, file),
    DEFINE_PROP_END_OF_LIST(),
};

extern AddressSpace *loader_as;

static void blob_loader_realize(DeviceState *dev, Error **errp)
{
    BlobLoaderState *s = BLOB_LOADER(dev);
    int size;
    hwaddr entry;

    if (s->cpu_nr != BLOB_LOADER_CPU_NONE) {
        CPUState *cs = first_cpu;
        int cpu_nr = s->cpu_nr;

        while (cs && cpu_nr) {
            cs = CPU_NEXT(cs);
            cpu_nr--;
        }
        if (!cs) {
            error_setg(errp, "Specified boot cpu #%d is non existant",
                       s->cpu_nr);
            return;
        }
        s->cpu = cs;
    }

    if (s->cpu) {
        loader_as = s->cpu->as;
    }

    if (s->file) {
        /* FIXME: Make work on BE */
        if (s->force_raw) {
            goto do_raw;
        }
        size = load_elf(s->file, NULL, NULL, &entry, NULL, NULL, 0,
                        ELF_MACHINE, 0);
        /* If it wasn't an ELF image, try an u-boot image. */
        if (size < 0) {
            size = load_uimage(s->file, &entry, NULL, NULL, NULL, NULL);
        }
        /* Not an ELF image nor a u-boot image, try a RAW image. */
do_raw:
        if (size < 0) {
            /* FIXME: sanitize size limit */
            size = load_image_targphys(s->file, s->addr, 0);
        } else {
            s->addr = entry;
        }
        if (size < 0) {
            error_setg(errp, "Cannot load specified image %s", s->file);
            return;
        }
    }

    loader_as = NULL;

}

static void blob_loader_reset(DeviceState *dev)
{
    BlobLoaderState *s = BLOB_LOADER(dev);

    if (s->cpu) {
        CPUClass *cc = CPU_GET_CLASS(s->cpu);
        cpu_reset(s->cpu);
        cc->set_pc(s->cpu, s->addr);
        cpu_halt_reset_common(s->cpu, NULL, false, true);
    }
    /* Probably only works on LE */
    if (s->data_len) {
        dma_memory_write((s->cpu ? s->cpu : first_cpu)->as, s->addr, &s->data,
                         s->data_len);
    }
}

static void blob_loader_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = blob_loader_reset;
    dc->realize = blob_loader_realize;
    dc->props = blob_loader_props;
    dc->desc = "blob loader";
}

static TypeInfo blob_loader_info = {
    .name = TYPE_BLOB_LOADER,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(BlobLoaderState),
    .class_init = blob_loader_class_init,
};

static void blob_loader_register_type(void)
{
    type_register_static(&blob_loader_info);
}

type_init(blob_loader_register_type)
