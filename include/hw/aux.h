/*
 * aux.h
 *
 *  Copyright (C)2014 : GreenSocs Ltd
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

#ifndef QEMU_AUX_H
#define QEMU_AUX_H

#include "hw/qdev.h"

enum aux_command {
    WRITE_I2C = 0,
    READ_I2C = 1,
    WRITE_I2C_STATUS = 2,
    WRITE_I2C_MOT = 4,
    READ_I2C_MOT = 5,
    WRITE_AUX = 8,
    READ_AUX = 9
};

enum aux_reply {
    AUX_I2C_ACK = 0,
    AUX_NACK = 1,
    AUX_DEFER = 2,
    AUX_I2C_NACK = 4,
    AUX_I2C_DEFER = 8
};

typedef struct AUXBus AUXBus;
typedef struct AUXSlave AUXSlave;
typedef enum aux_command aux_command;
typedef enum aux_reply aux_reply;

#define TYPE_AUX_SLAVE "aux-slave"
#define AUX_SLAVE(obj) \
     OBJECT_CHECK(AUXSlave, (obj), TYPE_AUX_SLAVE)
#define AUX_SLAVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(AUXSlaveClass, (klass), TYPE_AUX_SLAVE)
#define AUX_SLAVE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(AUXSlaveClass, (obj), TYPE_AUX_SLAVE)

struct AUXSlave {
    /* < private > */
    DeviceState parent_obj;

    /* address of the device on the aux bus. */
    hwaddr address;
    /* memory region associated. */
    MemoryRegion *mmio;
};

typedef struct AUXSlaveClass {
    DeviceClass parent_class;

    /* Callbacks provided by the device.  */
    int (*init)(AUXSlave *dev);
} AUXSlaveClass;

/*
 * \func aux_init_bus
 * \brief Init an aux bus.
 * \param parent The device where this bus is located.
 * \param name The name of the bus.
 * \return The new aux bus.
 */
AUXBus *aux_init_bus(DeviceState *parent, const char *name);

/*
 * \func aux_slave_set_address
 * \brief Set the address of the slave on the aux bus.
 * \param dev The aux slave device.
 * \param address The address to give to the slave.
 */
void aux_set_slave_address(AUXSlave *dev, uint32_t address);

/*
 * \func aux_request
 * \brief Make a request on the bus.
 * \param bus Ths bus where the request happen.
 * \param cmd The command requested.
 * \param address The 20bits address of the slave.
 * \param len The length of the read or write.
 * \param data The data array which will be filled or read during transfer.
 * \return Return the reply of the request.
 */
aux_reply aux_request(AUXBus *bus, aux_command cmd, uint32_t address,
                              uint8_t len, uint8_t *data);

/*
 * \func aux_get_i2c_bus
 * \brief Get the i2c bus for I2C over AUX command.
 * \param bus The aux bus.
 * \return Return the i2c bus associated.
 */
I2CBus *aux_get_i2c_bus(AUXBus *bus);

/*
 * \func aux_init_mmio
 * \brief Init an mmio for an aux slave, must be called after
 *        memory_region_init_io.
 * \param aux_slave The aux slave.
 * \param mmio The mmio to be registered.
 */
void aux_init_mmio(AUXSlave *aux_slave, MemoryRegion *mmio);

DeviceState *aux_create_slave(AUXBus *bus, const char *name, uint32_t addr);

#endif /* !QEMU_AUX_H */
