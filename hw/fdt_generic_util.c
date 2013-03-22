/*
 * Utility functions for fdt generic framework
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
 * Copyright (c) 2009 Michal Simek.
 * Copyright (c) 2011 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2011 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>.
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

#ifndef FDT_GENERIC_UTIL_ERR_DEBUG
#define FDT_GENERIC_UTIL_ERR_DEBUG 0
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        fprintf(stderr,  "%s", node_path); \
        DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0);

#include "fdt_generic_util.h"
#include "net/net.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

/* FIXME: wrap direct calls into libfdt */

#include <libfdt.h>

static int simple_bus_fdt_init(char *bus_node_path, FDTMachineInfo *fdti);

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq)
{
    char node_path[DT_PATH_LENGTH];

    FDTMachineInfo *fdti = fdt_init_new_fdti(fdt);

    fdti->irq_base = cpu_irq;

    /* bind any force bound instances */
    fdt_force_bind_all(fdti);

    /* parse the device tree */
    if (!qemu_devtree_get_root_node(fdt, node_path)) {
        fdt_init_set_opaque(fdti, node_path, NULL);
        simple_bus_fdt_init(node_path, fdti);
        while (qemu_co_queue_enter_next(fdti->cq));
    } else {
        fprintf(stderr, "FDT: ERROR: cannot get root node from device tree %s\n"
            , node_path);
    }

    DB_PRINT(0, "FDT: Device tree scan complete\n");
    FDTMachineInfo *ret = g_malloc0(sizeof(*ret));
    return fdti;
}

struct FDTInitNodeArgs {
    char *node_path;
    char *parent_path;
    FDTMachineInfo *fdti;
};

static int fdt_init_qdev(char *node_path, FDTMachineInfo *fdti, char *compat);

static void fdt_init_node(void *args)
{
    int i;
    struct FDTInitNodeArgs *a = args;
    char *node_path = a->node_path;
    FDTMachineInfo *fdti = a->fdti;
    g_free(a);

    simple_bus_fdt_init(node_path, fdti);

    char *all_compats = NULL, *compat, *node_name, *next_compat, *device_type;
    int compat_len;

    DB_PRINT_NP(1, "enter\n");

    /* try instance binding first */
    node_name = qemu_devtree_get_node_name(fdti->fdt, node_path);
    DB_PRINT_NP(1, "node with name: %s\n", node_name ? node_name : "(none)");
    if (!node_name) {
        printf("FDT: ERROR: nameless node: %s\n", node_path);
    }
    if (!fdt_init_inst_bind(node_path, fdti, node_name)) {
        DB_PRINT_NP(0, "instance bind successful\n");
        goto exit;
    }

    /* fallback to compatibility binding */
    all_compats = qemu_devtree_getprop(fdti->fdt, node_path,
        "compatible", &compat_len, false, NULL);
    if (!all_compats) {
        DB_PRINT_NP(0, "no compatibility found\n");
    }

    for (compat = all_compats; compat && compat_len; compat = next_compat+1) {
        char *compat_prefixed = g_strdup_printf("compatible:%s", compat);
        if (!fdt_init_compat(node_path, fdti, compat_prefixed)) {
            goto exit;
        }
        g_free(compat_prefixed);
        if (!fdt_init_qdev(node_path, fdti, compat)) {
            goto exit;
        }
        next_compat = rawmemchr(compat, '\0');
        compat_len -= (next_compat + 1 - compat);
        if (compat_len > 0) {
            *next_compat = ' ';
        }
    }

    device_type = qemu_devtree_getprop(fdti->fdt, node_path,
                                       "device_type", NULL, false, NULL);
    device_type = g_strdup_printf("device_type:%s", device_type);
    if (!fdt_init_compat(node_path, fdti, device_type)) {
        goto exit;
    }

    if (!all_compats) {
        goto exit;
    }
    DB_PRINT_NP(0, "FDT: Unsupported peripheral invalidated - "
                "compatibilities %s\n", all_compats);
    qemu_devtree_setprop_string(fdti->fdt, node_path, "compatible",
        "invalidated");

    for (i = 0;; i++) {
        Error *errp = NULL;
        uint64_t size = 0;
        /* FIXME: inspect address cells and size cells properties */
        hwaddr base = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg",
                                                2 * i, false, &errp);
        if (!errp) {
            size = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg",
                                             2 * i + 1, false, &errp);
        }
        DB_PRINT_NP(errp ? 1 : 0, "%svalid reg property found, %s mmio RAZWI "
                    "for region %d\n", errp ? "in" : "",
                    errp ? "skipping" : "doing", i);
        if (!errp) {
            MemoryRegion *address_space_mem = get_system_memory();
            MemoryRegion *razwi = g_new(MemoryRegion, 1);
            DB_PRINT_NP(0, "mmio address %#llx RAZWI'd\n",
                        (unsigned long long)base);
            memory_region_init_io(razwi, &razwi_unimp_ops, g_strdup(node_path),
                                  node_path, size);
            memory_region_add_subregion(address_space_mem, base, razwi);
        } else {
            break;
        }
    }
