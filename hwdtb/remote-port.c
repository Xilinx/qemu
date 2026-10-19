/*
 * HWDTB remote port device bindings
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/hwdtb.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "hw/remote-port.h"
#include "hw/remote-port-device.h"
#include "qapi/error.h"
#include "error.h"
#include "trace.h"
#include "system/system.h"

static bool node_get_rp_descr(HwDtbNode *node, size_t idx,
                              uint32_t *rp_phandle, uint32_t *chan_idx)
{
    if (!hwdtb_node_get_prop_nth_uint32(node, "remote-ports", idx * 2,
                                        rp_phandle)) {
        return false;
    }

    if (!hwdtb_node_get_prop_nth_uint32(node, "remote-ports", idx * 2 + 1,
                                        chan_idx)) {
        return false;
    }

    return true;
}

static void node_attach_remote_port(HwDtbNode *node)
{
    Object *obj;
    size_t i = 0;
    uint32_t rp_phandle, chan_idx;
    g_autoptr(GString) prop_name = g_string_new("");

    if (!HWDTB_NODE_AS(node, REMOTE_PORT_DEVICE)) {
        return;
    }

    obj = hwdtb_get_obj(node);

    while (node_get_rp_descr(node, i, &rp_phandle, &chan_idx)) {
        HwDtbNode *rp_node = hwdtb_get_node_by_phandle(node->hwdtb, rp_phandle);
        Object *rp;

        if (rp_node == NULL) {
            hwdtb_report_err(node, HWDTB_ERR_PHANDLE_NOT_FOUND, "remote-ports",
                             rp_phandle);
            i++;
            continue;
        }

        if (!HWDTB_NODE_AS(rp_node, REMOTE_PORT)) {
            hwdtb_report_err(rp_node, HWDTB_ERR_TYPE_MISMATCH,
                             TYPE_REMOTE_PORT);
            i++;
            continue;
        }

        rp = hwdtb_get_obj(rp_node);
        g_string_printf(prop_name, "rp-adaptor%zu", i);
        object_property_set_link(obj, prop_name->str, rp, &error_abort);

        g_string_printf(prop_name, "rp-chan%zu", i);
        object_property_set_int(obj, prop_name->str, chan_idx, NULL);

        g_string_printf(prop_name, "remote-port-dev%" PRIu32, chan_idx);
        object_property_set_link(rp, prop_name->str, obj, &error_abort);

        trace_hwdtb_node_remote_port_dev_bind(node->path, i, rp_node->path,
                                              chan_idx);
        i++;
    }
}

static void remote_port_set_sync_quantum(HwDtbNode *node)
{
    DeviceState *dev = DEVICE(hwdtb_get_obj(node));

    if (!hwdtb_node_has_prop(node, "sync-quantum") && global_sync_quantum) {
        qdev_prop_set_uint64(dev, "sync-quantum", global_sync_quantum);
    }
}

/*
 * Set the remote port prefix property to what it would have been in the legacy
 * code. It used to inherit from the QOM path of the remote port object. To keep
 * backward compatibility with existing scripts hardcoding the remote port
 * socket name, reproduce the legacy prefix here. Reconstruct the same prefix by
 * ignoring all the containers in the QOM hierarchy, and by using the hwdtb node
 * names instead of the QOM ones.
 */
static void remote_port_set_legacy_prefix(HwDtbNode *node)
{
    DeviceState *dev = DEVICE(hwdtb_get_obj(node));
    g_autoptr(GString) legacy_prefix = g_string_new(hwdtb_node_get_name(node));

    while (node->parent) {
        HwDtbNode *parent = node->parent;

        if (parent->parent == NULL) {
            g_string_prepend_c(legacy_prefix, '/');
        } else if (!object_dynamic_cast(hwdtb_get_obj(parent),
                                        TYPE_CONTAINER)) {
            g_string_prepend_c(legacy_prefix, '/');
            g_string_prepend(legacy_prefix, hwdtb_node_get_name(parent));
        }

        node = node->parent;
    }

    qdev_prop_set_string(dev, "prefix", legacy_prefix->str);
}

static void handle_remote_port_obj(HwDtbNode *node)
{
    if (HWDTB_NODE_AS(node, REMOTE_PORT_DEVICE)) {
        node_attach_remote_port(node);
    } else if (HWDTB_NODE_AS(node, REMOTE_PORT)) {
        remote_port_set_sync_quantum(node);
        remote_port_set_legacy_prefix(node);
    }
}

void hwdtb_attach_remote_ports(HwDtb *hwdtb)
{
    hwdtb_walk(hwdtb, handle_remote_port_obj);
}

