/*
 * Execution trace packager.
 * Copyright (c) 2013 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ETRACE_H__
#define __ETRACE_H__

#include <stdio.h>
#include <stdbool.h>

struct etrace_entry32 {
    uint32_t duration;
    uint32_t start, end;
};

struct etrace_entry64 {
    uint32_t duration;
    uint64_t start, end;
};

enum qemu_etrace_flag {
    ETRACE_F_NONE        = 0,
    ETRACE_F_EXEC        = (1 << 0),
    ETRACE_F_TRANSLATION = (1 << 1),
    ETRACE_F_MEM         = (1 << 2),
    ETRACE_F_CPU         = (1 << 3),
    ETRACE_F_GPIO         = (1 << 4),
};

enum qemu_etrace_event_u64_flag {
    ETRACE_EVU64_F_NONE        = 0,
    ETRACE_EVU64_F_PREV_VAL    = (1 << 0),
};

enum etrace_mem_attr {
    MEM_READ    = (0 << 0),
    MEM_WRITE   = (1 << 0),
};

struct etracer {
    const char *filename;
    FILE *fp;
    unsigned int arch_bits;
    uint64_t flags;

    /* FIXME: Removeme.  */
    unsigned int current_unit_id;

#define EXEC_CACHE_SIZE (16 * 1024)
    uint64_t exec_start;
    bool exec_start_valid;
    int64_t exec_start_time;
    struct {
        union {
            struct etrace_entry64 t64[EXEC_CACHE_SIZE];
            struct etrace_entry32 t32[2 * EXEC_CACHE_SIZE];
        };
        uint64_t start_time;
        unsigned int pos;
        unsigned int unit_id;
    } exec_cache;
};

bool etrace_init(struct etracer *t, const char *filename,
                 const char *opts,
                 unsigned int arch_id, unsigned int arch_bits);
void etrace_close(struct etracer *t);
void etrace_dump_exec(struct etracer *t, unsigned int unit_id,
                      uint64_t start, uint64_t end,
                      uint64_t start_time, uint32_t duration);

/* Helpers.  */
void etrace_dump_exec_start(struct etracer *t,
                            unsigned int unit_id,
                            uint64_t start);

void etrace_dump_exec_end(struct etracer *t,
                          unsigned int unit_id,
                          uint64_t end);

void etrace_mem_access(struct etracer *t, uint16_t unit_id,
                       uint64_t guest_vaddr, uint64_t guest_paddr,
                       size_t size, uint64_t attr, uint64_t val);

void etrace_dump_tb(struct etracer *t, AddressSpace *as, uint16_t unit_id,
                    uint64_t guest_vaddr, uint64_t guest_paddr,
                    size_t guest_len,
                    void *host_buf, size_t host_len);

void etrace_note_write(struct etracer *t, unsigned int unit_id,
                       void *buf, size_t len);

/* fp should point to an etracer object.  */
int etrace_note_fprintf(FILE *fp,
                        const char *fmt, ...);

void etrace_event_u64(struct etracer *t, uint16_t unit_id,
                      uint32_t flags,
                      const char *dev_name,
                      const char *event_name,
                      uint64_t val, uint64_t prev_val);

/* QEMU helpers to simplify integration into qemu.  */
extern const char *qemu_arg_etrace;
extern const char *qemu_arg_etrace_flags;
extern struct etracer qemu_etracer;
extern bool qemu_etrace_enabled;
void qemu_etrace_cleanup(void);

static inline bool qemu_etrace_mask(uint64_t mask)
{
    if (qemu_etrace_enabled
        && (qemu_etracer.flags & mask)) {
        return true;
    }
    return false;
}

void qemu_etrace_gpio_init(void);
#endif
