/*
 * Tiny device allowing reset of all devices mapped to a given MR.
 *
 * Copyright (c) 2018 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#ifndef RESET_DOMAIN_DEBUG
#define RESET_DOMAIN_DEBUG 0
#endif

#define DPRINT(args, ...) \
    do { \
        if (RESET_DOMAIN_DEBUG) { \
            printf("%s: " args, __func__, ## __VA_ARGS__); \
        } \
    } while (0)


#define TYPE_RESET_DOMAIN "qemu.reset-domain"
#define RESET_DOMAIN(obj) \
        OBJECT_CHECK(ResetDomain, (obj), TYPE_RESET_DOMAIN)

#define MAX_RESET_MR 16

typedef struct ResetDomain {
    DeviceState parent;

    struct {
        uint16_t max_alias_depth;
    } cfg;
    MemoryRegion *mr[MAX_RESET_MR];
} ResetDomain;

static void reset_mr(ResetDomain *s, MemoryRegion *mr, int level)
{
    Object *obj_owner;
    DeviceState *dev_owner;
    const MemoryRegion *submr;

    QTAILQ_FOREACH(submr, &mr->subregions, subregions_link) {
        if (submr->alias) {
            DPRINT("\n** ALIAS %s level=%d max=%d\n",
                   memory_region_name(submr),
                   level, s->cfg.max_alias_depth);
            if (level < s->cfg.max_alias_depth) {
                reset_mr(s, submr->alias, level + 1);
            }
            continue;
        }
        obj_owner = memory_region_owner((MemoryRegion *)submr);
        if (!object_dynamic_cast(obj_owner, TYPE_DEVICE)) {
            /* Cannot reset non-device objects.  */
            continue;
        }

        dev_owner = DEVICE(obj_owner);
        DPRINT("MR %s RESET owner %s\n",
               memory_region_name(submr), dev_owner->id);
        qdev_reset_all(dev_owner);
    }
}


static void reset_reset(DeviceState *dev)
{
    ResetDomain *s = RESET_DOMAIN(dev);
    int i;

    DPRINT("\n\n");
    DPRINT("****** RESET DOMAIN %s *****\n", dev->id);
    for (i = 0; i < MAX_RESET_MR; i++) {
        if (s->mr[i]) {
            reset_mr(s, s->mr[i], 0);
        }
    }
    DPRINT("\n\n");
}

static void reset_init(Object *obj)
{
    ResetDomain *s = RESET_DOMAIN(obj);
    int i;

    for (i = 0; i < MAX_RESET_MR; i++) {
        char mr_name[16];

        snprintf(mr_name, 16, "mr%d", i);
        object_property_add_link(obj, mr_name, TYPE_MEMORY_REGION,
                             (Object **)&s->mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    }
}

static Property reset_props[] = {
    /*
     * When we reset an MR, the MR may have aliasing regions pointing to other
     * memory-regions. If an alias is encounterd we recurse and start resetting
     * devices within the alias region. The alias region may in turn have
     * aliases. The max_alias_level controls the max depth of recursion.
     */
    DEFINE_PROP_UINT16("max-alias-depth", ResetDomain, cfg.max_alias_depth, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void reset_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = reset_reset;
    device_class_set_props(dc, reset_props);
}

static const TypeInfo reset_info = {
    .name          = TYPE_RESET_DOMAIN,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(ResetDomain),
    .class_init    = reset_class_init,
    .instance_init = reset_init,
};

static void reset_register_types(void)
{
    type_register_static(&reset_info);
}

type_init(reset_register_types)
