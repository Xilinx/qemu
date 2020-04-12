/*
 * QEMU model of Xilinx I/O Module PIT
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/register.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/fdt_generic_util.h"

#ifndef XILINX_IO_MODULE_PIT_ERR_DEBUG
#define XILINX_IO_MODULE_PIT_ERR_DEBUG 0
#endif

#define TYPE_XILINX_IO_MODULE_PIT "xlnx.io_pit"

#define XILINX_IO_MODULE_PIT(obj) \
     OBJECT_CHECK(XilinxPIT, (obj), TYPE_XILINX_IO_MODULE_PIT)

REG32(IOM_PIT_PRELOAD, 0x0)
REG32(IOM_PIT_COUNTER, 0x4)
REG32(IOM_PIT_CONTROL, 0x8)
 FIELD(IOM_PIT_CONTROL, EN, 0, 1)
 FIELD(IOM_PIT_CONTROL, PRELOAD, 1, 1)

#define R_MAX                       (R_IOM_PIT_CONTROL + 1)

typedef struct XilinxPIT {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    struct {
        bool use;
        uint32_t size;
        bool readable;
        bool interrupt;
    } cfg;
    uint32_t frequency;
    /* Counter in Pre-Scalar(ps) Mode */
    uint32_t ps_counter;
    /* ps_mode irq-in to enable/disable pre-scalar */
    bool ps_enable;
    /* IRQ to pulse out when present timer hits zero */
    qemu_irq hit_out;
    /* State var to remember hit_in level */
    bool ps_level;

    ptimer_state *ptimer;
    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
    const char *prefix;
} XilinxPIT;

static Property xlx_iom_properties[] = {
    DEFINE_PROP_UINT32("frequency", XilinxPIT, frequency, 66*1000000),
    DEFINE_PROP_BOOL("use-pit", XilinxPIT, cfg.use, 0),
    DEFINE_PROP_UINT32("pit-size", XilinxPIT, cfg.size, 1),
    DEFINE_PROP_BOOL("pit-readable", XilinxPIT, cfg.readable, 1),
    DEFINE_PROP_BOOL("pit-interrupt", XilinxPIT, cfg.interrupt, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static uint64_t pit_ctr_pr(RegisterInfo *reg, uint64_t val)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(reg->opaque);
    uint32_t r;

    if (!s->cfg.use) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Disabled\n", s->prefix);
        return 0xdeadbeef;
    }

    if (s->ps_enable) {
        r = s->ps_counter;
    } else {
        r = ptimer_get_count(s->ptimer);
    }
    return r;
}

static void pit_control_pw(RegisterInfo *reg, uint64_t value)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(reg->opaque);
    uint32_t v32 = value;

    if (!s->cfg.use) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Disabled\n", s->prefix);
        return;
    }

    ptimer_transaction_begin(s->ptimer);
    ptimer_stop(s->ptimer);
    if (v32 & R_IOM_PIT_CONTROL_EN_MASK) {
        if (s->ps_enable) {
            /* pre-scalar mode Do-Nothing here. Wait for the friend to hit_in
             * and decrement the counter(s->ps_counter)*/
            s->ps_counter = s->regs[R_IOM_PIT_PRELOAD];
        } else {
            ptimer_set_limit(s->ptimer, s->regs[R_IOM_PIT_PRELOAD], 1);
            ptimer_run(s->ptimer, !(v32 & R_IOM_PIT_CONTROL_PRELOAD_MASK));

        }
    }
    ptimer_transaction_commit(s->ptimer);
}

static void pit_timer_hit(void *opaque)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(opaque);

    qemu_irq_pulse(s->irq);
    /* hit_out to make another pit move its counter in pre-scalar mode */
    qemu_irq_pulse(s->hit_out);
}