exit:

    DB_PRINT_NP(1, "exit\n");

    if (!fdt_init_has_opaque(fdti, node_path)) {
        fdt_init_set_opaque(fdti, node_path, NULL);
    }
    g_free(node_path);
    g_free(all_compats);
    return;
}

static int simple_bus_fdt_init(char *node_path, FDTMachineInfo *fdti)
{
    int i;
    int num_children = qemu_devtree_get_num_children(fdti->fdt, node_path,
                                                        1);
    char **children = qemu_devtree_get_children(fdti->fdt, node_path, 1);

    DB_PRINT_NP(num_children ? 0 : 1, "num child devices: %d\n", num_children);

    for (i = 0; i < num_children; i++) {
        struct FDTInitNodeArgs *init_args = g_malloc0(sizeof(*init_args));
        init_args->node_path = children[i];
        init_args->fdti = fdti;
        qemu_coroutine_enter(qemu_coroutine_create(fdt_init_node), init_args);
    }

    g_free(children);
    return 0;
}

qemu_irq *fdt_get_irq_info(FDTMachineInfo *fdti, char *node_path, int irq_idx,
                          char *info) {
    void *fdt = fdti->fdt;
    uint32_t intc_phandle, intc_cells, idx, cells[32];
    char intc_node_path[DT_PATH_LENGTH], *node_name;
    DeviceState *intc;
    qemu_irq *ret;
    int i;
    Error *errp = NULL;

    intc_phandle = qemu_devtree_getprop_cell(fdt, node_path, "interrupt-parent",
                                                                0, true, &errp);
    if (errp) {
        goto fail;
    }

    if (qemu_devtree_get_node_by_phandle(fdt, intc_node_path, intc_phandle)) {
        goto fail;
    }
    intc_cells = qemu_devtree_getprop_cell(fdt, intc_node_path,
                                           "#interrupt-cells", 0, false, &errp);
    for (i = 0; i < intc_cells; ++i) {
        cells[i] = qemu_devtree_getprop_cell(fdt, node_path, "interrupts",
                                             intc_cells * irq_idx + i, false,
                                             &errp);
        if (errp) {
            goto fail;
        }
    }

    while (!fdt_init_has_opaque(fdti, intc_node_path)) {
        fdt_init_yield(fdti);
    }
    intc = DEVICE(fdt_init_get_opaque(fdti, intc_node_path));
    if (!intc) {
        goto fail;
    }
    node_name = qemu_devtree_get_node_name(fdt, intc_node_path);

    while (!intc->realized) {
        fdt_init_yield(fdti);
    }

    switch (intc_cells) { /* FIXME: be less ARM and Microblaze specific */
    case (2) : /* microblaze */
        idx = cells[0];
        goto simple;
    case (3) : /* ARM a9 GIC */
        idx = cells[1];
        if (cells[0]) { /* PPI nastiness */
            int cpu = 0;
            qemu_irq *next;

            if (info) {
                sprintf(info, "ARM PPI: %d (%s)", idx, node_name);
            }

            ret = g_new0(qemu_irq, 9);
            next = ret;

            for (cpu = 0; cpu < 8; cpu ++) {
                if (cells[2] & 1 << (cpu + 8)) {
                    *next = qdev_get_gpio_in(intc, (cpu + 2) * 32 + idx + 16);
                    next++;
                }
            }
            return ret;
        } else {
            goto simple;
        }
    default:
        goto fail;
    }
simple:
    if (info) {
        sprintf(info, "%d (%s)", idx, node_name);
        g_free((void *)node_name);
    }
    ret = g_new0(qemu_irq, 2);
    ret[0] = qdev_get_gpio_in(intc, idx);
    return ret;

fail:
    if (info) {
        sprintf(info, "(none)");
    }
    return NULL;
}

qemu_irq *fdt_get_irq(FDTMachineInfo *fdti, char *node_path, int irq_idx)
{
    return fdt_get_irq_info(fdti, node_path, irq_idx, NULL);
}

/* FIXME: figure out a real solution to this */

#define DIGIT(a) ((a) >= '0' && (a) <= '9')
#define LOWER_CASE(a) ((a) >= 'a' && (a) <= 'z')

static void trim_xilinx_version(char *x)
{
    for (;;) {
        x = strchr(x, '-');
        if (!x || strlen(x) < 7) {
            return;
        }
        if (DIGIT(x[1]) &&
                x[2] == '.' &&
                DIGIT(x[3]) &&
                DIGIT(x[4]) &&
                x[5] == '.' &&
                LOWER_CASE(x[6])) {
            *x = '\0';
            return;
        }
        x++;
    }
}

