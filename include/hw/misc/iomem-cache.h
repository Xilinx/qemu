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
#ifndef XLNX_IOMEM_CACHE_H
#define XLNX_IOMEM_CACHE_H

#include "hw/sysbus.h"
#include "hw/core/cpu.h"
#include "hw/ptimer.h"

#define TYPE_IOMEM_CACHE "iomem-cache"
#define TYPE_IOMEM_CACHE_IOMMU \
        "iomem-cache-iommu-memory-region"

#define IOMEM_CACHE(obj) \
        OBJECT_CHECK(IOMemCache, (obj), TYPE_IOMEM_CACHE)
#define IOMEM_CACHE_PARENT_CLASS \
    object_class_get_parent(object_class_by_name(TYPE_IOMEM_CACHE))

#define MAX_CPU_INDEX 20

typedef struct CacheLine {
    bool valid;
    IOMMUTLBEntry iotlb;
    int line_idx;
    uint8_t *data;
} CacheLine;

typedef struct IOMemCacheWrBuf {
    uint8_t data[8 * KiB];
    hwaddr start;
    hwaddr len;
    hwaddr max_len;
    ptimer_state *timer;
    QemuMutex mutex;
} IOMemCacheWrBuf;

typedef struct IOMemCache IOMemCache;

typedef struct IOMemCacheRegion {
    IOMemCache *parent;

    IOMMUMemoryRegion iommu;

    uint64_t offset;
} IOMemCacheRegion;

typedef struct IOMemCache {
    SysBusDevice parent_obj;

    IOMemCacheRegion *region;

    /* RAM address space and memory region (for cached acceses) */
    AddressSpace as_ram;
    MemoryRegion mr_ram;
    uint8_t *ram_ptr;

    /* Uncached address space and memory region */
    AddressSpace down_as_uncached;
    MemoryRegion down_mr_uncached;

    /* DMA address space and memory region */
    AddressSpace down_as;
    MemoryRegion *down_mr;

    /* Cache */
    struct {
        CacheLine *line;

        /* Track allocated */
        GHashTable *table;

        uint32_t num_lines;
        uint32_t line_size;

        uint32_t num_allocated;
        uint32_t max_allocated;
    } cache;

    /* Per CPU cache line tracking */
    struct {
        GHashTable *table;
    } cpu_cache[MAX_CPU_INDEX];

    QemuMutex mutex;

    /* Write buffer */
    IOMemCacheWrBuf wbuf;

    struct {
        uint32_t cache_size;
        uint32_t line_size;

        /* If set all memory will be treated as cacheable. */
        bool cache_all;
    } cfg;

} IOMemCache;

void cpu_clean_inv_one(CPUState *cpu, run_on_cpu_data d);
void tlb_flush(CPUState *cpu);

#endif
