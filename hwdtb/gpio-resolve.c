/*
 * HWDTB GPIO resolve pass
 *
 * This pass tries to resolve GPIO connections by identifying inputs and outputs
 * on the devices.
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/hwdtb.h"
#include "hw/sysbus.h"
#include "hw/fdt_generic_util.h"
#include "trace.h"

static const char *CONN_GPIO_NAMESPACE[] = {
    [HWDTB_CON_INTERRUPT] = SYSBUS_DEVICE_GPIO_IRQ,
    [HWDTB_CON_GPIO] = NULL,
    [HWDTB_CON_POWER_GPIO] = "power",
    [HWDTB_CON_RESET_GPIO] = "reset",
    [HWDTB_CON_INTERRUPT_GPIO] = NULL,
    [HWDTB_CON_ERROR_OUT_GPIO] = NULL,
    [HWDTB_CON_PWR_STATE_GPIO] = NULL,
};

static void conn_resolve_client(HwDtbNode *node, HwDtbConnection *conn)
{
    const char *name;
    size_t idx;

    if (hwdtb_gpio_is_resolved(&conn->gpio)) {
        return;
    }

    name = conn->name ?: CONN_GPIO_NAMESPACE[conn->kind];

    /*
     * When a gpio namespace is provided using a *-names property, the index is
     * always 0. The cell index in the specifier property is ignored.
     *
     * E.g.:
     *    foo {
     *       gpios = <&bar 0 &baz 3>;
     *       gpio-names = "foo", "bar";
     *    }
     *
     * In this case we should look for GPIO foo[0] and bar[0] on the foo node.
     */
    idx = conn->name ? 0 : conn->idx;

    if (hwdtb_node_has_gpio_output(node, name, idx)) {
        conn->gpio.sta = HWDTB_GPIO_OUTPUT;
    } else if (hwdtb_node_has_gpio_input(node, name, idx)) {
        /*
         * -- Legacy --
         * Reversed direction
         */
        conn->gpio.sta = HWDTB_GPIO_INPUT;
    } else {
        qemu_log_mask(LOG_FDT,
                      "%s: GPIO resolution failure: GPIO [%s, %zu] not found\n",
                      node->path, name, idx);
        conn->gpio.sta = HWDTB_GPIO_RESOLUTION_FAILURE;
        return;
    }

    conn->gpio.idx = idx;
    conn->gpio.name = name;

    trace_hwdtb_node_gpio_resolve(node->path,
                                  hwdtb_gpio_get_resolution_str(&conn->gpio),
                                  conn->gpio.name ?: "unnamed-gpio",
                                  conn->gpio.idx);
}

