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

#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"
#include "net/net.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#ifndef FDT_GENERIC_UTIL_ERR_DEBUG
#define FDT_GENERIC_UTIL_ERR_DEBUG 0
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask_level(LOG_FDT, lvl, ": %s: ", __func__); \
        qemu_log_mask_level(LOG_FDT, lvl, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
       qemu_log_mask_level(LOG_FDT, lvl, "%s", node_path); \
       DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0);

#include "hw/remote-port-device.h"

/* FIXME: wrap direct calls into libfdt */

#include <libfdt.h>
#include <stdlib.h>

static int simple_bus_fdt_init(char *bus_node_path, FDTMachineInfo *fdti);

typedef struct QEMUIRQSharedState {
    qemu_irq sink;
    int num;
    bool (*merge_fn)(bool *, int);
/* FIXME: remove artificial limit */
#define MAX_IRQ_SHARED_INPUTS 16
    bool inputs[MAX_IRQ_SHARED_INPUTS];
} QEMUIRQSharedState;

static bool qemu_irq_shared_or_handler(bool *inputs, int n)
{
    int i;

    for (i = 0; i < n; ++i) {
        if (inputs[i]) {
            return true;
        }
    }
    return false;
}

static bool qemu_irq_shared_and_handler(bool *inputs, int n)
{
    int i;

    for (i = 0; i < n; ++i) {
        if (!inputs[i]) {
            return false;
        }
    }
    return true;
}

static void qemu_irq_shared_handler(void *opaque, int n, int level)
{
    QEMUIRQSharedState *s = opaque;

    s->inputs[n] = level;
    qemu_set_irq(s->sink, s->merge_fn(s->inputs, s->num));
}

static void fdt_init_all_irqs(FDTMachineInfo *fdti)
{
    while (fdti->irqs) {
        FDTIRQConnection *first = fdti->irqs;
        qemu_irq sink = first->irq;
        bool (*merge_fn)(bool *, int) = first->merge_fn;
        int num_sources = 0;
        FDTIRQConnection *irq;

        for (irq = first; irq; irq = irq->next) {
            if (irq->irq == sink) { /* Same sink */
                num_sources++;
            }
        }
        if (num_sources > 1) {
            QEMUIRQSharedState *s = g_malloc0(sizeof *s);
            s->sink = sink;
            s->merge_fn = merge_fn;
            qemu_irq *sources = qemu_allocate_irqs(qemu_irq_shared_handler, s,
                                                   num_sources);
            for (irq = first; irq; irq = irq->next) {
                if (irq->irq == sink) {
                    char *shared_irq_name = g_strdup_printf("shared-irq-%p",
                                                            *sources);

                    if (irq->merge_fn != merge_fn) {
                        fprintf(stderr, "ERROR: inconsistent IRQ merge fns\n");
                        exit(1);
                    }

                    object_property_add_child(OBJECT(irq->dev), shared_irq_name,
                                              OBJECT(*sources), &error_abort);
                    g_free(shared_irq_name);
                    irq->irq = *(sources++);
                    s->num++;
                }
            }
        }
        DB_PRINT(0, "%s: connected to %s irq line %d\n", first->sink_info,
                 object_get_canonical_path(OBJECT(first->dev)),
                 first->i);

        qdev_connect_gpio_out_named(DEVICE(first->dev), first->name, first->i,
                                    first->irq);
        fdti->irqs = first->next;
        g_free(first);
    }
}

