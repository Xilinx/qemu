/*
 * QEMU Synchronous Serial Interface support
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
 * Copyright (c) 2012 PetaLogix Pty Ltd.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "qapi/error.h"
#include "hw/fdt_generic_util.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qapi/error.h"

struct SSIBus {
    BusState parent_obj;
};

#define TYPE_SSI_BUS "SSI"
#define SSI_BUS(obj) OBJECT_CHECK(SSIBus, (obj), TYPE_SSI_BUS)

static const TypeInfo ssi_bus_info = {
    .name = TYPE_SSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SSIBus),
};

static void ssi_cs_default(void *opaque, int n, int level)
{
    SSISlave *s = SSI_SLAVE(opaque);
    bool cs = !!level;
    assert(n == 0);
    if (s->cs != cs) {
        SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(s);
        if (ssc->set_cs) {
            ssc->set_cs(s, cs);
        }
    }
    s->cs = cs;
}

static uint32_t ssi_transfer_raw_default(SSISlave *dev, uint32_t val)
{
    SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(dev);

    if ((dev->cs && ssc->cs_polarity == SSI_CS_HIGH) ||
            (!dev->cs && ssc->cs_polarity == SSI_CS_LOW) ||
            ssc->cs_polarity == SSI_CS_NONE) {
        return ssc->transfer(dev, val);
    }
    return 0;
}

static bool ssi_slave_parse_reg(FDTGenericMMap *obj, FDTGenericRegPropInfo reg,
                                Error **errp)
{
    SSISlave *s = SSI_SLAVE(obj);
    SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(s);
    DeviceState *parent = DEVICE(reg.parents[0]);
    BusState *parent_bus;
    char bus_name[16];

    if (!parent) {
        /* Not much we can do here but aborting.  */
        error_setg(&error_fatal, "%s: No SSI Parent", DEVICE(s)->id);
    }

    if (!parent->realized) {
        return true;
    }

    if (ssc->transfer_raw == ssi_transfer_raw_default &&
        ssc->cs_polarity != SSI_CS_NONE) {
        qdev_connect_gpio_out(parent, reg.a[0],
                              qdev_get_gpio_in_named(DEVICE(s),
                                                     SSI_GPIO_CS, 0));
    }

    snprintf(bus_name, 16, "spi%" PRIx64, reg.b[0]);
    parent_bus = qdev_get_child_bus(parent, bus_name);
    if (!parent_bus) {
        /* Not every spi bus ends with a numeral
         * so try just the name as well
         */
        snprintf(bus_name, 16, "spi");
        parent_bus = qdev_get_child_bus(parent, bus_name);
    }
    qdev_set_parent_bus(DEVICE(s), parent_bus);
    return false;
}

static void ssi_slave_realize(DeviceState *dev, Error **errp)
{
    SSISlave *s = SSI_SLAVE(dev);
    SSISlaveClass *ssc = SSI_SLAVE_GET_CLASS(s);

    if (ssc->transfer_raw == ssi_transfer_raw_default &&
            ssc->cs_polarity != SSI_CS_NONE) {
        qdev_init_gpio_in_named(dev, ssi_cs_default, SSI_GPIO_CS, 1);
    }

    ssc->realize(s, errp);
}

static void ssi_slave_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *ssc = SSI_SLAVE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_CLASS(klass);

    dc->realize = ssi_slave_realize;
    dc->bus_type = TYPE_SSI_BUS;
    if (!ssc->transfer_raw) {
        ssc->transfer_raw = ssi_transfer_raw_default;
    }
    fmc->parse_reg = ssi_slave_parse_reg;
}

static const TypeInfo ssi_slave_info = {
    .name = TYPE_SSI_SLAVE,
    .parent = TYPE_DEVICE,
    .class_init = ssi_slave_class_init,
    .class_size = sizeof(SSISlaveClass),
    .interfaces = (InterfaceInfo []) {
        { TYPE_FDT_GENERIC_MMAP },
        {},
    },
    .abstract = true,
};

bool ssi_realize_and_unref(DeviceState *dev, SSIBus *bus, Error **errp)
{
    return qdev_realize_and_unref(dev, &bus->parent_obj, errp);
}

DeviceState *ssi_create_slave(SSIBus *bus, const char *name)
{
    DeviceState *dev = qdev_new(name);

    ssi_realize_and_unref(dev, bus, &error_fatal);
    return dev;
}

SSIBus *ssi_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
    bus = qbus_create(TYPE_SSI_BUS, parent, name);
    return SSI_BUS(bus);
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    BusState *b = BUS(bus);
    BusChild *kid;
    SSISlaveClass *ssc;
    uint32_t r = 0;

    QTAILQ_FOREACH(kid, &b->children, sibling) {
        SSISlave *slave = SSI_SLAVE(kid->child);
        ssc = SSI_SLAVE_GET_CLASS(slave);
        r |= ssc->transfer_raw(slave, val);
    }

    return r;
}

const VMStateDescription vmstate_ssi_slave = {
    .name = "SSISlave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(cs, SSISlave),
        VMSTATE_END_OF_LIST()
    }
};

static void ssi_slave_register_types(void)
{
    type_register_static(&ssi_bus_info);
    type_register_static(&ssi_slave_info);
}

type_init(ssi_slave_register_types)

typedef struct SSIAutoConnectArg {
    qemu_irq **cs_linep;
    SSIBus *bus;
} SSIAutoConnectArg;

static int ssi_auto_connect_slave(Object *child, void *opaque)
{
    SSIAutoConnectArg *arg = opaque;
    SSISlave *dev = (SSISlave *)object_dynamic_cast(child, TYPE_SSI_SLAVE);
    qemu_irq cs_line;

    if (!dev || qdev_get_parent_bus(DEVICE(dev))) {
        return 0;
    }

    cs_line = qdev_get_gpio_in_named(DEVICE(dev), SSI_GPIO_CS, 0);
    qdev_set_parent_bus(DEVICE(dev), BUS(arg->bus));
    **arg->cs_linep = cs_line;
    (*arg->cs_linep)++;
    return 0;
}

void ssi_auto_connect_slaves(DeviceState *parent, qemu_irq *cs_line,
                             SSIBus *bus)
{
    SSIAutoConnectArg arg = {
        .cs_linep = &cs_line,
        .bus = bus
    };

    object_child_foreach(OBJECT(parent), ssi_auto_connect_slave, &arg);
}
