/*
 * PCA954X I2C Switch Dummy model
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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "sysemu/blockdev.h"
#include "hw/i2c/pca954x.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#ifndef PCA954X_DEBUG
#define PCA954X_DEBUG 0
#endif

#define DB_PRINT(fmt, args...) do { \
    if (PCA954X_DEBUG) { \
        qemu_log("PCA954X: "fmt, ## args); \
    } \
} while (0);

static pca954x_type known_devices[] = {
    /* I2C Muxes */
    { .name = "pca9542", .lanes = 2, .mux = true },
    { .name = "pca9544", .lanes = 4, .mux = true },
    { .name = "pca9547", .lanes = 8, .mux = true },
    /* I2C Switches */
    { .name = "pca9543", .lanes = 2, .mux = false },
    { .name = "pca9545", .lanes = 4, .mux = false },
    { .name = "pca9546", .lanes = 4, .mux = false },
    { .name = "pca9548", .lanes = 8, .mux = false },
    { .name = "pca9549", .lanes = 8, .mux = false },
};

static void pca954x_reset(DeviceState *dev)
{
    PCA954XState *s = PCA954X(dev);
    I2CSlave *i2cs =  I2C_SLAVE(dev);

    /* Switch decodes the enitre address range, trample any previously set
     * values for address and range
     */
    i2cs->address = 0;
    i2cs->address_range = 0x80;

    s->control_reg = 0;
    s->active_lanes = 0;
}

static uint8_t pca954x_recv(I2CSlave *i2c)
{
    PCA954XState *s = PCA954X(i2c);
    int i;
    int ret = 0;

    if (s->control_decoded) {
        ret |= s->control_reg;
        DB_PRINT("returning control register: %x\n", ret);
    } else {
        for (i = 0; i < s->lanes; ++i) {
            if (s->active_lanes & (1 << i)) {
                ret |= i2c_recv(s->busses[i]);
                DB_PRINT("recieving from active bus %d:%x\n", i, ret);
            }
        }
    }

    return ret;
}

static void pca954x_decode_lane(PCA954XState *s)
{
    if (s->mux) {
        s->active_lanes = (1 << (s->control_reg & (s->lanes - 1)));
    } else {
        s->active_lanes = s->control_reg;
    }
}

static int pca954x_send(I2CSlave *i2c, uint8_t data)
{
    PCA954XState *s = PCA954X(i2c);
    int i;
    int ret = -1;

    if (s->control_decoded) {
        DB_PRINT("setting control register: %x\n", data);
        s->control_reg = data;
        pca954x_decode_lane(s);
        ret = 0;
    } else {
        for (i = 0; i < s->lanes; ++i) {
            if (s->active_lanes & (1 << i)) {
                DB_PRINT("sending to active bus %d:%x\n", i, data);
                ret &= i2c_send(s->busses[i], data);
            }
        }
    }

    return ret;
}

static int pca954x_event(I2CSlave *i2c, enum i2c_event event)
{
    PCA954XState *s = PCA954X(i2c);
    int i;

    s->event = event;
    for (i = 0; i < s->lanes; ++i) {
        if (s->active_lanes & (1 << i)) {
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

    return 0;
}

static int pca954x_decode_address(I2CSlave *i2c, uint8_t address)
{
    PCA954XState *s = PCA954X(i2c);
    int i;
    uint8_t channel_status = 0;

    s->control_decoded = address ==
                    (PCA954X_CONTROL_ADDR | (s->chip_enable & 0x7));

    if (s->control_decoded) {
        return 0;
    }

    if (!s->active_lanes) {
        return 1;
    }

    for (i = 0; i < s->lanes; ++i) {
        if (s->active_lanes & (1 << i)) {
            DB_PRINT("starting active bus %d addr:%02x rnw:%d\n", i, address,
                    s->event == I2C_START_RECV);
            channel_status |= (i2c_start_transfer(s->busses[i], address,
                               s->event == I2C_START_RECV)) << i;
        } else {
            channel_status |= 1 << i;
        }
    }

    if (s->active_lanes & (~channel_status & 0xFF)) {
        return 0;
    }

    return 1;
}

static void pca954x_init(Object *obj)
{
    PCA954XState *s = PCA954X(obj);
    PCA954XClass *sc = PCA954X_GET_CLASS(obj);
    int i;

    if (sc->device) {
        s->mux = sc->device->mux;
        s->lanes = sc->device->lanes;
    } else {
        /* Emulate pca9548 device as default */
        s->mux = false;
        s->lanes = 8;
    }
    for (i = 0; i < s->lanes; ++i) {
        char bus_name[16];

        snprintf(bus_name, sizeof(bus_name), "i2c@%d", i);
        s->busses[i] = i2c_init_bus(DEVICE(s), bus_name);
    }
}

static void pca954x_realize(DeviceState *dev, Error **errp)
{
    /* Dummy */
}

static const VMStateDescription vmstate_PCA954X = {
    .name = "pca954x",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(i2c, PCA954XState),
        VMSTATE_UINT8(control_reg, PCA954XState),
        VMSTATE_BOOL(control_decoded, PCA954XState),
        VMSTATE_UINT8(active_lanes, PCA954XState),
        VMSTATE_UINT8(lanes, PCA954XState),
        VMSTATE_BOOL(mux, PCA954XState),
        VMSTATE_END_OF_LIST()
    }
};

static Property pca954x_properties[] = {
    /* These could be GPIOs, but the application is rare, just let machine model
     * tie them with props
     */
    DEFINE_PROP_UINT8("chip-enable", PCA954XState, chip_enable, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pca954x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    PCA954XClass *sc = PCA954X_CLASS(klass);

    k->event = pca954x_event;
    k->recv = pca954x_recv;
    k->send = pca954x_send;
    k->decode_address = pca954x_decode_address;

    dc->realize = pca954x_realize;
    dc->reset = pca954x_reset;
    dc->vmsd = &vmstate_PCA954X;
    device_class_set_props(dc, pca954x_properties);
    sc->device = data;
}

static TypeInfo pca954x_info = {
    .name          = TYPE_PCA954X,
    .parent        = TYPE_I2C_SLAVE,
    .class_size    = sizeof(PCA954XClass),
    .instance_size = sizeof(PCA954XState),
    .instance_init = pca954x_init,
};

static void pca954x_register_types(void)
{
    int i;

    type_register_static(&pca954x_info);
    for (i = 0; i < ARRAY_SIZE(known_devices); i++) {
        TypeInfo t = {
            .name = known_devices[i].name,
            .parent = TYPE_PCA954X,
            .class_init = pca954x_class_init,
            .class_data = &known_devices[i]
        };
        type_register(&t);
    }
}

type_init(pca954x_register_types)