static void substitute_char(char *s, char a, char b)
{
    for (;;) {
        s = strchr(s, a);
        if (!s) {
            return;
        }
        *s = b;
        s++;
    }
}

static inline const char *trim_vendor(const char *s)
{
    /* FIXME: be more intelligent */
    const char *ret = memchr(s, ',', strlen(s));
    return ret ? ret + 1 : s;
}

static DeviceState *fdt_create_qdev_from_compat(const char *compat,
                                                char **dev_type)
{
    DeviceState *ret = NULL;

    char *c = g_strdup(compat);
    ret = qdev_try_create(NULL, c);
    if (!ret) {
        /* QEMU substitutes "."s for ","s in device names, so try with that
         * substitutution
         */
        substitute_char(c, ',', '.');
        ret = qdev_try_create(NULL, c);
    }
    if (!ret) {
        /* try again with the xilinx version string trimmed */
        trim_xilinx_version(c);
        ret = qdev_try_create(NULL, c);
    }

    if (dev_type) {
        *dev_type = c;
    } else {
        g_free(c);
    }

    if (!ret) {
        const char *no_vendor = trim_vendor(compat);

        if (no_vendor != compat) {
            return fdt_create_qdev_from_compat(no_vendor, dev_type);
        }
    }
    return ret;
}

/*FIXME: roll into device tree functionality */

static inline uint64_t get_int_be(const void *p, int len)
{
    switch (len) {
    case 1:
        return *((uint8_t *)p);
    case 2:
        return be16_to_cpu(*((uint16_t *)p));
    case 4:
        return be32_to_cpu(*((uint32_t *)p));
    case 8:
        return be32_to_cpu(*((uint64_t *)p));
    default:
        fprintf(stderr, "unsupported integer length\n");
        abort();
    }
}

