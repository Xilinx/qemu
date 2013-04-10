/*
 * Register Definition API
 *
 * Copyright (c) 2013 Xilinx Inc.
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "exec/register.h"
#include "qemu/log.h"

static inline void register_write_log(RegisterInfo *reg, int dir, uint64_t val,
                                      int mask, const char *msg,
                                      const char *reason)
{
    qemu_log_mask(mask, "%s:%s bits %#" PRIx64 " %s write of %d%s%s\n",
                  reg->prefix, reg->access->name, val, msg, dir,
                  reason ? ": " : "", reason ? reason : "");
}

static inline void register_write_val(RegisterInfo *reg, uint64_t val)
{
    int i;

    for (i = 0; i < reg->data_size; ++i) {
        reg->data[i] = val >> (reg->data_big_endian ?
                    8 * (reg->data_size - 1 - i) : 8 * i);
    }
}

static inline uint64_t register_read_val(RegisterInfo *reg)
{
    uint64_t ret = 0;
    int i;

    for (i = 0; i < reg->data_size; ++i) {
        ret |= (uint64_t)reg->data[i] << (reg->data_big_endian ?
                    8 * (reg->data_size - 1 - i) : 8 * i);
    }
    return ret;
}

void register_write(RegisterInfo *reg, uint64_t val, uint64_t we)
{
    uint64_t old_val, new_val, test;
    const RegisterAccessInfo *ac;
    const RegisterAccessError *rae;

    assert(reg);

    ac = reg->access;
    if (!ac || !ac->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to undefined device state "
                      "(written value: %#" PRIx64 ")\n", reg->prefix, val);
        return;
    }

    uint32_t no_w0_mask = ac->ro | ac->w1c | ac->nw0 | ~we;
    uint32_t no_w1_mask = ac->ro | ac->w1c | ac->nw1 | ~we;

    if (reg->debug) {
        qemu_log("%s:%s: write of value %#" PRIx64 "\n", reg->prefix, ac->name,
                 val);
    }

    if (qemu_loglevel_mask(LOG_GUEST_ERROR)) {
        for (rae = ac->ge1; rae && rae->mask; rae++) {
            test = val & rae->mask;
            if (test) {
                register_write_log(reg, 1, test, LOG_GUEST_ERROR,
                                   "invalid", rae->reason);
            }
        }
        for (rae = ac->ge0; rae && rae->mask; rae++) {
            test = val & rae->mask;
            if (test) {
                register_write_log(reg, 0, test, LOG_GUEST_ERROR,
                                   "invalid", rae->reason);
            }
        }
    }

    if (qemu_loglevel_mask(LOG_UNIMP)) {
        for (rae = ac->ui1; rae && rae->mask; rae++) {
            test = val & rae->mask;
            if (test) {
                register_write_log(reg, 1, test, LOG_GUEST_ERROR,
                                   "unimplmented", rae->reason);
            }
        }
        for (rae = ac->ui0; rae && rae->mask; rae++) {
            test = val & rae->mask;
            if (test) {
                register_write_log(reg, 0, test, LOG_GUEST_ERROR,
                                   "unimplemented", rae->reason);
            }
        }
    }

    assert(reg->data);
    old_val = register_read_val(reg);

    new_val = val & ~(no_w1_mask & val);
    new_val |= no_w1_mask & old_val & val;
    new_val |= no_w0_mask & old_val & ~val;
    new_val &= ~(val & ac->w1c);

    if (ac->pre_write) {
        new_val = ac->pre_write(reg, new_val);
    }
    register_write_val(reg, new_val);
    if (ac->post_write) {
        ac->post_write(reg, new_val);
    }
}

uint64_t register_read(RegisterInfo *reg)
{
    uint64_t ret;
    const RegisterAccessInfo *ac;

    assert(reg);

    ac = reg->access;
    if (!ac || !ac->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from undefined device state\n",
                      reg->prefix);
        return 0;
    }

    assert(reg->data);
    if (ac->pre_read) {
        ac->pre_read(reg);
    }
    ret = register_read_val(reg);

    register_write_val(reg, ret & ~ac->cor);

    ret &= ~ac->wo;
    ret |= ac->wo & ac->reset;

    if (ac->post_read) {
        ret = ac->post_read(reg, ret);
    }
    if (reg->debug) {
        qemu_log("%s:%s: read of value %#" PRIx64 "\n", reg->prefix, ac->name,
                 ret);
    }

    return ret;
}

void register_reset(RegisterInfo *reg)
{
    assert(reg);

    if (!reg->data || !reg->access) {
        return;
    }

    register_write_val(reg, reg->access->reset);
}

static inline void register_write_memory(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned size, bool be)
{
    RegisterInfo *reg = opaque;
    uint64_t we = (size == 8) ? ~0ull : (1ull << size * 8) - 1;
    int shift = 8 * (be ? reg->data_size - size - addr : addr);

    assert(size + addr <= reg->data_size);
    register_write(reg, value << shift, we << shift);
}

void register_write_memory_be(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    register_write_memory(opaque, addr, value, size, true);
}


void register_write_memory_le(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    register_write_memory(opaque, addr, value, size, false);
}

static inline uint64_t register_read_memory(void *opaque, hwaddr addr,
                                            unsigned size, bool be)
{
    RegisterInfo *reg = opaque;
    int shift = 8 * (be ? reg->data_size - size - addr : addr);

    return register_read(reg) >> shift;
}

uint64_t register_read_memory_be(void *opaque, hwaddr addr, unsigned size)
{
    return register_read_memory(opaque, addr, size, true);
}

uint64_t register_read_memory_le(void *opaque, hwaddr addr, unsigned size)
{
    return register_read_memory(opaque, addr, size, false);
}