static void target_resolve(HwDtbConnection *conn, HwDtbConnectionTarget *target)
{
    size_t idx;

    if (hwdtb_gpio_is_resolved(&target->gpio)) {
        return;
    }

    if (conn->kind == HWDTB_CON_INTERRUPT &&
        HWDTB_NODE_AS(target->target, FDT_GENERIC_INTC)) {
        size_t i;

        trace_hwdtb_node_gpio_resolve_legacy_intc(target->target->path);
        target->gpio.sta = HWDTB_GPIO_LEGACY_INTC;

        /*
         * The index in the resolved GPIO is unused when resolved as a
         * HWDTB_GPIO_LEGACY_INTC. However the HwDtbRegisteredGPIO hash function
         * uses it to hash the entry. Set it to something derived from to the tuple
         * value.
         */
        target->gpio.idx = 0;
        for (i = 0; i < target->tuple->len; i++) {
            uint32_t v = g_array_index(target->tuple, uint32_t, i);
            target->gpio.idx |= v << (i * 8);
        }

        return;
    }

    /*
     * -- Legacy --
     * #gpio-cells > 1 does not make much sense in the hwdtb usecase. However in
     * some legacy dtbs, some gpio-controller nodes have a #gpio-cells = 2. The
     * second element of the tuple is always 0 and is ignored by the legacy
     * fdt_generic code.
     *
     * We want to get rid of those and only support #gpio-cells = <1>
     */
    if (target->tuple->len == 0) {
        target->gpio.sta = HWDTB_GPIO_RESOLUTION_FAILURE;
        return;
    }

    idx = g_array_index(target->tuple, uint32_t, 0);

    if (hwdtb_node_has_gpio_input(target->target, NULL, idx)) {
        target->gpio.sta = HWDTB_GPIO_INPUT;
    } else if (hwdtb_node_has_gpio_output(target->target, NULL, idx)) {
        /*
         * -- Legacy --
         * Reversed direction
         */
        target->gpio.sta = HWDTB_GPIO_OUTPUT;
    } else {
        qemu_log_mask(LOG_FDT,
                      "%s: GPIO resolution failure: GPIO "
                      "[unnamed-gpio, %zu] not found\n",
                      target->target->path, idx);
        target->gpio.sta = HWDTB_GPIO_RESOLUTION_FAILURE;
        return;
    }

    target->gpio.idx = idx;
    target->gpio.name = NULL; /* unnamed gpio */

    trace_hwdtb_node_gpio_resolve(target->target->path,
                                  hwdtb_gpio_get_resolution_str(&target->gpio),
                                  "unnamed-gpio",
                                  target->gpio.idx);
}

/*
 * -- Legacy --
 * Since GPIO connections do not describe their direction, we can end up in
 * ambiguous cases where a GPIO exists both as an input and an output on a given
 * device.
 *
 * This function tries to resolve those cases by:
 *    - finding connections resolved as in - in or out - out,
 *    - trying to resolve them as in - out or out - in connections.
 *
 * Note that if a connection can both be resolved as in - out and out - in,
 * there is not much we can do in term of guessing what the user wanted in the
 * first place...
 *
 * This heuristic can go away once we deprecate those non-directionnal
 * connections in hwdtbs.
 */
static void conn_legacy_deambiguous(HwDtbNode *node, HwDtbConnection *conn)
{
    HwDtbConnectionTarget *target;

    if (conn->targets->len != 1) {
        /*
         * Don't bother with multiple targets connections. Those are limited to
         * HWDTB_CON_INTERRUPT connections which don't have this direction
         * resolution issue (they are always described in the out -> in
         * direction in dtbs)
         */
        return;
    }

    target = &g_array_index(conn->targets, HwDtbConnectionTarget, 0);

    if ((conn->gpio.sta == HWDTB_GPIO_OUTPUT) &&
        (target->gpio.sta == HWDTB_GPIO_OUTPUT)) {
        /* try with client as an input */
        if (hwdtb_node_has_gpio_input(node, conn->gpio.name, conn->gpio.idx)) {
            trace_hwdtb_node_legacy_gpio_deambiguous(
                node->path, conn->gpio.name ?: "unnamed-gpio", conn->gpio.idx,
                "input");
            conn->gpio.sta = HWDTB_GPIO_INPUT;
        }
    }

    if ((conn->gpio.sta == HWDTB_GPIO_INPUT) &&
        (target->gpio.sta == HWDTB_GPIO_INPUT)) {
        /* try with controller as an input */
        if (hwdtb_node_has_gpio_output(target->target, target->gpio.name,
                                       target->gpio.idx)) {
            trace_hwdtb_node_legacy_gpio_deambiguous(
                target->target->path, target->gpio.name ?: "unnamed-gpio",
                target->gpio.idx, "output");
            target->gpio.sta = HWDTB_GPIO_OUTPUT;
        }
    }

}

static void conn_resolve(HwDtbNode *node, HwDtbConnection *conn)
{
    size_t i;

    conn_resolve_client(node, conn);

    for (i = 0; i < conn->targets->len; i++) {
        HwDtbConnectionTarget *target;

        target = &g_array_index(conn->targets, HwDtbConnectionTarget, i);
        target_resolve(conn, target);
    }

    conn_legacy_deambiguous(node, conn);
}

