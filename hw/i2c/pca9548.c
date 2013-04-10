/*
 * PCA9548 I2C Switch Dummy model
 *
 * Copyright (c) 2012 Xilinx Inc.
 * Copyright (c) 2012 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/i2c/i2c.h"
#include "hw/hw.h"
#include "sysemu/blockdev.h"

#ifndef PCA9548_DEBUG
#define PCA9548_DEBUG 0
#endif
#define DB_PRINT(fmt, args...) do {\
    if (PCA9548_DEBUG) {\
        fprintf(stderr, "PCA9548: "fmt, ## args);\
    } \
} while (0);

#define NUM_BUSSES 8
#define pca9548_CONTROL_ADDR 0x74

typedef struct {
    I2CSlave i2c;
    i2c_bus *busses[NUM_BUSSES];

    /*state */
    uint8_t control_reg;
    enum i2c_event event;
    bool control_decoded;

    uint8_t chip_enable; /*property */
} PCA9548State;

#define TYPE_PCA9548 "pca9548"

#define PCA9548(obj) \
     OBJECT_CHECK(PCA9548State, (obj), TYPE_PCA9548)

static void pca9548_reset(DeviceState *dev)
{
    PCA9548State *s = PCA9548(dev);

    s->control_reg = 0;
}

static int pca9548_recv(I2CSlave *i2c)
{
    PCA9548State *s = PCA9548(i2c);
    int i;
    int ret = 0;

    if (s->control_decoded) {
        ret |= s->control_reg;
        DB_PRINT("returning control register: %x\n", ret);
    } else {
        for (i = 0; i < NUM_BUSSES; ++i) {
            if (s->control_reg & (1 << i)) {
                ret |= i2c_recv(s->busses[i]);
                DB_PRINT("recieving from active bus %d:%x\n", i, ret);
            }
        }
    }

    return ret;
}

static int pca9548_send(I2CSlave *i2c, uint8_t data)
{
    PCA9548State *s = PCA9548(i2c);
    int i;
    int ret = -1;

    if (s->control_decoded) {
        DB_PRINT("setting control register: %x\n", data);
        s->control_reg = data;
        ret = 0;
    } else {
        for (i = 0; i < NUM_BUSSES; ++i) {
            if (s->control_reg & (1 << i)) {
                DB_PRINT("sending to active bus %d:%x\n", i, data);
                ret &= i2c_send(s->busses[i], data);
            }
        }
    }

    return ret;
}

static void pca9548_event(I2CSlave *i2c, enum i2c_event event)
{
    PCA9548State *s = PCA9548(i2c);
    int i;

    s->event = event;
    for (i = 0; i < NUM_BUSSES; ++i) {
        if (s->control_reg & (1 << i)) {
            switch (event) {
            /* defer START conditions until we have an address */
            case I2C_START_SEND:
            case I2C_START_RECV:
                break;
            /* Forward others to sub busses */
            case I2C_FINISH:
                if (!s->control_decoded) {
                    DB_PRINT("stopping active bus %d\n", i);
                    i2c_end_transfer(s->busses[i]);
                }
                break;
            case I2C_NACK:
                if (!s->control_decoded) {
                    DB_PRINT("nacking active bus %d\n", i);
                    i2c_nack(s->busses[i]);
                }
                break;
            }
        }
    }
}

static void pca9548_decode_address(I2CSlave *i2c, uint8_t address)
{
    PCA9548State *s = PCA9548(i2c);
    int i;

    s->control_decoded = address ==
                    (pca9548_CONTROL_ADDR | (s->chip_enable & 0x7));
    if (s->control_decoded) {
        return;
    }
    for (i = 0; i < NUM_BUSSES; ++i) {
        if (s->control_reg & (1 << i)) {
            DB_PRINT("starting active bus %d addr:%02x rnw:%d\n", i, address,
                    s->event == I2C_START_RECV);
            i2c_start_transfer(s->busses[i], address,
                               s->event == I2C_START_RECV);
        }
    }
}

static void pca9548_init(Object *obj)
{
    int i;
    PCA9548State *s = PCA9548(obj);

    for (i = 0; i < NUM_BUSSES; ++i) {
        char bus_name[16];

        snprintf(bus_name, sizeof(bus_name), "i2c@%d", i);
        s->busses[i] = i2c_init_bus(DEVICE(s), bus_name);
    }
}

static void pca9548_realize(DeviceState *dev, Error **errp)
{
    I2CSlave *i2cs =  I2C_SLAVE(dev);

    /* Switch decodes the enitre address range, trample any previously set
     * values for address and range
     */
    i2cs->address = 0;
    i2cs->address_range = 0x80;
}

static const VMStateDescription vmstate_PCA9548 = {
    .name = "pca9548",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(i2c, PCA9548State),
        VMSTATE_UINT8(control_reg, PCA9548State),
        VMSTATE_BOOL(control_decoded, PCA9548State),
        VMSTATE_END_OF_LIST()
    }
};

static Property pca9548_properties[] = {
    /* These could be GPIOs, but the application is rare, just let machine model
     * tie them with props
     */
    DEFINE_PROP_UINT8("chip-enable", PCA9548State, chip_enable, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pca9548_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pca9548_event;
    k->recv = pca9548_recv;
    k->send = pca9548_send;
    k->decode_address = pca9548_decode_address;

    dc->realize = pca9548_realize;
    dc->reset = pca9548_reset;
    dc->vmsd = &vmstate_PCA9548;
    dc->props = pca9548_properties;
}

static TypeInfo pca9548_info = {
    .name          = TYPE_PCA9548,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PCA9548State),
    .instance_init = pca9548_init,
    .class_init    = pca9548_class_init,
};

static void pca9548_register_types(void)
{
    type_register_static(&pca9548_info);
}

type_init(pca9548_register_types)
