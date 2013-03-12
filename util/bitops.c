/*
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Copyright (C) 2008 IBM Corporation
 * Written by Rusty Russell <rusty@rustcorp.com.au>
 * (Inspired by David Howell's find_next_bit implementation)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "qemu-common.h"
#include "qemu/log.h"
#include "qemu/bitops.h"

#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
    const unsigned long *p = addr + BITOP_WORD(offset);
    unsigned long result = offset & ~(BITS_PER_LONG-1);
    unsigned long tmp;

    if (offset >= size) {
        return size;
    }
    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp &= (~0UL << offset);
        if (size < BITS_PER_LONG) {
            goto found_first;
        }
        if (tmp) {
            goto found_middle;
        }
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size & ~(BITS_PER_LONG-1)) {
        if ((tmp = *(p++))) {
            goto found_middle;
        }
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size) {
        return result;
    }
    tmp = *p;

found_first:
    tmp &= (~0UL >> (BITS_PER_LONG - size));
    if (tmp == 0UL) {		/* Are any bits set? */
        return result + size;	/* Nope. */
    }
found_middle:
    return result + ctzl(tmp);
}

/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
    const unsigned long *p = addr + BITOP_WORD(offset);
    unsigned long result = offset & ~(BITS_PER_LONG-1);
    unsigned long tmp;

    if (offset >= size) {
        return size;
    }
    size -= result;
    offset %= BITS_PER_LONG;
    if (offset) {
        tmp = *(p++);
        tmp |= ~0UL >> (BITS_PER_LONG - offset);
        if (size < BITS_PER_LONG) {
            goto found_first;
        }
        if (~tmp) {
            goto found_middle;
        }
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }
    while (size & ~(BITS_PER_LONG-1)) {
        if (~(tmp = *(p++))) {
            goto found_middle;
        }
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }
    if (!size) {
        return result;
    }
    tmp = *p;

found_first:
    tmp |= ~0UL << size;
    if (tmp == ~0UL) {	/* Are any bits zero? */
        return result + size;	/* Nope. */
    }
found_middle:
    return result + ctzl(~tmp);
}

unsigned long find_last_bit(const unsigned long *addr, unsigned long size)
{
    unsigned long words;
    unsigned long tmp;

    /* Start at final word. */
    words = size / BITS_PER_LONG;

    /* Partial final word? */
    if (size & (BITS_PER_LONG-1)) {
        tmp = (addr[words] & (~0UL >> (BITS_PER_LONG
                                       - (size & (BITS_PER_LONG-1)))));
        if (tmp) {
            goto found;
        }
    }

    while (words) {
        tmp = addr[--words];
        if (tmp) {
        found:
            return words * BITS_PER_LONG + BITS_PER_LONG - 1 - clzl(tmp);
        }
    }

    /* Not found */
    return size;
}
void uint32_array_reset(uint32_t *state, const UInt32StateInfo *info, int num)
{
    int i = 0;

    for (i = 0; i < num; ++i) {
        state[i] = info[i].reset;
    }
}

void uint32_write(uint32_t *state, const UInt32StateInfo *info, uint32_t val,
                  const char *prefix, bool debug)
{
    int i;
    uint32_t new_val;
    int width = info->width ? info->width : 32;

    uint32_t no_w0_mask = info->ro | info->w1c | info->nw0 |
                        ~((1ull << width) - 1);
    uint32_t no_w1_mask = info->ro | info->w1c | info->nw1 |
                        ~((1ull << width) - 1);

    if (!info->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to undefined device state "
                      "(written value: %#08x)\n", prefix, val);
        return;
    }

    if (debug) {
        fprintf(stderr, "%s:%s: write of value %08x\n", prefix, info->name,
                val);
    }

    /*FIXME: skip over if no LOG_GUEST_ERROR */
    for (i = 0; i <= 1; ++i) {
        uint32_t test = (val ^ (i ? 0 : ~0)) & (i ? info->ge1 : info->ge0);
        if (test) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s:%s bits %#08x may not be written"
                          " to %d\n", prefix, info->name, test, i);
        }
    }

    new_val = val & ~(no_w1_mask & val);
    new_val |= no_w1_mask & *state & val;
    new_val |= no_w0_mask & *state & ~val;
    new_val &= ~(val & info->w1c);
    *state = new_val;
}

uint32_t uint32_read(uint32_t *state, const UInt32StateInfo *info,
                     const char *prefix, bool debug)
{
    uint32_t ret = *state;

    /* clear on read */
    ret &= ~info->cor;
    *state = ret;

    if (!info->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from undefined device state "
                      "(read value: %#08x)\n", prefix, ret);
        return ret;
    }

    if (debug) {
        fprintf(stderr, "%s:%s: read of value %08x\n", prefix, info->name, ret);
    }

    return ret;
}
