/*
 * Register Definition API
 *
 * Copyright (c) 2013 Xilinx Inc.
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/register-dep.h"
#include "qemu/log.h"

static inline void register_write_log(DepRegisterInfo *reg, int dir, uint64_t val,
                                      int mask, const char *msg,
                                      const char *reason)
{
    qemu_log_mask(mask, "%s:%s bits %#" PRIx64 " %s write of %d%s%s\n",
                  reg->prefix, reg->access->name, val, msg, dir,
                  reason ? ": " : "", reason ? reason : "");
}

static inline void register_write_val(DepRegisterInfo *reg, uint64_t val)
{
    if (!reg->data) {
        return;
    }
    switch (reg->data_size) {
    case 1:
        *(uint8_t *)reg->data = val;
        break;
    case 2:
        *(uint16_t *)reg->data = val;
        break;
    case 4:
        *(uint32_t *)reg->data = val;
        break;
    case 8:
        *(uint64_t *)reg->data = val;
        break;
    default:
        abort();
    }
}

static inline uint64_t register_read_val(DepRegisterInfo *reg)
{
    switch (reg->data_size) {
    case 1:
        return *(uint8_t *)reg->data;
    case 2:
        return *(uint16_t *)reg->data;
    case 4:
        return *(uint32_t *)reg->data;
    case 8:
        return *(uint64_t *)reg->data;
    default:
        abort();
    }
    return 0; /* unreachable */
}

void dep_register_write(DepRegisterInfo *reg, uint64_t val, uint64_t we)
{
    uint64_t old_val, new_val, test, no_w_mask;
    const DepRegisterAccessInfo *ac;
    const DepRegisterAccessError *rae;

    assert(reg);

    ac = reg->access;
    old_val = reg->data ? register_read_val(reg) : ac->reset;
    if (reg->write_lite && !~we) { /* fast path!! */
        new_val = val;
        goto register_write_fast;
    }

    if (!ac || !ac->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to undefined device state "
                      "(written value: %#" PRIx64 ")\n", reg->prefix, val);
        return;
    }

    no_w_mask = ac->ro | ac->w1c | ~we;

    if (reg->debug) {
        qemu_log("%s:%s: write of value %#" PRIx64 "\n", reg->prefix, ac->name,
                 val);
    }

    if (qemu_loglevel_mask(LOG_GUEST_ERROR)) {
        test = (old_val ^ val) & ac->rsvd;
        if (test) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: change of value in reserved bit"
                          "fields: %#" PRIx64 ")\n", reg->prefix, test);
        }
        for (rae = ac->ge1; rae && rae->mask; rae++) {
            test = val & rae->mask;
            if (test) {
                register_write_log(reg, 1, test, LOG_GUEST_ERROR,
                                   "invalid", rae->reason);
            }
        }
        for (rae = ac->ge0; rae && rae->mask; rae++) {
            test = ~val & rae->mask;
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
            test = ~val & rae->mask;
            if (test) {
                register_write_log(reg, 0, test, LOG_GUEST_ERROR,
                                   "unimplemented", rae->reason);
            }
        }
    }

    new_val = (val & ~no_w_mask) | (old_val & no_w_mask);
    new_val &= ~(val & ac->w1c);

    if (ac->pre_write) {
        new_val = ac->pre_write(reg, new_val);
    }
register_write_fast:
    register_write_val(reg, new_val);
    dep_register_refresh_gpios(reg, old_val);

    if (ac->post_write) {
        ac->post_write(reg, new_val);
    }
}

uint64_t dep_register_read(DepRegisterInfo *reg)
{
    uint64_t ret;
    const DepRegisterAccessInfo *ac;

    assert(reg);

    ac = reg->access;
    if (!ac || !ac->name) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from undefined device state\n",
                      reg->prefix);
        return 0;
    }

    ret = reg->data ? register_read_val(reg) : ac->reset;

    if (!reg->read_lite) {
        register_write_val(reg, ret & ~ac->cor);
    }

    if (ac->post_read) {
        ret = ac->post_read(reg, ret);
    }

    if (!reg->read_lite) {
        if (reg->debug) {
            qemu_log("%s:%s: read of value %#" PRIx64 "\n", reg->prefix,
                     ac->name, ret);
        }
    }

    return ret;
}

void dep_register_reset(DepRegisterInfo *reg)
{
    assert(reg);
    const DepRegisterAccessInfo *ac;
    uint64_t val;

    if (!reg->data || !reg->access) {
        return;
    }

    ac = reg->access;

    val = register_read_val(reg);
    if (!(val & ac->inhibit_reset)) {
        val = reg->access->reset;
    }
    /* FIXME: move to init */
    /* if there are no debug msgs and no RMW requirement, mark for fast write */
    reg->write_lite = reg->debug || ac->ro || ac->w1c || ac->pre_write ||
            ((ac->ge0 || ac->ge1) && qemu_loglevel_mask(LOG_GUEST_ERROR)) ||
            ((ac->ui0 || ac->ui1) && qemu_loglevel_mask(LOG_UNIMP))
             ? false : true;
    /* no debug and no clear-on-read is a fast read */
    reg->read_lite = reg->debug || ac->cor ? false : true;

    register_write_val(reg, val);
    dep_register_refresh_gpios(reg, ~val);
}