FDTMachineInfo *fdt_generic_create_machine(void *fdt, qemu_irq *cpu_irq)
{
    char node_path[DT_PATH_LENGTH];

    FDTMachineInfo *fdti = fdt_init_new_fdti(fdt);

    fdti->irq_base = cpu_irq;

    /* parse the device tree */
    if (!qemu_devtree_get_root_node(fdt, node_path)) {
        fdt_init_set_opaque(fdti, node_path, NULL);
        simple_bus_fdt_init(node_path, fdti);
        while (qemu_co_enter_next(fdti->cq));
        fdt_init_all_irqs(fdti);
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
    all_compats = qemu_fdt_getprop(fdti->fdt, node_path, "compatible",
                                   &compat_len, false, NULL);
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

    device_type = qemu_fdt_getprop(fdti->fdt, node_path,
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
    qemu_fdt_setprop_string(fdti->fdt, node_path, "compatible", "invalidated");
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

static qemu_irq fdt_get_gpio(FDTMachineInfo *fdti, char *node_path,
                             int* cur_cell, qemu_irq input,
                             const FDTGenericGPIOSet *gpio_set,
                             const char *debug_success, bool *end) {
    void *fdt = fdti->fdt;
    uint32_t parent_phandle, parent_cells = 0, cells[32];
    char parent_node_path[DT_PATH_LENGTH];
    DeviceState *parent;
    int i;
    Error *errp = NULL;
    const char *reason;
    bool free_reason = false;
    const char *propname = gpio_set->names->propname;
    const char *cells_propname = gpio_set->names->cells_propname;

    cells[0] = 0;

    parent_phandle = qemu_fdt_getprop_cell(fdt, node_path, propname,
                                           (*cur_cell)++, false, &errp);
    if (errp) {
        reason = g_strdup_printf("Cant get phandle from \"%s\" property\n",
                                 propname);
        *end = true;
        free_reason = true;
        goto fail_silent;
    }
    if (qemu_devtree_get_node_by_phandle(fdt, parent_node_path,
                                         parent_phandle)) {
        *end = true;
        reason = "cant get node from phandle\n";
        goto fail;
    }
    parent_cells = qemu_fdt_getprop_cell(fdt, parent_node_path,
                                         cells_propname, 0, false, &errp);
    if (errp) {
        *end = true;
        reason = g_strdup_printf("cant get parent \"%s\" property\n",
                                 cells_propname);
        free_reason = true;
        goto fail;
    }

    for (i = 0; i < parent_cells; ++i) {
        cells[i] = qemu_fdt_getprop_cell(fdt, node_path, propname,
                                         (*cur_cell)++, false, &errp);
        if (errp) {
            *end = true;
            reason = "cant get cell value";
            goto fail;
        }
    }

    while (!fdt_init_has_opaque(fdti, parent_node_path)) {
        fdt_init_yield(fdti);
    }
    parent = DEVICE(fdt_init_get_opaque(fdti, parent_node_path));

    if (!parent) {
        reason = "parent is not a device";
        goto fail_silent;
    }

    while (!parent->realized) {
        fdt_init_yield(fdti);
    }

    {
        const FDTGenericGPIOConnection *fgg_con = NULL;
        uint16_t range, idx;
        const char *gpio_name = NULL;
        qemu_irq ret;

        if (object_dynamic_cast(OBJECT(parent), TYPE_FDT_GENERIC_GPIO)) {
            const FDTGenericGPIOSet *set;
            FDTGenericGPIOClass *parent_fggc =
                        FDT_GENERIC_GPIO_GET_CLASS(parent);

            for (set = parent_fggc->controller_gpios; set && set->names;
                 set++) {
                if (!strcmp(gpio_set->names->cells_propname,
                            set->names->cells_propname)) {
                    fgg_con = set->gpios;
                    break;
                }
            }
        }

        /* FIXME: cells[0] is not always the fdt indexing match system */
        idx = cells[0] & ~(1ul << 31);
        if (fgg_con) {
            range = fgg_con->range ? fgg_con->range : 1;
            while (!(idx >= fgg_con->fdt_index
                     && idx < (fgg_con->fdt_index + range))
                   && fgg_con->name) {
                fgg_con++;
            }
            if (!fgg_con) {
                goto fail;
            }

            idx -= fgg_con->fdt_index;
            gpio_name = fgg_con->name;
        }

        if (input) {
            FDTIRQConnection *irq = g_new0(FDTIRQConnection, 1);
            bool (*merge_fn)(bool *, int) = qemu_irq_shared_or_handler;

            /* FIXME: I am kind of stealing here. Use the msb of the first
             * cell to indicate the merge function. This needs to be discussed
             * with device-tree community on how this should be done properly.
             */
            if (cells[0] & (1 << 31)) {
                merge_fn = qemu_irq_shared_and_handler;
            }

            DB_PRINT_NP(1, "%s GPIO output %s[%d] on %s\n", debug_success,
                        gpio_name ? gpio_name : "unnamed", idx,
                        parent_node_path);
            *irq = (FDTIRQConnection) {
                .dev = parent,
                .name = gpio_name,
                .merge_fn = merge_fn,
                .i = idx,
                .irq = input,
                .sink_info = NULL, /* FIMXE */
                .next = fdti->irqs
            };
            fdti->irqs = irq;
        }
        ret = qdev_get_gpio_in_named(parent, gpio_name, idx);

        if (ret) {
            DB_PRINT_NP(1, "wiring GPIO input %s on %s ... \n",
                        fgg_con ? fgg_con->name : "unnamed", parent_node_path);
        }
        return ret;
    }
fail:
    fprintf(stderr, "Failure: %s\n", reason);
fail_silent:
    if (free_reason) {
        g_free((void *)reason);
    }
    return NULL;
}

static void fdt_get_irq_info_from_intc(FDTMachineInfo *fdti, qemu_irq *ret,
                                       char *intc_node_path,
                                       uint32_t *cells, uint32_t num_cells,
                                       uint32_t max, Error **errp)
{
    FDTGenericIntcClass *intc_fdt_class;
    DeviceState *intc;

    while (!fdt_init_has_opaque(fdti, intc_node_path)) {
        fdt_init_yield(fdti);
    }
    intc = DEVICE(fdt_init_get_opaque(fdti, intc_node_path));

    if (!intc) {
        goto fail;
    }

    while (!intc->realized) {
        fdt_init_yield(fdti);
    }

    intc_fdt_class = FDT_GENERIC_INTC_GET_CLASS(intc);
    if (!intc_fdt_class) {
        goto fail;
    }

    intc_fdt_class->get_irq(FDT_GENERIC_INTC(intc), ret, cells, num_cells,
                            max, errp);
    return;
fail:
    error_setg(errp, "%s", __func__);
}

static uint32_t imap[4096];
static bool imap_cached;

qemu_irq *fdt_get_irq_info(FDTMachineInfo *fdti, char *node_path, int irq_idx,
                          char *info, bool *map_mode) {
    void *fdt = fdti->fdt;
    uint32_t intc_phandle, intc_cells, cells[32];
    char intc_node_path[DT_PATH_LENGTH];
    qemu_irq *ret = NULL;
    int i;
    Error *errp = NULL;

    intc_phandle = qemu_fdt_getprop_cell(fdt, node_path, "interrupt-parent",
                                                                0, true, &errp);
    if (errp) {
        errp = NULL;
        intc_cells = qemu_fdt_getprop_cell(fdt, node_path,
                                           "#interrupt-cells", 0, true, &errp);
        if (errp) {
            goto fail;
        }
        *map_mode = true;
    } else {
        if (qemu_devtree_get_node_by_phandle(fdt, intc_node_path,
                                             intc_phandle)) {
            goto fail;
        }
        intc_cells = qemu_fdt_getprop_cell(fdt, intc_node_path,
                                           "#interrupt-cells", 0, true, &errp);
        if (errp) {
            goto fail;
        }
        *map_mode = false;
    }

    for (i = 0; i < intc_cells; ++i) {
        cells[i] = qemu_fdt_getprop_cell(fdt, node_path, "interrupts",
                                         intc_cells * irq_idx + i, false, &errp);
        if (errp) {
            goto fail;
        }
    }

    if (*map_mode) {
        int k, i = 0;
        ret = g_new0(qemu_irq, 1);
        int num_matches = 0;
        int len;
        uint32_t imap_mask[intc_cells];
        uint32_t *imap_p;

        for (k = 0; k < intc_cells; ++k) {
            imap_mask[k] = qemu_fdt_getprop_cell(fdt, node_path,
                                                 "interrupt-map-mask", k + 2,
                                                 true, &errp);
            if (errp) {
                goto fail;
            }
        }

        imap_p = qemu_fdt_getprop(fdt, node_path, "interrupt-map", &len, true,
                                  &errp);
        if (errp) {
            goto fail;
        }
        if (!imap_cached) {
            memcpy(imap, imap_p, len);
            imap_cached = true;
        }

        len /= sizeof(uint32_t);

        while (i < len) {
            bool match = true;
            uint32_t new_intc_cells, new_cells[32];
            i++; i++; /* FIXME: do address cells properly */
            for (k = 0; k < intc_cells; ++k) {
                uint32_t  map_val = be32_to_cpu(imap[i++]);
                if ((cells[k] ^ map_val) & imap_mask[k]) {
                    match = false;
                }
            }
            /* when caching, we hackishly store the number of cells for
             * the parent in the MSB. +1, so zero MSB means non cachd
             * and the full lookup is needed.
             */
            intc_phandle = be32_to_cpu(imap[i++]);
            if (intc_phandle & (0xffu << 24)) {
                new_intc_cells = (intc_phandle >> 24) - 1;
            } else {
                if (qemu_devtree_get_node_by_phandle(fdt, intc_node_path,
                                                     intc_phandle)) {
                    goto fail;
                }
                new_intc_cells = qemu_fdt_getprop_cell(fdt, intc_node_path,
                                                       "#interrupt-cells", 0,
                                                       false, &errp);
                imap[i - 1] = cpu_to_be32(intc_phandle | (new_intc_cells + 1) << 24);
                if (errp) {
                    goto fail;
                }
            }
            for (k = 0; k < new_intc_cells; ++k) {
                new_cells[k] = be32_to_cpu(imap[i++]);
            }
            if (match) {
                num_matches++;
                ret = g_renew(qemu_irq, ret, num_matches + 1);
                if (intc_phandle & (0xffu << 24)) {
                    if (qemu_devtree_get_node_by_phandle(fdt, intc_node_path,
                                                         intc_phandle &
                                                         ((1 << 24) - 1))) {
                        goto fail;
                    }
                }
                memset(&ret[num_matches], 0, sizeof(*ret));
                fdt_get_irq_info_from_intc(fdti, &ret[num_matches-1], intc_node_path,
                                           new_cells, new_intc_cells, 1, &errp);
                if (info) {
                   sprintf(info, "%s", intc_node_path);
                   info += strlen(info) + 1;
                }
                if (errp) {
                    goto fail;
                }
            }
        }
        return ret;
    }

    ret = g_new0(qemu_irq, fdt_generic_num_cpus + 2);
    fdt_get_irq_info_from_intc(fdti, ret, intc_node_path, cells, intc_cells,
                               fdt_generic_num_cpus, &errp);
    if (errp) {
        goto fail;
    }

    /* FIXME: Phase out this info bussiness */
    if (info) {
        sprintf(info, "%s", intc_node_path);
    }

    return ret;

fail:
    if (errp) {
        sprintf(info, "%s", error_get_pretty(errp));
    } else {
        sprintf(info, "(none)");
    }
    return NULL;
}

qemu_irq *fdt_get_irq(FDTMachineInfo *fdti, char *node_path, int irq_idx,
                      bool *map_mode)
{
    return fdt_get_irq_info(fdti, node_path, irq_idx, NULL, map_mode);
}

/* FIXME: figure out a real solution to this */

#define DIGIT(a) ((a) >= '0' && (a) <= '9')
#define LOWER_CASE(a) ((a) >= 'a' && (a) <= 'z')

static void trim_xilinx_version(char *x)
{
    for (;;) {
        x = strchr(x, '-');
        if (!x) {
            return;
        }
        if (DIGIT(x[1])) {
            char *p;
            if (strtol(x+1, &p, 0)) {
                p = p;	// -Werror
            }
            if ( *p == '.') {
                *x = 0;
                return;
            } else if ( *p == 0) {
                return;
            }
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

static Object *fdt_create_from_compat(const char *compat, char **dev_type)
{
    Object *ret = NULL;

    char *c = g_strdup(compat);
    ret = object_new(c);
    if (!ret) {
        /* QEMU substitutes "."s for ","s in device names, so try with that
         * substitutution
         */
        substitute_char(c, ',', '.');
        ret = object_new(c);
    }
    if (!ret) {
        /* try again with the xilinx version string trimmed */
        trim_xilinx_version(c);
        ret = object_new(c);
    }

    if (dev_type) {
        *dev_type = c;
    } else {
        g_free(c);
    }

    if (!ret) {
        const char *no_vendor = trim_vendor(compat);

        if (no_vendor != compat) {
            return fdt_create_from_compat(no_vendor, dev_type);
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

/* FIXME: use structs instead of parallel arrays */

static const char *fdt_generic_reg_size_prop_names[] = {
    "#address-cells",
    "#size-cells",
    "#bus-cells",
    "#priority-cells",
};

static const int fdt_generic_reg_cells_defaults[] = {
    1,
    1,
    0,
    0,
};

static int fdt_init_qdev(char *node_path, FDTMachineInfo *fdti, char *compat)
{
    Object *dev, *parent;
    char *dev_type = NULL;
    int is_intc;
    Error *errp = NULL;
    int i, j;
    QEMUDevtreeProp *prop, *props;
    char parent_node_path[DT_PATH_LENGTH];
    const FDTGenericGPIOSet *gpio_set = NULL;
    FDTGenericGPIOClass *fggc = NULL;

    if (!compat) {
        return 1;
    }
    dev = fdt_create_from_compat(compat, &dev_type);
    if (!dev) {
        DB_PRINT_NP(1, "no match found for %s\n", compat);
        return 1;
    }
    DB_PRINT_NP(1, "matched compat %s\n", compat);

    /* Do this super early so fdt_generic_num_cpus is correct ASAP */
    if (object_dynamic_cast(dev, TYPE_CPU)) {
        fdt_generic_num_cpus++;
        DB_PRINT_NP(0, "is a CPU - total so far %d\n", fdt_generic_num_cpus);
    }

    if (qemu_devtree_getparent(fdti->fdt, parent_node_path, node_path)) {
        abort();
    }
    while (!fdt_init_has_opaque(fdti, parent_node_path)) {
        fdt_init_yield(fdti);
    }
    parent = fdt_init_get_opaque(fdti, parent_node_path);
    if (dev->parent) {
        DB_PRINT_NP(0, "Node already parented - skipping node\n");
    } else if (parent) {
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
                              object_get_root(),
                              qemu_devtree_get_node_name(fdti->fdt, node_path),
                              OBJECT(dev), NULL);
    }
    fdt_init_set_opaque(fdti, node_path, dev);

    /* Set the default sync-quantum based on the global one. Node properties
     * in the dtb can later override this value.  */
    if (global_sync_quantum) {
        ObjectProperty *p;

        p = object_property_find(OBJECT(dev), "sync-quantum", NULL);
        if (p) {
            object_property_set_int(OBJECT(dev), global_sync_quantum,
                                    "sync-quantum", &errp);
        }
    }

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

        if (!strcmp(propname, "type")) {
            continue;
        }

        /* FIXME: handle generically using accessors and stuff */
        if (!strcmp(p->type, "uint8") || !strcmp(p->type, "uint16") ||
                !strcmp(p->type, "uint32") || !strcmp(p->type, "uint64")) {
            object_property_set_int(OBJECT(dev), get_int_be(val, len), propname,
                                    &error_abort);
            DB_PRINT_NP(0, "set property %s to %#llx\n", propname,
                        (unsigned long long)get_int_be(val, len));
        } else if (!strcmp(p->type, "boolean") || !strcmp(p->type, "bool")) {
            object_property_set_bool(OBJECT(dev), !!get_int_be(val, len),
                                     propname, &error_abort);
            DB_PRINT_NP(0, "set property %s to %s\n", propname,
                        get_int_be(val, len) ? "true" : "false");
        } else if (!strcmp(p->type, "string") || !strcmp(p->type, "str")) {
            object_property_set_str(OBJECT(dev), (const char *)val, propname,
                                    &error_abort);
            DB_PRINT_NP(0, "set property %s to %s\n", propname,
                        (const char *)val);
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

            proxy = linked_dev ? object_property_get_link(linked_dev,
                                                          propname_target,
                                                          &errp) : NULL;
            if (!errp && proxy) {
                DB_PRINT_NP(0, "detected proxy object for %s connection\n",
                            propname);
                linked_dev = proxy;
            }
            errp = NULL;
            if (linked_dev) {
                object_property_set_link(OBJECT(dev), linked_dev, propname,
                                         &error_abort);
                DB_PRINT_NP(0, "set link %s\n", propname);
            }
        } else {
            DB_PRINT_NP(0, "WARNING: property is of unknown type\n");
        }
    }

    /* FIXME: not pretty, but is half a sane dts binding */
    if (object_dynamic_cast(dev, TYPE_REMOTE_PORT_DEVICE)) {
        int i;

        for (i = 0;;++i) {
            char adaptor_node_path[DT_PATH_LENGTH];
            uint32_t adaptor_phandle, chan;
            DeviceState *adaptor;
            char *name;

            adaptor_phandle = qemu_fdt_getprop_cell(fdti->fdt, node_path,
                                                    "remote-ports",
                                                    2 * i, false, &errp);
            if (errp) {
                DB_PRINT_NP(1, "cant get phandle from \"remote-ports\" "
                            "property\n");
                break;
            }
            if (qemu_devtree_get_node_by_phandle(fdti->fdt, adaptor_node_path,
                                                 adaptor_phandle)) {
                DB_PRINT_NP(1, "cant get node from phandle\n");
                break;
            }
            adaptor = DEVICE(fdt_init_get_opaque(fdti, adaptor_node_path));
            name = g_strdup_printf("rp-adaptor%" PRId32, i);
            object_property_set_link(OBJECT(dev), OBJECT(adaptor), name, &errp);
            DB_PRINT_NP(0, "connecting RP to adaptor %s channel %d",
                        object_get_canonical_path(OBJECT(adaptor)), i);
            g_free(name);
            if (errp) {
                DB_PRINT_NP(1, "cant set adaptor link for device property\n");
                break;
            }

            chan = qemu_fdt_getprop_cell(fdti->fdt, node_path, "remote-ports",
                                         2 * i + 1, false, &errp);
            if (errp) {
                DB_PRINT_NP(1, "cant get channel from \"remote-ports\" "
                            "property\n");
                break;
            }

            name = g_strdup_printf("rp-chan%" PRId32, i);
            object_property_set_int(OBJECT(dev), chan, name, &errp);
            /* Not critical - device has right to not care about channel
             * numbers if its a pure slave (only responses).
             */
            if (errp) {
                DB_PRINT_NP(1, "cant set %s property %s\n", name, error_get_pretty(errp));
                errp = NULL;
            }
            g_free(name);

            name = g_strdup_printf("remote-port-dev%d", chan);
            object_property_set_link(OBJECT(adaptor), OBJECT(dev), name,
                                     &errp);
            g_free(name);
            if (errp) {
                DB_PRINT_NP(1, "cant set device link for adaptor\n");
                break;
            }
        }
        errp = NULL;
    }

    if (object_dynamic_cast(dev, TYPE_DEVICE)) {
        DeviceClass *dc = DEVICE_GET_CLASS(dev);
        /* connect nic if appropriate */
        static int nics;
        const char *short_name = qemu_devtree_get_node_name(fdti->fdt, node_path);

        if (object_property_find(OBJECT(dev), "mac", NULL) &&
                    object_property_find(OBJECT(dev), "netdev", NULL)) {
            qdev_set_nic_properties(DEVICE(dev), &nd_table[nics]);
        }
        if (nd_table[nics].instantiated) {
            DB_PRINT_NP(0, "NIC instantiated: %s\n", dev_type);
            nics++;
        }

        /* Regular TYPE_DEVICE houskeeping */
        DB_PRINT_NP(0, "Short naming node: %s\n", short_name);
        (DEVICE(dev))->id = g_strdup(short_name);
        qdev_init_nofail(DEVICE(dev));
        qemu_register_reset((void (*)(void *))dc->reset, dev);
    }

    if (object_dynamic_cast(dev, TYPE_SYS_BUS_DEVICE) || 
        object_dynamic_cast(dev, TYPE_FDT_GENERIC_MMAP)) {
        FDTGenericRegPropInfo reg = {0};
        char parent_path[DT_PATH_LENGTH];
        int cell_idx = 0;
        bool extended = true;

        qemu_fdt_getprop_cell(fdti->fdt, node_path, "reg-extended", 0, false,
                              &errp);
        if (errp) {
            error_free(errp);
            errp = NULL;
            extended = false;
            qemu_devtree_getparent(fdti->fdt, parent_path, node_path);
        }

        for (reg.n = 0;; reg.n++) {
            char ph_parent[DT_PATH_LENGTH];
            const char *pnp = parent_path;

            reg.parents = g_renew(Object *, reg.parents, reg.n + 1);
            reg.parents[reg.n] = parent;

            if (extended) {
                int p_ph = qemu_fdt_getprop_cell(fdti->fdt, node_path,
                                                 "reg-extended", cell_idx++,
                                                 false, &errp);
                if (errp) {
                    error_free(errp);
                    errp = NULL;
                    goto exit_reg_parse;
                }
                if (qemu_devtree_get_node_by_phandle(fdti->fdt, ph_parent,
                                                     p_ph)) {
                    goto exit_reg_parse;
                }
                while (!fdt_init_has_opaque(fdti, ph_parent)) {
                    fdt_init_yield(fdti);
                }
                reg.parents[reg.n] = fdt_init_get_opaque(fdti, ph_parent);
                pnp = ph_parent;
            }

            for (i = 0; i < FDT_GENERIC_REG_TUPLE_LENGTH; ++i) {
                const char *size_prop_name = fdt_generic_reg_size_prop_names[i];
                int nc = qemu_fdt_getprop_cell(fdti->fdt, pnp, size_prop_name,
                                               0, true, &errp);

                if (errp) {
                    int size_default = fdt_generic_reg_cells_defaults[i];

                    DB_PRINT_NP(0, "WARNING: no %s for %s container, assuming "
                                " default of \n%d", size_prop_name, pnp,
                                size_default);
                    nc = size_default;
                    error_free(errp);
                    errp = NULL;
                }
                
                reg.x[i] = g_renew(uint64_t, reg.x[i], reg.n + 1);
                reg.x[i][reg.n] = nc ?
                    qemu_fdt_getprop_sized_cell(fdti->fdt, node_path,
                                                extended ? "reg-extended"
                                                         : "reg",
                                                cell_idx, nc, &errp)
                    : 0;
                cell_idx += nc;
                if (errp) {
                    goto exit_reg_parse;
                }
            }
        }
exit_reg_parse:

        if (object_dynamic_cast(dev, TYPE_FDT_GENERIC_MMAP)) {
            FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_GET_CLASS(dev);
            if (fmc->parse_reg) {
                while (fmc->parse_reg(FDT_GENERIC_MMAP(dev), reg,
                                      &error_abort)) {
                    fdt_init_yield(fdti);
                }
            }
        }
    }
    
    if (object_dynamic_cast(dev, TYPE_SYS_BUS_DEVICE)) {
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
            char *irq_info_p = irq_info;
            bool map_mode;
            int len = -1;
            qemu_irq *irqs = fdt_get_irq_info(fdti, node_path, i, irq_info,
                                              &map_mode);
            /* INTCs inferr their top level, if no IRQ connection specified */
            fdt_get_property(fdti->fdt, fdt_path_offset(fdti->fdt, node_path),
                             "interrupts-extended", &len);
            if (!irqs && is_intc && i == 0 && len <= 0) {
                FDTGenericIntc *id = (FDTGenericIntc *)object_dynamic_cast(
                                        dev, TYPE_FDT_GENERIC_INTC);
                FDTGenericIntcClass *idc = FDT_GENERIC_INTC_GET_CLASS(id);
                if (id && idc->auto_parent) {
                    Error *err = NULL;
                    idc->auto_parent(id, &err);
                } else {
                    irqs = fdti->irq_base;
                }
            }
            if (!irqs) {
                break;
            }
            while (*irqs) {
                FDTIRQConnection *irq = g_new0(FDTIRQConnection, 1);
                *irq = (FDTIRQConnection) {
                    .dev = DEVICE(dev),
                    .name = SYSBUS_DEVICE_GPIO_IRQ,
                    .merge_fn = qemu_irq_shared_or_handler,
                    .i = j,
                    .irq = *irqs,
                    .sink_info = g_strdup(irq_info_p),
                    .next = fdti->irqs
                };
                if (!map_mode) {
                    j++;
                } else {
                    irq_info_p += strlen(irq_info_p) + 1;
                }
                fdti->irqs = irq;
                irqs++;
            }
            if (map_mode) {
                j++;
            }
        }
    }

    if (object_dynamic_cast(dev, TYPE_FDT_GENERIC_GPIO)) {
        fggc = FDT_GENERIC_GPIO_GET_CLASS(dev);
        gpio_set = fggc->client_gpios;
    }

    if (!gpio_set) {
        gpio_set = default_gpio_sets;
    }

    for (; object_dynamic_cast(dev, TYPE_DEVICE) && gpio_set->names;
           gpio_set++) {
        bool end = false;
        int cur_cell = 0;

        for (i = 0; !end; i++) {
            char *debug_success;
            const FDTGenericGPIOConnection *c = gpio_set->gpios;
            const char *gpio_name = NULL;
            uint16_t named_idx = 0;
            qemu_irq input, output;
            memset(&input, 0, sizeof(input));

            if (c) {
                uint16_t range = c->range ? c->range : 1;
                while ((c->fdt_index > i || c->fdt_index + range <= i)
                       && c->name) {
                    c++;
                }
                named_idx = i - c->fdt_index;
                gpio_name = c->name;
            }
            if (!gpio_name) {
                const char *names_propname = gpio_set->names->names_propname;
                gpio_name = qemu_fdt_getprop_string(fdti->fdt, node_path,
                                                    names_propname, i, false,
                                                    NULL);
            }
            if (!gpio_name) {
                input = qdev_get_gpio_in(DEVICE(dev), i);
            } else {
                input = qdev_get_gpio_in_named(DEVICE(dev), gpio_name,
                                               named_idx);
            }
            debug_success = g_strdup_printf("Wiring GPIO input %s[%" PRId16 "] "
                                            "to", gpio_name, named_idx);
            output = fdt_get_gpio(fdti, node_path, &cur_cell, input, gpio_set,
                                  debug_success, &end);
            g_free(debug_success);
            if (output) {
                FDTIRQConnection *irq = g_new0(FDTIRQConnection, 1);
                *irq = (FDTIRQConnection) {
                    .dev = DEVICE(dev),
                    .name = gpio_name,
                    .merge_fn = qemu_irq_shared_or_handler,
                    .i = named_idx,
                    .irq = output,
                    .sink_info = NULL, /*FIXME */
                    .next = fdti->irqs
                };
                fdti->irqs = irq;
                DB_PRINT_NP(1, "... GPIO output %s[%" PRId16 "]\n", gpio_name,
                            named_idx);
            }
        }
    }

    if (dev_type) {
        g_free(dev_type);
    }

    return 0;
}

static const TypeInfo fdt_generic_intc_info = {
    .name          = TYPE_FDT_GENERIC_INTC,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(FDTGenericIntcClass),
};

static const TypeInfo fdt_generic_mmap_info = {
    .name          = TYPE_FDT_GENERIC_MMAP,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(FDTGenericMMapClass),
};

static const TypeInfo fdt_generic_gpio_info = {
    .name          = TYPE_FDT_GENERIC_GPIO,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(FDTGenericGPIOClass),
};

static void fdt_generic_intc_register_types(void)
{
    type_register_static(&fdt_generic_intc_info);
    type_register_static(&fdt_generic_mmap_info);
    type_register_static(&fdt_generic_gpio_info);
}

type_init(fdt_generic_intc_register_types)
