/*
 * HWDTB node resolver
 *
 * Try to resolve a node compatible string property to a QOM type and/or a
 * factory function
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/hwdtb.h"
#include "qom/object.h"
#include "trace.h"

#include <libfdt.h>

static const char *skip_digits_until(const char *str, char delim)
{
    do {
        if (!isdigit(*str)) {
            return NULL;
        }

        str++;
    } while (*str && (*str != delim));

    return str;
}

static bool is_xilinx_version_suffix(const char *suffix)
{
    suffix++;

    /* [0-9]+\. */
    suffix = skip_digits_until(suffix, '.');

    if (suffix == NULL) {
        return false;
    }

    if (*suffix == '\0') {
        return false;
    }

    suffix++;

    /* [0-9]+\.? */
    suffix = skip_digits_until(suffix, '.');

    if (suffix == NULL) {
        return false;
    }

    if (*suffix == '\0') {
        /* matched [0-9]+\.[0-9]+ */
        return true;
    }

    suffix++;

    if (*suffix != 'a') {
        return false;
    }

    /* matched [0-9]+\.[0-9]+\.a */
    return true;
}

static bool is_arm_version_suffix(const char *suffix)
{
    suffix++;

    if (*suffix++ != 'r') {
        return false;
    }

    suffix = skip_digits_until(suffix, 'p');

    if ((suffix == NULL) || (*suffix == '\0')) {
        return false;
    }

    suffix++;
    suffix = skip_digits_until(suffix, '\0');

    if (suffix == NULL) {
        return false;
    }

    return true;
}

/*
 * @return a new string with the following suffix stripped:
 *     - -[0-9]+\.[0-9]+(\.a)? (Xilinx-style version suffix)
 *     - -r[0-9]+p[0-9]+ (ARM-style version suffix)
 *   or NULL if no such suffixes are found.
 */
static const char *strip_version_suffix(const char *compat)
{
    const char *start = strrchr(compat, '-');
    char *ret = NULL;

    if (start == NULL) {
        return NULL;
    }

    if (!is_xilinx_version_suffix(start) && !is_arm_version_suffix(start)) {
        return NULL;
    }

    ret = g_new0(char, start - compat + 1);
    memcpy(ret, compat, start - compat);

    return ret;
}

/*
 * @return a new string with the first part of the compatible string
 * stripped until the first comma (included), or NULL if no such prefix is
 * found.
 */
static const char *strip_vendor_prefix(const char *compat)
{
    const char *start = strchr(compat, ',');

    if (start == NULL) {
        return NULL;
    }

    if (start[1] == '\0') {
        return NULL;
    }

    return g_strdup(start + 1);
}

static const char *substitute_char(const char *compat, char from, char to)
{
    const char *found = strchr(compat, from);
    char *ret, *needle;

    if (found == NULL) {
        return NULL;
    }

    ret = g_strdup(compat);
    needle = strchr(ret, from);

    while (needle) {
        *needle = to;
        needle = strchr(ret, from);
    }

    return ret;
}

/*
 * -- Legacy --
 * legacy compatible string transformations/expansion:
 *
 *   - strip version:
 *       xxxxx-2.0 -> xxxxx   (Xilinx style)
 *       xxxxx-r2p0 -> xxxxx  (ARM style)
 *   - strip vendor:
 *       vendor,dev -> dev
 *   - replace ',' and '.' with '-'
 *   - replace '.' with '-'
 *
 * Example: expansion of the xlnx,ps7-can-1.00.a compatible string:
 *   xlnx,ps7-can-1.00.a
 *   xlnx,ps7-can
 *   ps7-can-1.00.a
 *   ps7-can
 *   xlnx.ps7-can-1.00.a
 *   xlnx.ps7-can
 *   xlnx-ps7-can-1.00.a
 *   xlnx-ps7-can
 */
static void compat_expand(const char *compat, GArray *compats)
{
    const char *s;
    size_t i, cur_len;

    g_array_append_val(compats, compat);

    s = strip_version_suffix(compat);
    if (s) {
        g_array_append_val(compats, s);
    }

    cur_len = compats->len;
    for (i = 0; i < cur_len; i++) {
        s = strip_vendor_prefix(g_array_index(compats, const char *, i));

        if (s) {
            g_array_append_val(compats, s);
        }
    }

    cur_len = compats->len;
    for (i = 0; i < cur_len; i++) {
        s = substitute_char(g_array_index(compats, const char *, i), ',', '.');

        if (s) {
            g_array_append_val(compats, s);
        }

        s = substitute_char(g_array_index(compats, const char *, i), ',', '-');

        if (s) {
            g_array_append_val(compats, s);
        }

        s = substitute_char(g_array_index(compats, const char *, i), '.', '-');

        if (s) {
            g_array_append_val(compats, s);
        }
    }
}

static void populate_compatibles(HwDtbNode *node, GArray *compats,
                                const char *compat)
{
    g_autoptr(GArray) expanded = g_array_new(false, true, sizeof(const char *));

    compat_expand(g_strdup(compat), expanded);
    g_array_append_vals(compats, expanded->data, expanded->len);
}

static void parse_compatible(HwDtbNode *node)
{
    g_autoptr(GArray) compats = g_array_new(false, true, sizeof(const char *));
    const char *compat;
    size_t i;

    /* Default oc and factory: create a dummy container */
    node->oc = object_class_by_name(TYPE_CONTAINER);
    node->factory = hwdtb_get_default_factory();

    compat = hwdtb_node_get_prop_strings(node, "compatible", NULL);

    while (compat) {
        populate_compatibles(node, compats, compat);
        compat = hwdtb_node_get_prop_strings(node, "compatible", compat);
    }

    if (compats->len == 0) {
        return;
    }

    for (i = 0; i < compats->len; i++) {
        char *c = g_array_index(compats, char *, i);
        const char *translated = hwdtb_compat_translate(c);

        if (translated != NULL) {
            g_free(c);
            g_array_index(compats, char *, i) = g_strdup(translated);
        }
    }

    if (trace_event_get_state_backends(TRACE_HWDTB_NODE_RESOLVE_CANDIDATES)) {
        g_autoptr(GString) candidates = g_string_new("");

        for (i = 0; i < compats->len; i++) {
            const char *c = g_array_index(compats, const char *, i);

            g_string_append_printf(candidates, "%s%s",
                                   i ? ", " : "",
                                   c);
        }

        trace_hwdtb_node_resolve_candidates(node->path, candidates->str);
    }

    for (i = 0; i < compats->len; i++) {
        const char *c = g_array_index(compats, const char *, i);
        ObjectClass *oc;
        HwDtbObjectFactory factory;

        oc = object_class_by_name(c);
        factory = hwdtb_get_factory_for_compat(c);

        if (oc || factory) {
            node->oc = oc ?: node->oc;
            node->factory = factory ?: node->factory;
            trace_hwdtb_node_resolve_success(node->path, c);
            return;
        }
    }

    trace_hwdtb_node_resolve_failure(node->path);
}

void hwdtb_resolve(HwDtb *hwdtb)
{
    hwdtb_walk(hwdtb, parse_compatible);
}
