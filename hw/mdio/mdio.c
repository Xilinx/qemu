/*
 * QEMU Ethernet MDIO bus & PHY models
 *
 * Copyright (c) 2008 Edgar E. Iglesias,
 *                    Grant Likely (grant.likely@secretlab.ca).
 * Copyright (c) 2016 Xilinx Inc.
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
 *
 * This is a generic MDIO implementation.
 *
 * TODO:
 * - Make the Model use mmio to communicate directly form IO register space.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/mdio/mdio_slave.h"
#include "hw/mdio/mdio.h"

#ifndef MDIO_DEBUG
#define MDIO_DEBUG 0
#endif

#define DPRINT(fmt, args...) \
    do { \
        if (MDIO_DEBUG) { \
            qemu_log("%s: " fmt, __func__, ## args); \
        } \
    } while (0)

static uint16_t mdio_read_req(struct MDIO *s, uint8_t addr, uint8_t reg)
{
    uint16_t val;

    val = mdio_recv(s->bus, addr, reg);
    DPRINT("slave %d reg %d<- 0x%x\n", addr, reg, val);
    return val;
}

static void mdio_write_req(struct MDIO *s, uint8_t addr, uint8_t reg,
                           uint16_t data)
{
     mdio_send(s->bus, addr, reg, data);
     DPRINT("slave %d reg %d<- 0x%x\n", addr, reg, data);
}

static void mdio_init(Object *obj)
{
    MDIO *s = MDIO(obj);

    s->read = mdio_read_req;
    s->write = mdio_write_req;
}

static void mdio_realize(DeviceState *dev, Error **errp)
{
    MDIO *s = MDIO(dev);

    s->bus = mdio_init_bus(dev, "mdio-bus");

    if (!s->bus) {
        DPRINT("mdio bus un-initialized\n");
    }
}

static void mdio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = mdio_realize;
}

static const TypeInfo mdio_info = {
    .name = TYPE_MDIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MDIO),
    .class_size = sizeof(MDIOClass),
    .class_init = mdio_class_init,
    .instance_init = mdio_init,
};

static void mdio_register_types(void)
{
    type_register_static(&mdio_info);
}

type_init(mdio_register_types)
