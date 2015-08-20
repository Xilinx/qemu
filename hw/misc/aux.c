/*
 * aux.c
 *
 *  Copyright 2015 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * This is an implementation of the AUX bus for VESA Display Port v1.1a.
 */

#include "hw/aux.h"
#include "hw/i2c/i2c.h"
#include "monitor/monitor.h"

/* #define DEBUG_AUX */

#ifdef DEBUG_AUX
#define DPRINTF(fmt, ...)\
do { printf("aux: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)do {} while (0)
#endif

#define TYPE_AUXTOI2C "aux-to-i2c-bridge"
#define AUXTOI2C(obj) OBJECT_CHECK(AUXTOI2CState, (obj), TYPE_AUXTOI2C)

typedef struct AUXTOI2CState AUXTOI2CState;

struct AUXBus {
    BusState qbus;
    AUXSlave *current_dev;
    AUXSlave *dev;
    uint32_t last_i2c_address;
    aux_command last_transaction;

    AUXTOI2CState *bridge;

    MemoryRegion *aux_io;
    AddressSpace aux_addr_space;
};

static Property aux_props[] = {
    DEFINE_PROP_UINT64("address", struct AUXSlave, address, 0),
    DEFINE_PROP_END_OF_LIST(),
};

#define TYPE_AUX_BUS "aux-bus"
#define AUX_BUS(obj) OBJECT_CHECK(AUXBus, (obj), TYPE_AUX_BUS)

static void aux_slave_dev_print(Monitor *mon, DeviceState *dev, int indent);

static void aux_bus_class_init(ObjectClass *klass, void *data)
{
    /*
     * AUXSlave has an mmio so we need to change the way we print information
     * in monitor.
     */
    BusClass *k = BUS_CLASS(klass);
    k->print_dev = aux_slave_dev_print;
}

static const TypeInfo aux_bus_info = {
    .name = TYPE_AUX_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(AUXBus),
    .class_init = aux_bus_class_init
};

AUXBus *aux_init_bus(DeviceState *parent, const char *name)
{
    AUXBus *bus;

    bus = AUX_BUS(qbus_create(TYPE_AUX_BUS, parent, name));

    /*
     * Create the bridge.
     */
    bus->bridge = AUXTOI2C(qdev_create(BUS(bus), TYPE_AUXTOI2C));

    /*
     * Memory related.
     */
    bus->aux_io = g_malloc(sizeof(*bus->aux_io));
    memory_region_init(bus->aux_io, OBJECT(bus), "aux-io", (1 << 20));
    address_space_init(&bus->aux_addr_space, bus->aux_io, "aux-io");
    return bus;
}

static void aux_bus_map_device(AUXBus *bus, AUXSlave *dev)
{
    memory_region_add_subregion(bus->aux_io, dev->address, dev->mmio);
}

void aux_set_slave_address(AUXSlave *dev, uint32_t address)
{
    qdev_prop_set_uint64(DEVICE(dev), "address", address);
}

static bool aux_bus_is_bridge(AUXBus *bus, DeviceState *dev)
{
    return (dev == DEVICE(bus->bridge));
}

/*
 * Make a native request on the AUX bus.
 */
static aux_reply aux_native_request(AUXBus *bus, aux_command cmd,
                                    uint32_t address, uint8_t len,
                                    uint8_t *data)
{
    /*
     * Transactions on aux address map are 1bytes len time.
     */
    aux_reply ret = AUX_NACK;
    size_t i;

    switch (cmd) {
    case READ_AUX:
        for (i = 0; i < len; i++) {
            if (!address_space_rw(&bus->aux_addr_space, address++, data++,
                                  1, false)) {
                ret = AUX_I2C_ACK;
            } else {
                ret = AUX_NACK;
                break;
            }
        }
    break;
    case WRITE_AUX:
        for (i = 0; i < len; i++) {
            if (!address_space_rw(&bus->aux_addr_space, address++, data++,
                                  1, true)) {
                ret = AUX_I2C_ACK;
            } else {
                ret = AUX_NACK;
                break;
            }
        }
    break;
    default:
        abort();
    break;
    }

    return ret;
}

aux_reply aux_request(AUXBus *bus, aux_command cmd, uint32_t address,
                      uint8_t len, uint8_t *data)
{
    DPRINTF("request at address 0x%5.5X, command %u, len %u\n", address, cmd,
            len);

    int temp;
    aux_reply ret = AUX_NACK;
    I2CBus *i2c_bus = aux_get_i2c_bus(bus);

    switch (cmd) {
    /*
     * Forward the request on the AUX bus..
     */
    case WRITE_AUX:
    case READ_AUX:
        ret = aux_native_request(bus, cmd, address, len, data);
    break;
    /*
     * Classic I2C transactions..
     */
    case READ_I2C:
        if (i2c_bus_busy(i2c_bus)) {
            i2c_end_transfer(i2c_bus);
        }

        if (i2c_start_transfer(i2c_bus, address, 1)) {
            ret = AUX_I2C_NACK;
            break;
        }

        while (len > 0) {
            temp = i2c_recv(i2c_bus);

            if (temp < 0) {
                ret = AUX_I2C_NACK;
                i2c_end_transfer(i2c_bus);
                break;
            }

            *data++ = temp;
            len--;
        }
        i2c_end_transfer(i2c_bus);
        ret = AUX_I2C_ACK;
    break;
    case WRITE_I2C:
        if (i2c_bus_busy(i2c_bus)) {
            i2c_end_transfer(i2c_bus);
        }

        if (i2c_start_transfer(i2c_bus, address, 0)) {
            ret = AUX_I2C_NACK;
            break;
        }

        while (len > 0) {
            if (!i2c_send(i2c_bus, *data++)) {
                ret = AUX_I2C_NACK;
                i2c_end_transfer(i2c_bus);
                break;
            }
            len--;
        }
        i2c_end_transfer(i2c_bus);
        ret = AUX_I2C_ACK;
    break;
    /*
     * I2C MOT transactions.
     *
     * Here we send a start when:
     *  - We didn't start transaction yet.
     *  - We had a READ and we do a WRITE.
     *  - We change the address.
     */
    case WRITE_I2C_MOT:
        if (!i2c_bus_busy(i2c_bus)) {
            /*
             * No transactions started..
             */
            if (i2c_start_transfer(i2c_bus, address, 0)) {
                ret = AUX_I2C_NACK;
                break;
            }
        } else if ((address != bus->last_i2c_address) ||
                   (bus->last_transaction == READ_I2C_MOT)) {
            /*
             * Transaction started but we need to restart..
             */
            i2c_end_transfer(i2c_bus);
            if (i2c_start_transfer(i2c_bus, address, 0)) {
                ret = AUX_I2C_NACK;
                break;
            }
        }

        while (len > 0) {
            if (!i2c_send(i2c_bus, *data++)) {
                ret = AUX_I2C_NACK;
                i2c_end_transfer(i2c_bus);
                break;
            }
            len--;
        }
        bus->last_transaction = WRITE_I2C_MOT;
        bus->last_i2c_address = address;
        ret = AUX_I2C_ACK;
    break;
    case READ_I2C_MOT:
        if (!i2c_bus_busy(i2c_bus)) {
            /*
             * No transactions started..
             */
            if (i2c_start_transfer(i2c_bus, address, 0)) {
                ret = AUX_I2C_NACK;
                break;
            }
        } else if (address != bus->last_i2c_address) {
            /*
             * Transaction started but we need to restart..
             */
            i2c_end_transfer(i2c_bus);
            if (i2c_start_transfer(i2c_bus, address, 0)) {
                ret = AUX_I2C_NACK;
                break;
            }
        }

        while (len > 0) {
            temp = i2c_recv(i2c_bus);

            if (temp < 0) {
                ret = AUX_I2C_NACK;
                i2c_end_transfer(i2c_bus);
                break;
            }

            *data++ = temp;
            len--;
        }
        bus->last_transaction = READ_I2C_MOT;
        bus->last_i2c_address = address;
        ret = AUX_I2C_ACK;
    break;
    default:
        DPRINTF("Not implemented!\n");
        ret = AUX_NACK;
    break;
    }

    DPRINTF("reply: %u\n", ret);
    return ret;
}

/*
 * AUX to I2C bridge.
 */
struct AUXTOI2CState {
    DeviceState parent_obj;
    I2CBus *i2c_bus;
};

I2CBus *aux_get_i2c_bus(AUXBus *bus)
{
    return bus->bridge->i2c_bus;
}

static void aux_bridge_init(Object *obj)
{
    AUXTOI2CState *s = AUXTOI2C(obj);
    /*
     * Create the I2C Bus.
     */
    s->i2c_bus = i2c_init_bus(DEVICE(obj), "aux-i2c");
}

static const TypeInfo aux_to_i2c_type_info = {
    .name = TYPE_AUXTOI2C,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AUXTOI2CState),
    .instance_init = aux_bridge_init
};

/*
 * AUX Slave.
 */
static void aux_slave_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
    AUXBus *bus = AUX_BUS(qdev_get_parent_bus(dev));
    hwaddr size;
    AUXSlave *s;

    /*
     * Don't print anything if the device is I2C "bridge".
     */
    if (aux_bus_is_bridge(bus, dev)) {
        return;
    }

    s = AUX_SLAVE(dev);

    size = memory_region_size(s->mmio);
    monitor_printf(mon, "%*smemory " TARGET_FMT_plx "/" TARGET_FMT_plx "\n",
                   indent, "", s->address, size);
}

