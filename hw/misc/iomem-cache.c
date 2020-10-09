/*
 * Copyright (c) 2020 Xilinx Inc.
 *
 * Written by Francisco Iglesias <francisco.iglesias@xilinx.com>
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
#include "qemu/main-loop.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/misc/iomem-cache.h"
#include "hw/core/cpu.h"
#include "qemu/option_int.h"
#include "hw/fdt_generic_util.h"

#define N_CACHE_SZ 2
#define MAX_UNCACHED_ACCESS_SIZE 4096

enum {
    CACHED_INDEX,
    NUM_INDEX
};

static CacheLine *iommem_cache_alloc_line(IOMemCache *s, hwaddr tag)
{
    int line_idx = 0;

    for (; line_idx < s->cfg.num_lines; line_idx++) {
        if (s->cache.line[line_idx].valid == false) {
            break;
        }
    }

    if (line_idx < s->cfg.num_lines) {
        CacheLine *l = g_new0(CacheLine, 1);
        hwaddr ram_offset = line_idx * s->cfg.line_size;

        /* Line has been taken */
        s->cache.line[line_idx].valid = true;

        l->line_idx = line_idx;
        l->iotlb = (IOMMUTLBEntry) {
            .target_as = &s->as_ram,
            .iova = tag,
            .translated_addr = ram_offset,
            .addr_mask = (s->cfg.line_size - 1),
            .perm = IOMMU_RW,
        };
        l->data = &s->ram_ptr[line_idx * s->cfg.line_size];

        s->cache.num_allocated++;

        if (s->cache.num_allocated > s->cache.max_allocated) {
            s->cache.max_allocated = s->cache.num_allocated;
        }

        return l;
    }

    g_assert_not_reached();
    return NULL;
}

static IOMMUTLBEntry iomem_cache_load_line(IOMemCache *s, hwaddr addr,
                                           int cpu_idx)
{
    hwaddr tag = addr & ~(s->cfg.line_size - 1);
    CacheLine *cpu_l = g_new0(CacheLine, 1);
    CacheLine *l = g_hash_table_lookup(s->cache.table, (gpointer) tag);

    if (!l) {
        bool is_write = false;

        l = iommem_cache_alloc_line(s, tag);

        address_space_rw(&s->down_as, tag,
                         MEMTXATTRS_UNSPECIFIED, l->data, s->cfg.line_size,
                         is_write);

        l->valid = true;
        g_hash_table_insert(s->cache.table, (gpointer) tag, l);
    }

    assert(l);

    /*
     * Insert into the cpu cache table for tracking the lines the cpu has
     * allocated.
     */
    if (g_hash_table_lookup(s->cpu_cache[cpu_idx].table,
                            (gpointer) tag) == NULL) {
        *cpu_l = *l;
        g_hash_table_insert(s->cpu_cache[cpu_idx].table, (gpointer) tag, cpu_l);
    }

    return l->iotlb;
}

static void iomem_cache_writeback_line(IOMemCache *s, CacheLine *l)
{
    bool is_write = true;

    assert(s->cache.line[l->line_idx].valid);

    address_space_rw(&s->down_as, l->iotlb.iova,
                     MEMTXATTRS_UNSPECIFIED, l->data, s->cfg.line_size,
                     is_write);

    s->cache.line[l->line_idx].valid = false;
    assert(s->cache.num_allocated > 0);
    s->cache.num_allocated--;
}

static bool in_cpu_cache(IOMemCache *s, hwaddr tag)
{
    for (int i = 0; s->cpu_cache[i].table; i++) {
        if (g_hash_table_lookup(s->cpu_cache[i].table, (gpointer) tag)) {
            return true;
        }
    }
    return false;
}

