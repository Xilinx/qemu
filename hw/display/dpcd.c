/*
 * dpcd.c
 *
 *  Copyright (C)2015 : GreenSocs Ltd
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
 * This is a simple AUX slave which emulate a screen connected.
 */

#include "qemu/osdep.h"
#include "hw/misc/aux.h"
#include "dpcd.h"

/* #define DEBUG_DPCD */
#ifdef DEBUG_DPCD
#define DPRINTF(fmt, ...) do { printf("dpcd: "fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

struct DPCDState {
    AUXSlave parent_obj;

    size_t current_reg;
    /*
     * The DCPD is 0x7FFFF length but read as 0 after offset 0x600.
     */
    uint8_t dpcd_info[0x600];

    MemoryRegion iomem;
};

static void dpcd_realize(DeviceState *dev, Error **errp)
{

}

static uint64_t aux_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t ret;
    DPCDState *e = DPCD(opaque);
    assert(size == 1);

    if (offset <= 0x600) {
        ret = e->dpcd_info[offset];
    } else {
        ret = 0;
    }

    DPRINTF("read %u @0x%8.8lX\n", (uint8_t)ret, offset);
    return ret;
}

static void aux_write(void *opaque, hwaddr offset, uint64_t value,
                      unsigned size)
{
    DPCDState *e = DPCD(opaque);
    assert(size == 1);

    DPRINTF("write %u @0x%8.8lX\n", (uint8_t)value, offset);

    if (offset <= 0x600) {
        e->dpcd_info[offset] = value;
    }
}

static const MemoryRegionOps aux_ops = {
    .read = aux_read,
    .write = aux_write
};

static void aux_edid_init(Object *obj)
{
    /*
     * Create a default DPCD..
     */
    DPCDState *s = DPCD(obj);

    memset(&(s->dpcd_info), 0, sizeof(s->dpcd_info));

    s->current_reg = 0;

    s->dpcd_info[0x00] = DPCD_REV_1_0;
    s->dpcd_info[0x01] = DPCD_2_7GBPS;
    s->dpcd_info[0x02] = 0x1;
    s->dpcd_info[0x08] = DPCD_EDID_PRESENT;
    s->dpcd_info[0x09] = 0xFF;

    /* CR DONE, CE DONE, SYMBOL LOCKED.. */
    s->dpcd_info[0x202] = 0x07;
    /* INTERLANE_ALIGN_DONE.. */
    s->dpcd_info[0x204] = 0x01;
    s->dpcd_info[0x205] = 0x01;

    /*
     * Create the address-map.
     */
    memory_region_init_io(&s->iomem, obj, &aux_ops, s, TYPE_DPCD, 0x7FFFF);
    aux_init_mmio(AUX_SLAVE(obj), &s->iomem);
}

static void aux_edid_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = dpcd_realize;
}

static const TypeInfo aux_edid_info = {
    .name          = TYPE_DPCD,
    .parent        = TYPE_AUX_SLAVE,
    .instance_size = sizeof(DPCDState),
    .instance_init = aux_edid_init,
    .class_init    = aux_edid_class_init,
};

static void aux_edid_register_types(void)
{
    type_register_static(&aux_edid_info);
}

type_init(aux_edid_register_types)
