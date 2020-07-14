/*
 * QEMU MDIO bus & slave models
 *
 * Copyright (c) 2016 Xilinx Inc.
 *
 * Written by sai pavan boddu <saipava@xilinx.com>
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
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/mdio/mdio_slave.h"
#include "hw/fdt_generic_util.h"

struct MDIOBus *mdio_init_bus(DeviceState *parent, const char *name)
{
    struct MDIOBus *bus;

    bus = MDIO_BUS(qbus_create(TYPE_MDIO_BUS, parent, name));
    return bus;
}

void mdio_set_slave_addr(MDIOSlave *s, uint8_t addr)
{
    s->addr = addr;
}

static MDIOSlave *mdio_find_slave(struct MDIOBus *bus, uint8_t addr)
{
    MDIOSlave *slave = NULL;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->qbus.children, sibling) {
        DeviceState *qdev = kid->child;
        MDIOSlave *candidate = MDIO_SLAVE(qdev);
        /* For slave having no address, assign the requested addr */
        if (candidate->addr == 0) {
            candidate->addr = addr;
        }
        if (addr == candidate->addr) {
            slave = candidate;
            break;
        }
    }
    return slave;
}

int mdio_send(struct MDIOBus *bus, uint8_t addr, uint8_t reg, uint8_t data)
{
    MDIOSlave *slave = NULL;
    MDIOSlaveClass *sc;

    if ((bus->cur_addr != addr) || !bus->cur_slave) {
        slave = mdio_find_slave(bus, addr);
        if (slave) {
            bus->cur_slave = slave;
            bus->cur_addr = addr;
        } else {
            return -1;
        }
    } else {
        slave = bus->cur_slave;
    }

    sc = MDIO_SLAVE_GET_CLASS(slave);
    if (sc->send) {
        return sc->send(slave, reg, data);
    }
    return -1;
}

int mdio_recv(struct MDIOBus *bus, uint8_t addr, uint8_t reg)
{
    MDIOSlave *slave = NULL;
    MDIOSlaveClass *sc;

    if ((bus->cur_addr != addr) || !bus->cur_slave) {
        slave = mdio_find_slave(bus, addr);
        if (slave) {
            bus->cur_slave = slave;
            bus->cur_addr = addr;
        } else {
            return -1;
        }
    } else {
        slave = bus->cur_slave;
    }

    sc = MDIO_SLAVE_GET_CLASS(slave);
    if (sc->recv) {
        return sc->recv(slave, reg);
    }
    return -1;
}

static Property mdio_props[] = {
    DEFINE_PROP_UINT8("reg", MDIOSlave, addr, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static bool mdio_slave_parse_reg(FDTGenericMMap *obj, FDTGenericRegPropInfo reg,
                                Error **errp)
{
    DeviceState *parent;

    parent = (DeviceState *)object_dynamic_cast(reg.parents[0], TYPE_DEVICE);

    if (!parent) {
        return false;
    }

    if (!parent->realized) {
        return true;
    }

    qdev_set_parent_bus(DEVICE(obj), qdev_get_child_bus(parent, "mdio-bus"));

    return false;
}

static void mdio_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, k->categories);
    k->bus_type = TYPE_MDIO_BUS;
    device_class_set_props(k, mdio_props);
    fmc->parse_reg = mdio_slave_parse_reg;
}

static const TypeInfo mdio_slave_info = {
    .name = TYPE_MDIO_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MDIOSlave),
    .class_size = sizeof(MDIOSlaveClass),
    .class_init = mdio_slave_class_init,
    .interfaces = (InterfaceInfo []) {
        {TYPE_FDT_GENERIC_MMAP},
        { },
    },
};

static const TypeInfo mdio_bus_info = {
    .name = TYPE_MDIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(struct MDIOBus),
};

static void mdio_slave_register_type(void)
{
    type_register_static(&mdio_bus_info);
    type_register_static(&mdio_slave_info);
}

type_init(mdio_slave_register_type)