static void iomem_cache_flush(CPUState *cpu, run_on_cpu_data d)
{
    IOMemCache *s = (IOMemCache *) d.host_ptr;
    int num_lines;

    qemu_mutex_lock(&s->mutex);

    num_lines = g_hash_table_size(s->cache.table);

    if (num_lines > (s->cfg.num_lines / N_CACHE_SZ)) {
        int cpu_idx = cpu->cpu_index;
        GHashTableIter iter;
        CacheLine *l;

        if (g_hash_table_size(s->cpu_cache[cpu_idx].table) > 0) {
            tlb_flush(cpu);
            g_hash_table_remove_all(s->cpu_cache[cpu_idx].table);
        }

        g_hash_table_iter_init(&iter, s->cache.table);

        while (g_hash_table_iter_next(&iter, NULL, (void **)&l)) {
            hwaddr tag = l->iotlb.iova;

            /* Writeback if none of the cpus has the cache line */
            if (!in_cpu_cache(s, tag)) {
                iomem_cache_writeback_line(s, l);

                /* Calls g_free on the l */
                g_hash_table_iter_remove(&iter);

                if (g_hash_table_size(s->cache.table) <=
                    ((s->cfg.num_lines / N_CACHE_SZ) / 2)) {
                    break;
                }
            }
        }
    }

    qemu_mutex_unlock(&s->mutex);
}

static void iomem_cache_maintenance(IOMemCache *s)
{
    int num_lines = g_hash_table_size(s->cache.table);

    if (num_lines > (s->cfg.num_lines / N_CACHE_SZ)) {
        CPUState *tmp_cpu;

        CPU_FOREACH(tmp_cpu) {
            async_safe_run_on_cpu(tmp_cpu,
                                  iomem_cache_flush,
                                  RUN_ON_CPU_HOST_PTR(s));
        }
    }
}

static IOMMUTLBEntry iomem_cache_translate(IOMMUMemoryRegion *iommu,
                                           hwaddr addr,
                                           IOMMUAccessFlags flags,
                                           int iommu_idx)
{
    IOMemCacheRegion *region = container_of(iommu, IOMemCacheRegion, iommu);
    IOMemCache *s = region->parent;
    IOMMUTLBEntry ret;
    bool locked;
    int cpu_idx;

    locked = qemu_mutex_iothread_locked();

    if (locked) {
        qemu_mutex_unlock_iothread();
    }

    qemu_mutex_lock(&s->mutex);

    /* Cached */
    iomem_cache_maintenance(s);

    cpu_idx = (current_cpu) ? current_cpu->cpu_index : 0;

    /* Use the absolut address with the cache */
    addr += region->offset;
    ret = iomem_cache_load_line(s, addr, cpu_idx);

    qemu_mutex_unlock(&s->mutex);

    if (locked) {
        qemu_mutex_lock_iothread();
    }
    return ret;
}

static uint64_t iomem_cache_get_min_page_size(IOMMUMemoryRegion *iommu)
{
    IOMemCacheRegion *region = container_of(iommu, IOMemCacheRegion, iommu);
    IOMemCache *s = region->parent;

    return s->cfg.line_size;
}

static int iomem_cache_attrs_to_index(IOMMUMemoryRegion *iommu,
                                      MemTxAttrs attrs)
{
    return CACHED_INDEX;
}

static int iomem_cache_num_indexes(IOMMUMemoryRegion *iommu)
{
    return NUM_INDEX;
}

static bool iomem_cache_enable(void)
{
    QemuOpts *opts = qemu_find_opts_singleton("memory");
    char *memstr = NULL;
    QemuOpt *opt;

    QTAILQ_FOREACH(opt, &opts->head, next) {
        if (strcmp(opt->name, "size") != 0) {
            continue;
        }
        memstr = opt->str;
        break;
    }

    if (memstr && strlen(memstr) == 1 && memstr[0] == '0') {
        return true;
    }

    return false;
}