static void node_conns_resolve(HwDtbNode *node, HwDtbConnectionKind kind)
{
    HwDtbConnection *conn;

    hwdtb_node_foreach_connection(conn, node, kind) {
        conn_resolve(node, conn);
    }
}

static void hwdtb_node_gpio_resolve(HwDtbNode *node)
{
    HwDtbNode *child;
    size_t i;

    for (i = 0; i < HWDTB_NUM_GPIO_CON; i++) {
        node_conns_resolve(node, i);
    }

    hwdtb_node_foreach_child(child, node) {
        hwdtb_node_gpio_resolve(child);
    }
}

void hwdtb_gpio_resolve(HwDtb *hwdtb)
{
    hwdtb_node_gpio_resolve(hwdtb->root);
}

typedef QSIMPLEQ_HEAD(ToBeReversedConnList, HwDtbConnection) ToBeReversedConnList;

static void save_node_in_connection(HwDtbNode *node, HwDtbConnection *conn)
{
    HwDtbConnectionTarget tmp_target;

    /*
     * The connection is going to be removed from the node connection list to be
     * put in the temporary "to be reversed" list. We need to keep track of what
     * node this connection belongs to. Abuse the target array of the connection
     * for that. This temporary cell is pop'ed in pop_node_from_connection.
     */
    tmp_target.target = node;
    g_array_append_val(conn->targets, tmp_target);
}

static HwDtbNode *pop_node_from_connection(HwDtbConnection *conn)
{
    HwDtbNode *ret;

    ret = g_array_index(conn->targets, HwDtbConnectionTarget,
                        conn->targets->len - 1).target;
    g_array_set_size(conn->targets, conn->targets->len - 1);

    return ret;
}

static bool gpio_eq(const HwDtbResolvedGPIO *a, const HwDtbResolvedGPIO *b)
{
    if (a->sta != b->sta) {
        return false;
    }

    if (a->idx != b->idx) {
        return false;
    }

    if ((a->name == NULL) && (b->name == NULL)) {
        return true;
    }

    if ((a->name == NULL) || (b->name == NULL)) {
        return false;
    }

    return !strcmp(a->name, b->name);
}

static HwDtbConnection *find_connection_matching_gpio(HwDtbNode *node,
                                                      HwDtbResolvedGPIO *gpio,
                                                      HwDtbConnectionKind kind)
{
    HwDtbConnection *conn;

    hwdtb_node_foreach_connection(conn, node, kind) {
        if (gpio_eq(&conn->gpio, gpio)) {
            return conn;
        }
    }

    return NULL;
}

static void conn_reverse_direction(HwDtbConnection *conn)
{
    HwDtbNode *node, *new_client;
    HwDtbConnectionTarget *target;
    size_t new_idx;
    HwDtbResolvedGPIO new_gpio;
    HwDtbConnection *matching_conn;
    HwDtbConnectionKind kind = conn->kind;

    node = pop_node_from_connection(conn);
    g_assert(conn->targets->len == 1);

    target = &g_array_index(conn->targets, HwDtbConnectionTarget, 0);

    new_client = target->target;
    new_gpio = target->gpio;
    new_idx = g_array_index(target->tuple, uint32_t, 0);

    target->target = node;
    g_array_index(target->tuple, uint32_t, 0) = conn->idx;
    target->gpio = conn->gpio;

    matching_conn = find_connection_matching_gpio(new_client, &new_gpio, kind);

    if (matching_conn) {
        g_array_append_val(matching_conn->targets, *target);
        g_array_free(conn->targets, true);
        g_free(conn);
        conn = matching_conn;
        target = &g_array_index(conn->targets, HwDtbConnectionTarget,
                                conn->targets->len - 1);
    } else {
        conn->gpio = new_gpio;
        conn->idx = new_idx;
        QSIMPLEQ_INSERT_TAIL(&new_client->connection[kind], conn, link);
    }

    trace_hwdtb_node_legacy_gpio_reverse(target->target->path,
                                         target->gpio.name,
                                         g_array_index(target->tuple, uint32_t, 0),
                                         new_client->path, conn->gpio.name,
                                         conn->idx);
}