static int aux_slave_qdev_init(DeviceState *dev)
{
    AUXSlave *s = AUX_SLAVE(dev);
    AUXSlaveClass *sc = AUX_SLAVE_GET_CLASS(s);

    return sc->init(s);
}

DeviceState *aux_create_slave(AUXBus *bus, const char *name, uint32_t addr)
{
    DeviceState *dev;

    dev = qdev_create(&bus->qbus, name);
    qdev_prop_set_uint64(dev, "address", addr);
    qdev_init_nofail(dev);
    aux_bus_map_device(AUX_BUS(qdev_get_parent_bus(dev)), AUX_SLAVE(dev));
    return dev;
}

void aux_init_mmio(AUXSlave *aux_slave, MemoryRegion *mmio)
{
    aux_slave->mmio = mmio;
}

static void aux_slave_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = aux_slave_qdev_init;
    set_bit(DEVICE_CATEGORY_MISC, k->categories);
    k->bus_type = TYPE_AUX_BUS;
    k->props = aux_props;
}

static const TypeInfo aux_slave_type_info = {
    .name = TYPE_AUX_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AUXSlave),
    .abstract = true,
    .class_size = sizeof(AUXSlaveClass),
    .class_init = aux_slave_class_init,
};

static void aux_slave_register_types(void)
{
    type_register_static(&aux_bus_info);
    type_register_static(&aux_slave_type_info);
    type_register_static(&aux_to_i2c_type_info);
}

type_init(aux_slave_register_types)