static void iom_pit_ps_hit_in(void *opaque, int n, int level)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(opaque);

    if (!(s->regs[R_IOM_PIT_CONTROL] & R_IOM_PIT_CONTROL_EN_MASK)) {
        /* PIT disabled */
        return;
    }

    /* Count only on positive edge */
    if (!s->ps_level && level) {
        s->ps_counter--;
        s->ps_level = level;
    } else {
        /* Not pos edge */
        s->ps_level = level;
        return;
    }

    /* If timer expires, try to preload or stop */
    if (s->ps_counter == 0) {
        pit_timer_hit(opaque);
        /* Check for pit preload/one-shot mode */
        if (s->regs[R_IOM_PIT_CONTROL] & R_IOM_PIT_CONTROL_PRELOAD_MASK) {
            /* Preload Mode, Reload the ps_counter */
            s->ps_counter = s->regs[R_IOM_PIT_PRELOAD];
        } else {
            /* One-Shot mode, turn off the timer */
            s->regs[R_IOM_PIT_CONTROL] &= ~R_IOM_PIT_CONTROL_EN_MASK;
        }
    }
}

static void iom_pit_ps_config(void *opaque, int n, int level)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(opaque);
    s->ps_enable = level;
}

static const RegisterAccessInfo pit_regs_info[] = {
    [R_IOM_PIT_PRELOAD] = { .name = "PRELOAD", .addr = A_IOM_PIT_PRELOAD },
    [R_IOM_PIT_COUNTER] = { .name = "COUNTER", .addr = A_IOM_PIT_COUNTER,
                            .post_read = pit_ctr_pr },
    [R_IOM_PIT_CONTROL] = { .name = "CONTROL", .addr = A_IOM_PIT_CONTROL,
                            .post_write = pit_control_pw },
};

static const MemoryRegionOps iom_pit_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void iom_pit_reset(DeviceState *dev)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    s->ps_level = false;
}

static void xlx_iom_realize(DeviceState *dev, Error **errp)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(dev);

    s->prefix = object_get_canonical_path(OBJECT(dev));

    if (s->cfg.use) {
        s->ptimer = ptimer_init(pit_timer_hit, s, PTIMER_POLICY_DEFAULT);
        ptimer_transaction_begin(s->ptimer);
        ptimer_set_freq(s->ptimer, s->frequency);
        ptimer_transaction_commit(s->ptimer);

        /* IRQ out to pulse when present timer expires/reloads */
        qdev_init_gpio_out(dev, &s->hit_out, 1);
        /* IRQ in to enable pre-scalar mode. Routed from gpo1 */
        qdev_init_gpio_in_named(dev, iom_pit_ps_config, "ps_config", 1);
        /* hit_out of neighbouring PIT is received as hit_in */
        qdev_init_gpio_in_named(dev, iom_pit_ps_hit_in, "ps_hit_in", 1);
    }
}

static void xlx_iom_pit_init(Object *obj)
{
    XilinxPIT *s = XILINX_IO_MODULE_PIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    reg_array =
        register_init_block32(DEVICE(obj), pit_regs_info,
                              ARRAY_SIZE(pit_regs_info),
                              s->regs_info, s->regs,
                              &iom_pit_ops,
                              XILINX_IO_MODULE_PIT_ERR_DEBUG,
                              R_MAX * 4);

    memory_region_init_io(&s->iomem, obj, &iom_pit_ops, reg_array,
                          TYPE_XILINX_IO_MODULE_PIT,
                          R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_xlx_iom = {
    .name = TYPE_XILINX_IO_MODULE_PIT,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static void xlx_iom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = iom_pit_reset;
    dc->realize = xlx_iom_realize;
    device_class_set_props(dc, xlx_iom_properties);
    dc->vmsd = &vmstate_xlx_iom;
}

static const TypeInfo xlx_iom_info = {
    .name          = TYPE_XILINX_IO_MODULE_PIT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxPIT),
    .class_init    = xlx_iom_class_init,
    .instance_init = xlx_iom_pit_init,
};

static void xlx_iom_register_types(void)
{
    type_register_static(&xlx_iom_info);
}

type_init(xlx_iom_register_types)
