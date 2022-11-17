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
#include "qom/object.h"

struct SSIBus {
    BusState parent_obj;
};

#define TYPE_SSI_BUS "SSI"
OBJECT_DECLARE_SIMPLE_TYPE(SSIBus, SSI_BUS)

static const TypeInfo ssi_bus_info = {
    .name = TYPE_SSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SSIBus),
};

static void ssi_cs_default(void *opaque, int n, int level)
{
    SSIPeripheral *s = SSI_PERIPHERAL(opaque);
    bool cs = !!level;
    assert(n == 0);
    if (s->cs != cs) {
        SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(s);
        if (ssc->set_cs) {
            ssc->set_cs(s, cs);
        }
    }
    s->cs = cs;
}

static uint32_t ssi_transfer_raw_default(SSIPeripheral *dev, uint32_t val)
{
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(dev);

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
    SSIPeripheral *s = SSI_PERIPHERAL(obj);
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(s);
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
    qdev_set_parent_bus(DEVICE(s), parent_bus, &error_abort);
    return false;
}

static void ssi_peripheral_realize(DeviceState *dev, Error **errp)
{
    SSIPeripheral *s = SSI_PERIPHERAL(dev);
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_GET_CLASS(s);

    if (ssc->transfer_raw == ssi_transfer_raw_default &&
            ssc->cs_polarity != SSI_CS_NONE) {
        qdev_init_gpio_in_named(dev, ssi_cs_default, SSI_GPIO_CS, 1);
    }

    ssc->realize(s, errp);
}

static void ssi_peripheral_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *ssc = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_CLASS(klass);

    dc->realize = ssi_peripheral_realize;
    dc->bus_type = TYPE_SSI_BUS;
    if (!ssc->transfer_raw) {
        ssc->transfer_raw = ssi_transfer_raw_default;
    }
    fmc->parse_reg = ssi_slave_parse_reg;
}

static const TypeInfo ssi_peripheral_info = {
    .name = TYPE_SSI_PERIPHERAL,
    .parent = TYPE_DEVICE,
    .class_init = ssi_peripheral_class_init,
    .class_size = sizeof(SSIPeripheralClass),
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

DeviceState *ssi_create_peripheral(SSIBus *bus, const char *name)
{
    DeviceState *dev = qdev_new(name);

    ssi_realize_and_unref(dev, bus, &error_fatal);
    return dev;
}

SSIBus *ssi_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
    bus = qbus_new(TYPE_SSI_BUS, parent, name);
    return SSI_BUS(bus);
}

uint32_t ssi_transfer(SSIBus *bus, uint32_t val)
{
    BusState *b = BUS(bus);
    BusChild *kid;
    SSIPeripheralClass *ssc;
    uint32_t r = 0;

    QTAILQ_FOREACH(kid, &b->children, sibling) {
        SSIPeripheral *peripheral = SSI_PERIPHERAL(kid->child);
        ssc = SSI_PERIPHERAL_GET_CLASS(peripheral);
        r |= ssc->transfer_raw(peripheral, val);
    }

    return r;
}

const VMStateDescription vmstate_ssi_peripheral = {
    .name = "SSISlave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(cs, SSIPeripheral),
        VMSTATE_END_OF_LIST()
    }
};

static void ssi_peripheral_register_types(void)
{
    type_register_static(&ssi_bus_info);
    type_register_static(&ssi_peripheral_info);
}

typedef struct SSIAutoConnectArg {
    qemu_irq **cs_linep;
    SSIBus *bus;
} SSIAutoConnectArg;

static int ssi_auto_connect_peripheral(Object *child, void *opaque)
{
    SSIAutoConnectArg *arg = opaque;
    SSIPeripheral *dev = (SSIPeripheral *)object_dynamic_cast(child,
                                                          TYPE_SSI_PERIPHERAL);
    qemu_irq cs_line;

    if (!dev || qdev_get_parent_bus(DEVICE(dev))) {
        return 0;
    }

    cs_line = qdev_get_gpio_in_named(DEVICE(dev), SSI_GPIO_CS, 0);
    qdev_set_parent_bus(DEVICE(dev), BUS(arg->bus), &error_abort);
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

    object_child_foreach(OBJECT(parent), ssi_auto_connect_peripheral, &arg);
}

type_init(ssi_peripheral_register_types)
