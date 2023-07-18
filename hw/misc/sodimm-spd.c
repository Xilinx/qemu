/*
 * QEMU model of the SO-DIMM SPD EEPROM
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/i2c/i2c.h"
#include "hw/block/m24cxx.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#define TYPE_SODIMM_SPD "sodimm-spd"
#define DEBUG_SODIMM_SPD     0
#define DPRINT(fmt, args...) \
        if (DEBUG_SODIMM_SPD) { \
            qemu_log("%s: " fmt, __func__, ## args); \
        }

#define SODIMM_SPD(obj) \
     OBJECT_CHECK(SodimmSPD, (obj), TYPE_SODIMM_SPD)

#define SODIMM_SPD_CLASS(obj) \
     OBJECT_CLASS_CHECK(SodimmSPDClass, (obj), TYPE_SODIMM_SPD)

#define SODIMM_SPD_GET_CLASS(obj) \
     OBJECT_GET_CLASS(SodimmSPDClass, (obj), TYPE_SODIMM_SPD)


#define SPD_MANF_ID_INDEX 329

typedef struct SodimmSPD {
    M24CXXState parent_obj;

} SodimmSPD;

typedef struct SodimmSPDClass {
    I2CSlaveClass parent_class;

    char manf_id[32];
} SodimmSPDClass;


static void sodimm_spd_reset(DeviceState *dev)
{
    M24CXXState *p = M24CXX(dev);
    SodimmSPDClass *sc = SODIMM_SPD_GET_CLASS(dev);
    /*
     * SPD data
     * Ref: "https://www.micron.com/products/dram-modules/
     *            sodimm/part-catalog/mta4atf51264hz-2g6/
     *            mta4atf51264hz-2g6e1"
     */
    uint8_t spd[512] = {
            0x23, 0x11, 0x0C, 0x3, 0x45, 0x21, 0x0, 0x8,
            0x0, 0x60, 0x0, 0x3, 0x2, 0x3, 0x0, 0x0,
            0x0, 0x0, 0x6, 0x0D, 0xF8, 0xFF, 0x1,
            0x0, 0x6E, 0x6E, 0x6E, 0x11, 0x0, 0x6E, 0xF0,
            0x0A, 0x20, 0x8, 0x0, 0x5, 0x0, 0xF0, 0x2B,
            0x34, 0x28, 0x0, 0x78, 0x0, 0x14, 0x3C,
            [46 ... 59] = 0, 0x16, 0x36, 0x0B, 0x35,
            0x16, 0x36, 0x0B, 0x35, 0x0, 0x0, 0x16, 0x36,
            0x0B, 0x35, 0x16, 0x36, 0x0B, 0x35,
            [78 ... 116] = 0, 0x0, 0x9C, 0xB5, 0x0, 0x0,
            0x0, 0x0, 0xE7, 0x0, 0x5B, 0x60, 0x0F, 0x1,
            0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
            0x0, [139 ... 253] = 0, 0xE2, 0xC0,
            [256 ... 319] = 0, 0x80, 0x2c, 0x0,
            0x0, 0x0, [329 ... 348] = 0, 0x31, 0x80,
            0x2C, 0x45, 0x0, 0x0
    };

    strcpy((char *)&spd[SPD_MANF_ID_INDEX], sc->manf_id);
    memcpy(p->storage, spd, 512);
}

static void sodimm_spd_init(Object *obj)
{
    /* Each SPD block is 128 byte*/
    qdev_prop_set_uint16(DEVICE(obj), "size", 512);
}

static void sodimm_spd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SodimmSPDClass *sc = SODIMM_SPD_CLASS(klass);

    strcpy(sc->manf_id, (char *) data);
    dc->reset = sodimm_spd_reset;
}

static const char *dev_info[] = {
    "4ATF51264HZ-2G6E1",
};

static const TypeInfo sodimm_spd_info = {
    .name = TYPE_SODIMM_SPD,
    .parent = TYPE_M24CXX,
    .instance_size = sizeof(SodimmSPD),
    .class_init = sodimm_spd_class_init,
    .instance_init = sodimm_spd_init,
    .class_size = sizeof(SodimmSPDClass),
    .class_data = &dev_info[0],
};

static void sodimm_spd_register_type(void)
{
    type_register_static(&sodimm_spd_info);
}

type_init(sodimm_spd_register_type)