static void conns_reverse_direction(ToBeReversedConnList *to_be_reversed)
{
    HwDtbConnection *conn, *next;

    QSIMPLEQ_FOREACH_SAFE(conn, to_be_reversed, link, next) {
        conn_reverse_direction(conn);
    }
}

static bool conn_needs_reversing(HwDtbConnection *conn)
{
    HwDtbConnectionTarget *target;

    if (conn->targets->len == 0) {
        return false;
    }

    target = &g_array_index(conn->targets, HwDtbConnectionTarget, 0);

    return (conn->gpio.sta == HWDTB_GPIO_INPUT)
        && (target->gpio.sta == HWDTB_GPIO_OUTPUT);
}

static void node_get_conns_to_be_reversed(HwDtbNode *node,
                                          ToBeReversedConnList *tbr)
{
    HwDtbNode *child;
    HwDtbConnection *conn, *next;
    size_t i;

    for (i = 0; i < HWDTB_NUM_GPIO_CON; i++) {
        hwdtb_node_foreach_connection_safe(conn, node, i, next) {
            if (!conn_needs_reversing(conn)) {
                continue;
            }

            save_node_in_connection(node, conn);
            QSIMPLEQ_REMOVE(&node->connection[i], conn, HwDtbConnection,
                            link);
            QSIMPLEQ_INSERT_TAIL(tbr, conn, link);
        }
    }

    hwdtb_node_foreach_child(child, node) {
        node_get_conns_to_be_reversed(child, tbr);
    }
}

/*
 * -- Legacy --
 * Reverses `client input' <- `controller output' connections
 *
 * This function reverses `input <- output' connections so that the GPIO connect
 * passes only sees `output -> input' connections.
 *
 * We want to get rid of this as it gets very confusing when writing hwdtbs. Not
 * knowing the connection direction is super error prone. It is ok in a Linux
 * devicetree context where a GPIO connection describes just this, a
 * "connection" (a.k.a. a wire). In the QEMU context however, where GPIOs are
 * inherently directional, it is not practical.
 */
void hwdtb_gpio_legacy_reverse(HwDtb *hwdtb)
{
    ToBeReversedConnList to_be_reversed =
        QSIMPLEQ_HEAD_INITIALIZER(to_be_reversed);

    node_get_conns_to_be_reversed(hwdtb->root, &to_be_reversed);
    conns_reverse_direction(&to_be_reversed);
}

static void conn_target_register_inputs(HwDtbConnectionTarget *target)
{
    hwdtb_node_register_gpio(target->target, &target->gpio, 1);
}

static void conn_register(HwDtbNode *node, HwDtbConnection *conn)
{
    size_t i;

    hwdtb_node_register_gpio(node, &conn->gpio, conn->targets->len);

    for (i = 0; i < conn->targets->len; i++) {
        HwDtbConnectionTarget *target;

        target = &g_array_index(conn->targets, HwDtbConnectionTarget, i);
        conn_target_register_inputs(target);
    }
}

static void node_gpio_register(HwDtbNode *node)
{
    HwDtbConnection *conn;
    HwDtbConnectionKind i;

    for (i = 0; i < HWDTB_NUM_GPIO_CON; i++) {
        hwdtb_node_foreach_connection(conn, node, i) {
            conn_register(node, conn);
        }
    }
}

void hwdtb_gpio_register(HwDtb *hwdtb)
{
    hwdtb_walk(hwdtb, node_gpio_register);
}
