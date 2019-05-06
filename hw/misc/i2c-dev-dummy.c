/*
 * QEMU model of the XPPU
 *
 * Copyright (c) 2018 Xilinx Inc.
 *
 * Written by Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
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
#include "hw/i2c/i2c.h"
#include "qemu/log.h"

#define TYPE_DUMMY_I2C_DEVICE "i2c-dev-dummy"
#define DEBUG_DUMMY_I2C_DEVICE     0
#define DPRINT(fmt, args...) \
        if (DEBUG_DUMMY_I2C_DEVICE) { \
            qemu_log("%s: " fmt, __func__, ## args); \
        }

static uint8_t dummyi2cdev_rx(I2CSlave *i2c)
{
    return 0;
}

static int dummyi2cdev_tx(I2CSlave *i2c, uint8_t data)
{
    return 0;
}

static void dummyi2cdev_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->recv = dummyi2cdev_rx;
    k->send = dummyi2cdev_tx;
}

static const TypeInfo dummyi2cdev_info = {
    .name = TYPE_DUMMY_I2C_DEVICE,
    .parent = TYPE_I2C_SLAVE,
    .class_init = dummyi2cdev_class_init,
};

static void dummyi2cdev_register_type(void)
{
    type_register_static(&dummyi2cdev_info);
}

type_init(dummyi2cdev_register_type)
