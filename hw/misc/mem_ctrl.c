/*
 * This file contains implementation of Memory Controller component.
 * Memory controller is used for managing state of RAM memory regions.
 * Based on pwr_/ret_cntrl inputs it can power up/down or put into
 * retention a RAM memory region.
 *
 * 2014 Aggios, Inc.
 *
 * Written by Strahinja Jankovic <strahinja.jankovic@aggios.com>
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
#include "hw/qdev-properties.h"
#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"

#define TYPE_MEM_CTRL "qemu.memory-controller"

#define MEM_CTRL(obj) \
     OBJECT_CHECK(MemCtrl, (obj), TYPE_MEM_CTRL)
#define MEM_CTRL_PARENT_CLASS \
     object_class_get_parent(object_class_by_name(TYPE_MEM_CTRL))

typedef struct MemCtrl {
    DeviceState parent_obj;
    MemoryRegion *mr_link;
    MemoryRegion pwrddown;
} MemCtrl;

/* Read and write functions when memory region is disabled
 * (either powered down or put into retention).
 */

static uint64_t mem_ctrl_pd_read(void *opaque, hwaddr addr, unsigned size)
{
    MemCtrl *s = MEM_CTRL(opaque);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Error: Memory unavailable (powered down/retained)!\n"
                  "\tAttempted read from %" HWADDR_PRIx "\n",
                  object_get_canonical_path(OBJECT(s)),
                  addr);

    return 0;
}

static void mem_ctrl_pd_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    MemCtrl *s = MEM_CTRL(opaque);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Error: Memory unavailable (powered down/retained)!\n"
                  "\tAttempted write to %" HWADDR_PRIx "=%" PRIx64 "\n",
                  object_get_canonical_path(OBJECT(s)),
                  addr, value);
}

static const MemoryRegionOps mem_ctrl_pd_ops = {
    .read = mem_ctrl_pd_read,
    .write = mem_ctrl_pd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* Power/retention control callbacks. */

static void mem_ctrl_pwr_hlt_cntrl(void *opaque)
{
    DeviceState *dev = DEVICE(opaque);
    MemCtrl *s = MEM_CTRL(opaque);

    memory_region_set_enabled(&s->pwrddown, !dev->ps.active);
}

static void mem_ctrl_pwr_cntrl(void *opaque, int n, int level)
{
    DeviceClass *dc_parent = DEVICE_CLASS(MEM_CTRL_PARENT_CLASS);

    dc_parent->pwr_cntrl(opaque, n, level);
    mem_ctrl_pwr_hlt_cntrl(opaque);

    /* FIXME: Need to trash contents of memory */
}

static void mem_ctrl_hlt_cntrl(void *opaque, int n, int level)
{
    DeviceClass *dc_parent = DEVICE_CLASS(MEM_CTRL_PARENT_CLASS);

    dc_parent->hlt_cntrl(opaque, n, level);
    mem_ctrl_pwr_hlt_cntrl(opaque);
}

static void mem_ctrl_realize(DeviceState *dev, Error **errp)
{
    MemCtrl *s = MEM_CTRL(dev);
    uint64_t mem_size;

    if (!s->mr_link) {
        error_setg(errp, "mr_link not set!\n");
        return;
    }

    mem_size = memory_region_size(s->mr_link);
    memory_region_init_io(&s->pwrddown, OBJECT(dev), &mem_ctrl_pd_ops, s,
                          TYPE_MEM_CTRL, mem_size);
    /* Create pwrddown as subregion */
    memory_region_add_subregion(s->mr_link, 0, &s->pwrddown);
}

static void mem_ctrl_init(Object *obj)
{
    MemCtrl *s = MEM_CTRL(obj);

    /* Link to RAM memory region. */
    object_property_add_link(obj, "mr", TYPE_MEMORY_REGION,
                             (Object **)&s->mr_link,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void mem_ctrl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mem_ctrl_realize;
    dc->pwr_cntrl = mem_ctrl_pwr_cntrl;
    dc->hlt_cntrl = mem_ctrl_hlt_cntrl;
}

static const TypeInfo mem_ctrl_info = {
    .name          = TYPE_MEM_CTRL,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(MemCtrl),
    .class_init    = mem_ctrl_class_init,
    .instance_init = mem_ctrl_init,
};

static void mem_ctrl_register_types(void)
{
    type_register_static(&mem_ctrl_info);
}

type_init(mem_ctrl_register_types)