static int fdt_init_qdev(char *node_path, FDTMachineInfo *fdti, char *compat)
{
    Object *dev, *parent;
    char *dev_type = NULL;
    int is_intc;
    Error *errp = NULL;
    int i, j;
    QEMUDevtreeProp *prop, *props;
    char parent_node_path[DT_PATH_LENGTH];
    int num_children = qemu_devtree_get_num_children(fdti->fdt, node_path, 1);
    char **children = qemu_devtree_get_children(fdti->fdt, node_path, 1);

    if (!compat) {
        return 1;
    }
    dev = OBJECT(fdt_create_qdev_from_compat(compat, &dev_type));
    if (!dev) {
        DB_PRINT_NP(1, "no match found for %s\n", compat);
        return 1;
    }

    if (qemu_devtree_getparent(fdti->fdt, parent_node_path, node_path)) {
        abort();
    }
    while (!fdt_init_has_opaque(fdti, parent_node_path)) {
        fdt_init_yield(fdti);
    }
    parent = fdt_init_get_opaque(fdti, parent_node_path);
    if (parent) {
        DB_PRINT_NP(1, "parenting node\n");
        object_property_add_child(OBJECT(parent),
                              qemu_devtree_get_node_name(fdti->fdt, node_path),
                              OBJECT(dev), NULL);
        if (object_dynamic_cast(parent, TYPE_BUS) &&
               object_dynamic_cast(dev, TYPE_DEVICE)) {
            DB_PRINT_NP(1, "bus parenting node\n");
            qdev_set_parent_bus(DEVICE(dev), BUS(parent));
        }
    } else {
        DB_PRINT_NP(1, "orphaning node\n");
        /* FIXME: Make this go away (centrally) */
        object_property_add_child(
                              container_get(qdev_get_machine(), "/unattached"),
                              qemu_devtree_get_node_name(fdti->fdt, node_path),
                              OBJECT(dev), NULL);
    }
    fdt_init_set_opaque(fdti, node_path, dev);

    /* FIXME: come up with a better way. Need to yield to give child nodes a
     * chance to set parents before qdev initing the parents
     */
    fdt_init_yield(fdti);

    props = qemu_devtree_get_props(fdti->fdt, node_path);
    for (prop = props; prop->name; prop++) {
        const char *propname = trim_vendor(prop->name);
        int len = prop->len;
        void *val = prop->value;

        ObjectProperty *p = object_property_find(OBJECT(dev), propname, NULL);
        if (p) {
            DB_PRINT_NP(1, "matched property: %s of type %s, len %d\n",
                                            propname, p->type, prop->len);
        }
        if (!p) {
            continue;
        }

        /* FIXME: handle generically using accessors and stuff */
        if (!strcmp(p->type, "uint8") || !strcmp(p->type, "uint16") ||
                !strcmp(p->type, "uint32") || !strcmp(p->type, "uint64")) {
            object_property_set_int(OBJECT(dev), get_int_be(val, len), propname,
                                    &errp);
            assert_no_error(errp);
            DB_PRINT_NP(0, "set property %s to %#llx\n", propname,
                        (unsigned long long)get_int_be(val, len));
        } else if (!strcmp(p->type, "bool")) {
            object_property_set_bool(OBJECT(dev), !!get_int_be(val, len),
                                     propname, &errp);
            assert_no_error(errp);
            DB_PRINT_NP(0, "set property %s to %s\n", propname,
                        get_int_be(val, len) ? "true" : "false");
        } else if (!strncmp(p->type, "link", 4)) {
            char target_node_path[DT_PATH_LENGTH];
            char propname_target[1024];
            strcpy(propname_target, propname);
            strcat(propname_target, "-target");

            Object *linked_dev, *proxy;

            if (qemu_devtree_get_node_by_phandle(fdti->fdt, target_node_path,
                                                get_int_be(val, len))) {
                abort();
            }
            while (!fdt_init_has_opaque(fdti, target_node_path)) {
                fdt_init_yield(fdti);
            }
            linked_dev = fdt_init_get_opaque(fdti, target_node_path);

            proxy = object_property_get_link(linked_dev, propname_target,
                                             &errp);
            if (!errp && proxy) {
                DB_PRINT_NP(0, "detected proxy object for %s connection\n",
                            propname);
                linked_dev = proxy;
            }
            errp = NULL;
            object_property_set_link(OBJECT(dev), linked_dev, propname, &errp);
            DB_PRINT_NP(0, "set link %s %p->%p\n", propname, OBJECT(dev),
                        linked_dev);
            assert_no_error(errp);
        }
    }

    for (i = 0; i < num_children; i++) {
        DeviceState *child;

        while (!fdt_init_has_opaque(fdti, children[i])) {
            DB_PRINT_NP(1, "Waiting on child %s to qdev_create\n", children[i]);
            fdt_init_yield(fdti);
        }
        child = (DeviceState *)object_dynamic_cast(fdt_init_get_opaque(fdti,
                                                   children[i]), TYPE_DEVICE);
        if (child) {
            while (!child->realized) {
                DB_PRINT_NP(1, "Waiting on child %s to qdev_init\n",
                            children[i]);
                fdt_init_yield(fdti);
            }
        }
    }

    if (object_dynamic_cast(dev, TYPE_DEVICE)) {
        /* connect nic if appropriate */
        static int nics;
        const char *short_name = qemu_devtree_get_node_name(fdti->fdt, node_path);

        qdev_set_nic_properties(DEVICE(dev), &nd_table[nics]);
        if (nd_table[nics].instantiated) {
            DB_PRINT_NP(0, "NIC instantiated: %s\n", dev_type);
            nics++;
        }
        DB_PRINT_NP(0, "Short naming node: %s\n", short_name);
        (DEVICE(dev))->id = g_strdup(short_name);
        qdev_init_nofail(DEVICE(dev));
    }

    if (object_dynamic_cast(dev, TYPE_SYS_BUS_DEVICE)) {
        /* map slave attachment */
        for (i = 0;; i++) {
            /* FIXME: inspect address cells and size cells properties */
            hwaddr base = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg",
                                                    2 * i, false, &errp);
            qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg", 2 * i + 1,
                                      false, &errp);
            DB_PRINT_NP(errp ? 1 : 0, "%svalid reg property found, %s mmio map "
                        "for region %d\n", errp ? "in" : "",
                        errp ? "skipping" : "doing", i);
            if (!errp) {
                DB_PRINT_NP(0, "mmio region %d mapped to %#llx\n", i,
                            (unsigned long long)base);
                sysbus_mmio_map(SYS_BUS_DEVICE(dev), i, base);
            } else {
                break;
            }
        }

        {
            int len;
            fdt_get_property(fdti->fdt, fdt_path_offset(fdti->fdt, node_path),
                                    "interrupt-controller", &len);
            is_intc = len >= 0;
            DB_PRINT_NP(is_intc ? 0 : 1, "is interrupt controller: %c\n",
                        is_intc ? 'y' : 'n');
        }
        /* connect irq */
        j = 0;
        for (i = 0;; i++) {
            char irq_info[1024];
            qemu_irq *irqs = fdt_get_irq_info(fdti, node_path, i, irq_info);
            /* INTCs inferr their top level, if no IRQ connection specified */
            if (!irqs && is_intc && i == 0) {
                irqs = fdti->irq_base;
            }
            if (!irqs) {
                break;
            }
            while (*irqs) {
                DB_PRINT_NP(0, "FDT: (%s) connecting irq %d: %s\n", dev_type, j,
                            irq_info);
                sysbus_connect_irq(SYS_BUS_DEVICE(dev), j++, *irqs);
                irqs++;
            }
        }
    }

    if (dev_type) {
        g_free(dev_type);
    }

    return 0;
}