static void iomem_cache_realize(DeviceState *dev, Error **errp)
{
    IOMemCache *s = IOMEM_CACHE(dev);
    CPUState *cpu;

    if (s->cfg.line_size == 0) {
        error_setg(errp, "line_size must be greater than 0");
        return;
    }

    if (s->down_mr == NULL) {
        error_setg(errp, "No memory region <mr> specified");
        return;
    }

    s->cfg.num_lines = (s->cfg.cache_size * N_CACHE_SZ) / s->cfg.line_size;

    s->ram_ptr = g_malloc(s->cfg.cache_size * N_CACHE_SZ);

    memory_region_init_ram_ptr(&s->mr_ram, OBJECT(s),
                               "iomem-cache-mr-ram",
                               s->cfg.cache_size * N_CACHE_SZ,
                               s->ram_ptr);

    address_space_init(&s->as_ram, &s->mr_ram, "mr_ram");

    address_space_init(&s->down_as, s->down_mr, "iomem-cache-dma");

    s->cache.line = g_new0(CacheLine, s->cfg.num_lines);
    s->cache.table = g_hash_table_new_full(NULL, NULL, NULL, g_free);

    CPU_FOREACH(cpu) {
        int cpu_idx = cpu->cpu_index;

        s->cpu_cache[cpu_idx].table = g_hash_table_new_full(NULL, NULL,
                                                            NULL, g_free);
    }

    qemu_mutex_init(&s->mutex);
}

static void iomem_cache_iommu_memory_region_class_init(ObjectClass *klass,
                                                       void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = iomem_cache_translate;
    imrc->attrs_to_index = iomem_cache_attrs_to_index;
    imrc->get_min_page_size = iomem_cache_get_min_page_size;
    imrc->num_indexes = iomem_cache_num_indexes;
}

static bool iomem_cache_parse_reg(FDTGenericMMap *obj,
                                  FDTGenericRegPropInfo reg, Error **errp)
{
    IOMemCache *s = IOMEM_CACHE(obj);
    FDTGenericMMapClass *parent_fmc =
        FDT_GENERIC_MMAP_CLASS(IOMEM_CACHE_PARENT_CLASS);
    bool enable_cache = iomem_cache_enable();
    int i;

    s->region = g_new0(IOMemCacheRegion, reg.n);

    for (i = 0; i < reg.n; ++i) {
        char *name = g_strdup_printf("iomem-cache-iommu-%d", i);

        memory_region_init_iommu(&s->region[i].iommu,
                                 sizeof(s->region[i].iommu),
                                 TYPE_IOMEM_CACHE_IOMMU,
                                 OBJECT(s),
                                 name,
                                 reg.s[i]);

        memory_region_set_enabled(MEMORY_REGION(&s->region[i].iommu),
                                  enable_cache);

        sysbus_init_mmio(SYS_BUS_DEVICE(obj),
                         MEMORY_REGION(&s->region[i].iommu));

        s->region[i].offset = reg.a[i];
        s->region[i].parent = s;

        g_free(name);
    }

    return parent_fmc ? parent_fmc->parse_reg(obj, reg, errp) : false;
}

static const TypeInfo iomem_cache_iommu_memory_region_info = {
    .name = TYPE_IOMEM_CACHE_IOMMU,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = iomem_cache_iommu_memory_region_class_init,
};

static Property iomem_cache_properties[] = {
    DEFINE_PROP_UINT32("cache-size", IOMemCache, cfg.cache_size, 32 * MiB),
    DEFINE_PROP_UINT32("line-size", IOMemCache, cfg.line_size, 1024),
    DEFINE_PROP_LINK("downstream-mr", IOMemCache, down_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void iomem_class_init(ObjectClass *klass, void * data)
{
    FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, iomem_cache_properties);
    dc->realize = iomem_cache_realize;
    fmc->parse_reg = iomem_cache_parse_reg;
}

static const TypeInfo iomem_cache_info = {
    .parent = TYPE_SYS_BUS_DEVICE,
    .name = TYPE_IOMEM_CACHE,
    .instance_size = sizeof(IOMemCache),
    .class_init = iomem_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_MMAP },
        { },
    },
};

static void memory_register_types(void)
{
    type_register_static(&iomem_cache_info);
    type_register_static(&iomem_cache_iommu_memory_region_info);
}

type_init(memory_register_types)
