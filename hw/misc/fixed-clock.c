/*
 * QEMU model of a Fixed Clock source.
 *
 * Copyright (c) 2013 Xilinx Inc.
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/fdt_generic_util.h"

#define TYPE_FIXED_CLOCK "fixed-clock"

#define FIXED_CLOCK(obj) \
    OBJECT_CHECK(FixedClock, (obj), TYPE_FIXED_CLOCK)

typedef struct FixedClock {
    /* private */
    DeviceState parent_obj;
    /*public */
    uint32_t freq_hz;
    qemu_irq clk;
} FixedClock;

static void fixed_clock_reset(DeviceState *dev)
{
    FixedClock *s = FIXED_CLOCK(dev);

    qemu_set_irq(s->clk, s->freq_hz);
}

static void fixed_clock_init(Object *obj)
{
    FixedClock *s = FIXED_CLOCK(obj);

    qdev_init_gpio_out(DEVICE(obj), &s->clk, 1);
}

static Property fixed_clock_properties [] = {
    DEFINE_PROP_UINT32("clock-frequency", FixedClock, freq_hz, 10000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void fixed_clock_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = fixed_clock_reset;
    device_class_set_props(dc, fixed_clock_properties);
}

static const TypeInfo fixed_clock_info = {
    .name          = "fixed-clock",
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(FixedClock),
    .class_init    = fixed_clock_class_init,
    .instance_init = fixed_clock_init,
    .interfaces    = (InterfaceInfo []) {
        { TYPE_FDT_GENERIC_GPIO },
        { },
    },
};

static void fixed_clock_register_types(void)
{
    type_register_static(&fixed_clock_info);
}

type_init(fixed_clock_register_types)
