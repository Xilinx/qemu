/*
 * HWDTB node creation
 *
 * Create the QOM objects associated to the hwdtb nodes
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/hwdtb.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "hw/core/cpu.h"
#include "hw/cpu/cluster.h"
#include "system/memory.h"
#include "migration/vmstate.h"
#include "error.h"
#include "trace.h"

#include <libfdt.h>

static char *get_child_prop_name(HwDtbNode *node)
{
    char *ret;

    ret = g_strdup_printf("hwdtb<%s>", hwdtb_node_get_name(node));

    return ret;
}

/*
 * Select a suitable CPU cluster for @cpu. If @cpu is a child of a cpu-cluster
 * in the tree, the CPU is already correctly parented and we have nothing to do.
 * Otherwise a cluster is selected based on the CPU type.
 */
static Object *select_cluster_for_cpu(HwDtbNode *cpu)
{
    const char *cpu_type;
    Object *ret;

    if (cpu->parent && HWDTB_NODE_AS(cpu->parent, CPU_CLUSTER)) {
        uint32_t cluster_id;

        /*
         * Already parented to a CPU cluster, nothing to do. Keep track of the
         * highest cluster-id value found in the hwdtb. This is used when
         * autocreating CPU clusters to not clash with user selected values.
         */
        if (hwdtb_node_get_prop_uint32(cpu->parent, "cluster-id",
                                       &cluster_id)) {
            cpu->hwdtb->next_cluster_id = MAX(cpu->hwdtb->next_cluster_id,
                                              cluster_id + 1);
        }
        return NULL;
    }

    cpu_type = object_get_typename(hwdtb_get_obj(cpu));

    ret = g_hash_table_lookup(cpu->hwdtb->cpu_clusters, cpu_type);

    if (ret == NULL) {
        g_autofree char *cluster_name = NULL;

        ret = object_new(TYPE_CPU_CLUSTER);

        /* automatic clusters get parented to the machine */
        cluster_name = g_strdup_printf("cpu-cluster[%s]", cpu_type);
        hwdtb_node_add_child_obj(cpu->hwdtb->root, cluster_name, ret);

        g_hash_table_insert(cpu->hwdtb->cpu_clusters, (gpointer) cpu_type, ret);
    }

    return ret;
}

static void parent_child_to_node(HwDtbNode *node, HwDtbNode *child)
{
    g_autofree char *prop = NULL;
    MemoryRegion *mr;

    /* parent the QOM child onto this QOM node */
    prop = get_child_prop_name(child);
    object_property_add_child(hwdtb_get_parenting_obj(node), prop, child->obj);
    object_unref(child->obj);

    /* RAM can only be registered for migration once parenting names it */
    mr = HWDTB_NODE_AS(child, MEMORY_REGION);
    if (mr && memory_region_is_ram(mr) && !qemu_ram_is_shared(mr->ram_block)) {
        vmstate_register_ram_global(mr);
    }
}

static void parent_cpu_child(HwDtbNode *node, HwDtbNode *child)
{
    Object *cluster;
    Object *cpu;
    Object *proxy;
    g_autofree char *prop_name = NULL;

    cluster = select_cluster_for_cpu(child);

    if (cluster) {
        /*
         * The CPU object needs to be parented to this auto-selected cluster.
         * The node object is currently the CPU. Create a proxy to replace
         * it. The proxy will alias the CPU.
         */
        cpu = hwdtb_get_obj(child);
        g_assert(cpu == child->obj);

        /* Parent the CPU to the selected cluster */
        object_property_add_child(cluster, "cpu[*]", cpu);
        object_unref(cpu);

        proxy = hwdtb_create_proxy(cpu, HWDTB_PROXY_LOCAL_OBJ);
        child->obj = proxy;
        trace_hwdtb_node_auto_parent_cpu_to_cluster(child->path,
                                                    object_get_typename(cpu));
    }

    parent_child_to_node(node, child);

}

static void parent_child(HwDtbNode *node, HwDtbNode *child)
{
    if (HWDTB_NODE_AS(child, CPU)) {
        node->hwdtb->num_cpu_found++;
        parent_cpu_child(node, child);
    } else {
        parent_child_to_node(node, child);
    }
}

static void instantiate_and_parent_children(HwDtbNode *node)
{
    HwDtbNode *child;

    hwdtb_node_foreach_child(child, node) {
        /* create the object using the resolved factory */
        child->obj = child->factory(child);
        parent_child(node, child);

        instantiate_and_parent_children(child);
    }
}

void hwdtb_instantiate(HwDtb *hwdtb)
{
    HwDtbNode *root = hwdtb->root;

    /* Use the machine as the root object */
    root->obj = hwdtb_create_proxy(OBJECT(hwdtb->machine),
                                   HWDTB_PROXY_PARENT_ON_OBJ);
    instantiate_and_parent_children(hwdtb->root);
}

static void set_chardev_prop_reserved(HwDtbNode *node)
{
    const char *chardev;
    unsigned int serial_idx;

    chardev = hwdtb_node_get_prop_string(node, "chardev");

    if (chardev == NULL) {
        return;
    }

    if (!strstart(chardev, "serial", NULL)) {
        return;
    }

    if (qemu_strtoui(chardev + strlen("serial"), NULL, 10, &serial_idx)) {
        return;
    }

    if (serial_idx > 31) {
        hwdtb_report_err(node, "chardev value `%s' > 31, not supported",
                         chardev);
        return;
    }

    node->hwdtb->reserved_serial_hd |= (1 << serial_idx);
}


static void hwdtb_set_prop_on_obj(HwDtbNode *node)
{
    Visitor *v;
    ObjectPropertyIterator iter;
    ObjectProperty *prop;
    Object *obj;

    if (hwdtb_is_proxy_to_foreign(node)) {
        /* not created by hwdtb, don't try to set properties on it */
        return;
    }

    v = hwdtb_node_input_visitor_new(node);
    obj = hwdtb_get_obj(node);

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        Error *err = NULL;

        if (!hwdtb_node_has_prop(node, prop->name)) {
            continue;
        }

        if (!prop->set) {
            /* read-only property */
            continue;
        }

        if (!strcmp(prop->name, "chardev")) {
            set_chardev_prop_reserved(node);
        }

        prop->set(obj, v, prop->name, prop->opaque, &err);

        if (err != NULL) {
            hwdtb_report_err(node, "error while setting property %s: %s",
                             prop->name, error_get_pretty(err));
            error_free(err);
        }
    }

    visit_free(v);
}

void hwdtb_set_properties(HwDtb *hwdtb)
{
    hwdtb_walk(hwdtb, hwdtb_set_prop_on_obj);
    hwdtb_attach_block_devs(hwdtb);
    hwdtb_attach_net_devs(hwdtb);
    hwdtb_attach_char_devs(hwdtb);
    hwdtb_attach_remote_ports(hwdtb);
}
