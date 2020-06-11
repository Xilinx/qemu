/*
 * QEMU model of a gpio based memory region muxer
 *
 * Copyright (c) 2017 Xilinx Inc.
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"

#define TYPE_GPIO_MR_MUX "gpio-mr-mux"

#define GPIO_MR_MUX(obj) \
     OBJECT_CHECK(GpioMrMux, (obj), TYPE_GPIO_MR_MUX)

#ifndef MR_MUX_DEBUG
#define MR_MUX_DEBUG 0
#endif

#define MAX_NR_GPIOS (4)
#define MAX_REGIONS (1 << MAX_NR_GPIOS)

typedef struct GpioMrMux {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    MemoryRegion *mr[MAX_REGIONS];
    MemoryRegion mr_alias[MAX_REGIONS];

    struct {
        uint64_t mr_size;
    } cfg;

    uint32_t state;
} GpioMrMux;

static void update_regions(GpioMrMux *s)
{
    int i;

    for (i = 0; i < MAX_REGIONS; i++) {
        bool enabled = s->state == i;
        memory_region_set_enabled(&s->mr_alias[i], enabled);
    }
}

static void input_handler(void *opaque, int nr, int level)
{
    GpioMrMux *s = GPIO_MR_MUX(opaque);

    s->state = deposit32(s->state, nr, 1, level);
    update_regions(s);
}

static void gpio_mr_mux_realize(DeviceState *dev, Error **errp)
{
    GpioMrMux *s = GPIO_MR_MUX(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    memory_region_init(&s->iomem, OBJECT(dev),
                       TYPE_GPIO_MR_MUX, s->cfg.mr_size);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < MAX_REGIONS; i++) {
        if (s->mr[i]) {
            char *name = g_strdup_printf("mr-alias%d", i);

            /* Create aliases because we can't modify the orginal MRs */
            memory_region_init_alias(&s->mr_alias[i], OBJECT(s),
                    name, s->mr[i], 0, memory_region_size(s->mr[i]));
            memory_region_add_subregion_overlap(&s->iomem, 0,
                                                &s->mr_alias[i], 0);
            g_free(name);
        }
    }
}

static void gpio_mr_mux_init(Object *obj)
{
    GpioMrMux *s = GPIO_MR_MUX(obj);
    int i;

    qdev_init_gpio_in(DEVICE(obj), input_handler, MAX_NR_GPIOS);

    for (i = 0; i < MAX_REGIONS; i++) {
            char *name = g_strdup_printf("mr%d", i);

            object_property_add_link(obj, name, TYPE_MEMORY_REGION,
                             (Object **)&s->mr[i],
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
            g_free(name);
    }
}

static Property gpio_mr_mux_properties[] = {
    DEFINE_PROP_UINT64("mr-size", GpioMrMux, cfg.mr_size, UINT64_MAX),
    DEFINE_PROP_END_OF_LIST(),
};

static void gpio_mr_mux_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = gpio_mr_mux_realize;
    device_class_set_props(dc, gpio_mr_mux_properties);
}

static const TypeInfo gpio_mr_mux_info = {
    .name          = TYPE_GPIO_MR_MUX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GpioMrMux),
    .class_init    = gpio_mr_mux_class_init,
    .instance_init = gpio_mr_mux_init,
};

static void gpio_mr_mux_register_types(void)
{
    type_register_static(&gpio_mr_mux_info);
}

type_init(gpio_mr_mux_register_types)
