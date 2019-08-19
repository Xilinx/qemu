/*
 * etrace gpio
 *
 * Copyright (c) 2014 Xilinx
 * Written by Edgar E. Iglesias
 *
 * FIXME: Clean up. Can probably be done more effiently and without
 *        touch all the internals of sysbus and devices.
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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qapi-events-ui.h"
#include "sysemu/arch_init.h"
#include "qemu/config-file.h"

#include "qemu/etrace.h"

#ifndef CONFIG_USER_ONLY
#include "hw/qdev-core.h"
#include "hw/sysbus.h"

typedef struct {
    DeviceState *dev;
    const char *devname;
    char *name;
    unsigned int level;
    bool set;
} IRQInterceptState;

// static void trace_irq_handler(void *opaque, int n, int level)
// {
//     IRQInterceptState *iis = opaque;
//     uint32_t flags = ETRACE_EVU64_F_NONE;

//     if (iis->set && (iis->level == level)) {
//         return;
//     }

//     if (iis->set) {
//         flags = ETRACE_EVU64_F_PREV_VAL;
//     }
//     etrace_event_u64(&qemu_etracer, -1, flags, iis->devname,
//                      iis->name, level, iis->level);
//     iis->set = true;
//     iis->level = level;
// }

// static void intercept_irq(DeviceState *dev, char *irq_name, int i, char *name)
// {
//     IRQInterceptState *iis;
//     qemu_irq new;

//     iis = g_new0(IRQInterceptState, 1);
//     iis->devname = object_get_canonical_path(OBJECT(dev));
//     iis->name = name;
//     new = qemu_allocate_irq(trace_irq_handler, iis, 0);
//     object_property_add_child(OBJECT(dev), name, OBJECT(new), NULL);
//     qdev_connect_gpio_out_named(dev, irq_name, i, new);
// }

static void sysbus_init(SysBusDevice *dev)
{
    /* FIXME: restore service */
    // unsigned int i = 0;

    // for (i = 0; i < dev->num_irq; i++) {
    //     char *name;
    //     int r;
    //     /* Unfortunately there is no sysbus_get_irq() :( */
    //     char *irq_name = g_strdup_printf(SYSBUS_DEVICE_GPIO_IRQ "-%d", i);
    //     qemu_irq irq = qdev_get_gpio_out_named(DEVICE(dev), irq_name, 0);

    //     if (!irq) {
    //         g_free(irq_name);
    //         continue;
    //     }

    //     r = asprintf(&name, "irq[%u]", i);
    //     assert(r > 0);
    //     intercept_irq(DEVICE(dev), irq_name, i, name);
    //     g_free(irq_name);
    // }
}

static void dev_named_init(DeviceState *dev, NamedGPIOList *l)
{
    // unsigned int i = 0;

    // if (!l->num_out) {
    //     return;
    // }

    // for (i = 0; i < l->num_out; i++) {
    //     char *name;
    //     int r;

    //     if (!l->out[i]) {
    //         continue;
    //     }

    //     r = asprintf(&name, "%s[%u]", l->name ? l->name : "gpio-out", i);
    //     assert(r > 0);
    //     intercept_irq(DEVICE(dev), l->name, i, name);
    // }
}

static int dev_init(Object *obj, void *opaque)
{
    SysBusDevice *sbd =
        (SysBusDevice *)object_dynamic_cast(obj, TYPE_SYS_BUS_DEVICE);
    DeviceState *dev = (DeviceState *)object_dynamic_cast(obj, TYPE_DEVICE);
    NamedGPIOList *l;

    if (sbd) {
        sysbus_init(sbd);
    }

    if (dev) {
        QLIST_FOREACH(l, &dev->gpios, node) {
            dev_named_init(dev, l);
        }
    }
    return 0;
}

void qemu_etrace_gpio_init(void)
{
    object_child_foreach_recursive(object_get_root(), dev_init, NULL);
}

#endif
