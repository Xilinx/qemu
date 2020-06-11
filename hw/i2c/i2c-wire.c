/*
 * QEMU model for I2C Wire
 *
 * Copyright (c) 2019 Xilinx Inc
 * Copyright (c) 2019 Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
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
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/i2c/i2c.h"

#define TYPE_I2C_WIRE "i2c-wire"

#define I2C_WIRE(obj) \
    OBJECT_CHECK(I2CWire, (obj), TYPE_I2C_WIRE)

typedef struct I2CWire I2CWire;

typedef struct I2CWire {
    I2CSlave i2c;

    I2CBus *parent_bus;
    I2CWire *peer;
    bool busy;
    enum i2c_event event;
} I2CWire;

#define I2CWIRE_PEER_BUS(P)     (P->parent_bus)

static inline void i2cWire_peer_busy(I2CWire *s)
{
    s->busy = true;
}

static inline void i2cWire_peer_free(I2CWire *s)
{
     s->busy = false;
}

static inline void i2cWire_busy(I2CWire *s)
{
    i2cWire_peer_busy(s);
    i2cWire_peer_busy(s->peer);
}

static inline void i2cWire_free(I2CWire *s)
{
    i2cWire_peer_free(s);
    i2cWire_peer_free(s->peer);
}

static int i2cWire_send(I2CSlave *slave, uint8_t data)
{
    I2CWire *s = I2C_WIRE(slave);

    return i2c_send(I2CWIRE_PEER_BUS(s->peer), data);
}

static uint8_t i2cWire_recv(I2CSlave *slave)
{
    I2CWire *s = I2C_WIRE(slave);

    return i2c_recv(I2CWIRE_PEER_BUS(s->peer));
}

static int i2cWire_decode_addr(I2CSlave *slave, uint8_t address)
{
    I2CWire *s = I2C_WIRE(slave);
    int r;

    /* Probe the peer device parent-bus */
    if (!s->busy) {
        i2cWire_busy(s);
        r = i2c_start_transfer(I2CWIRE_PEER_BUS(s->peer), address,
                               s->event == I2C_START_RECV);
        i2cWire_free(s);
    } else {
        r = 1; /* wire busy */
    }

    return r;
}

static int i2cWire_event(I2CSlave *slave, enum i2c_event event)
{
    I2CWire *s = I2C_WIRE(slave);

    switch (event) {
    case I2C_START_SEND:
    case I2C_START_RECV:
        break;
    case I2C_FINISH:
        if (!s->busy) {
            i2cWire_busy(s);
            i2c_end_transfer(I2CWIRE_PEER_BUS(s->peer));
            i2cWire_free(s);
        } else {
            return 0;
        }
        break;
    case I2C_NACK:
        if (!s->busy) {
            i2cWire_busy(s);
            i2c_nack(I2CWIRE_PEER_BUS(s->peer));
            i2cWire_free(s);
        } else {
            return 0;
        }
        break;
    }
    s->event = event;
    return 0;
}

static void i2cWire_reset(DeviceState *dev)
{
    I2CWire *s = I2C_WIRE(dev);
    I2CSlave *slave = I2C_SLAVE(dev);

    s->parent_bus = (I2CBus *) qdev_get_parent_bus(dev);
    slave->address = 0;
    slave->address_range = 0x80;
}

static void i2cWire_init(Object *obj)
{
    I2CWire *s = I2C_WIRE(obj);
    object_property_add_link(obj, "i2cWire-peer", TYPE_I2C_WIRE,
                             (Object **)&s->peer,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void i2cWire_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->reset = i2cWire_reset;
    k->decode_address = i2cWire_decode_addr;
    k->recv = i2cWire_recv;
    k->send = i2cWire_send;
    k->event = i2cWire_event;
}

static TypeInfo I2CWire_info = {
    .name = TYPE_I2C_WIRE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(I2CWire),
    .instance_init = i2cWire_init,
    .class_init = i2cWire_class_init,
};

static void i2cWire_register_types(void)
{
    type_register_static(&I2CWire_info);
}

type_init(i2cWire_register_types);