void dep_register_refresh_gpios(DepRegisterInfo *reg, uint64_t old_value)
{
    const DepRegisterAccessInfo *ac;
    const DepRegisterGPIOMapping *gpio;

    ac = reg->access;
    for (gpio = ac->gpios; gpio && gpio->name; gpio++) {
        int i;

        if (gpio->input) {
            continue;
        }

        for (i = 0; i < gpio->num; ++i) {
            uint64_t gpio_value, gpio_value_old;

            qemu_irq gpo = qdev_get_gpio_out_named(DEVICE(reg), gpio->name, i);
            /* FIXME: do at init time, not lazily in fast path */
            if (!gpio->width) {
                ((DepRegisterGPIOMapping *)gpio)->width = 1;
            }
            gpio_value_old = extract64(old_value,
                                   gpio->bit_pos + i * gpio->width,
                                   gpio->width) ^ gpio->polarity;
            gpio_value = extract64(register_read_val(reg),
                                   gpio->bit_pos + i * gpio->width,
                                   gpio->width) ^ gpio->polarity;
            if (!(gpio_value_old ^ gpio_value)) {
                continue;
            }
            if (reg->debug && gpo) {
                qemu_log("refreshing gpio out %s to %" PRIx64 "\n",
                         gpio->name, gpio_value);
            }
            qemu_set_irq(gpo, gpio_value);
        }
    }
}

typedef struct DeviceNamedGPIOHandlerOpaque {
    DeviceState *dev;
    const char *name;
} DeviceNamedGPIOHandlerOpaque;

static void register_gpio_handler(void *opaque, int n, int level)
{
    DeviceNamedGPIOHandlerOpaque *gho = opaque;
    DepRegisterInfo *reg = DEP_REGISTER(gho->dev);

    const DepRegisterAccessInfo *ac;
    const DepRegisterGPIOMapping *gpio;

    ac = reg->access;
    for (gpio = ac->gpios; gpio && gpio->name; gpio++) {
        if (gpio->input && !strcmp(gho->name, gpio->name)) {
            /* FIXME: do at init time, not lazily in fast path */
            if (!gpio->width) {
                ((DepRegisterGPIOMapping *)gpio)->width = 1;
            }
            register_write_val(reg, deposit64(register_read_val(reg),
                                              gpio->bit_pos + n * gpio->width,
                                              gpio->width,
                                              level ^ gpio->polarity));
            return;
        }
    }

    abort();
}

/* FIXME: Convert to proper QOM init fn */

void dep_register_init(DepRegisterInfo *reg)
{
    assert(reg);
    const DepRegisterAccessInfo *ac;
    const DepRegisterGPIOMapping *gpio;

    if (!reg->data || !reg->access) {
        return;
    }

    object_initialize((void *)reg, sizeof(*reg), TYPE_DEP_REGISTER);

    ac = reg->access;
    for (gpio = ac->gpios; gpio && gpio->name; gpio++) {
        if (!gpio->num) {
            ((DepRegisterGPIOMapping *)gpio)->num = 1;
        }
        if (gpio->input) {
            DeviceNamedGPIOHandlerOpaque gho = {
                .name = gpio->name,
                .dev = DEVICE(reg),
            };
            qemu_irq irq;

            qdev_init_gpio_in_named(DEVICE(reg), register_gpio_handler,
                                    gpio->name, gpio->num);
            /* FIXME: Pure evil, but GPIO handlers don't know their names yet */
            irq = qdev_get_gpio_in_named(DEVICE(reg), gpio->name, gpio->num);
            irq->opaque = g_memdup(&gho, sizeof(gho));
        } else {
            /* FIXME: propably ment to be freed somewhere */
            qemu_irq *gpos = g_new0(qemu_irq, gpio->num);

            qdev_init_gpio_out_named(DEVICE(reg), gpos, gpio->name, gpio->num);
        }
    }
}

static inline void register_write_memory(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned size, bool be)
{
    DepRegisterInfo *reg = opaque;
    uint64_t we = ~0;
    int shift = 0;

    if (reg->data_size != size) {
        we = (size == 8) ? ~0ull : (1ull << size * 8) - 1;
        shift = 8 * (be ? reg->data_size - size - addr : addr);
    }

    assert(size + addr <= reg->data_size);
    dep_register_write(reg, value << shift, we << shift);
}

void dep_register_write_memory_be(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    register_write_memory(opaque, addr, value, size, true);
}


void dep_register_write_memory_le(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    register_write_memory(opaque, addr, value, size, false);
}

static inline uint64_t register_read_memory(void *opaque, hwaddr addr,
                                            unsigned size, bool be)
{
    DepRegisterInfo *reg = opaque;
    int shift = 8 * (be ? reg->data_size - size - addr : addr);

    return dep_register_read(reg) >> shift;
}

uint64_t dep_register_read_memory_be(void *opaque, hwaddr addr, unsigned size)
{
    return register_read_memory(opaque, addr, size, true);
}

uint64_t dep_register_read_memory_le(void *opaque, hwaddr addr, unsigned size)
{
    return register_read_memory(opaque, addr, size, false);
}

static const TypeInfo register_info = {
    .name  = TYPE_DEP_REGISTER,
    .parent = TYPE_DEVICE,
};

static void register_register_types(void)
{
    type_register_static(&register_info);
}

type_init(register_register_types)
