/*
 * Tables of FDT device models and their init functions. Keyed by compatibility
 * strings, device instance names.
 *
 * Copyright (c) 2010 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2010 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>.
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
#include "hw/fdt_generic.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/coroutine.h"
#include "qemu/log.h"
#include "hw/cpu/cluster.h"
#include "sysemu/reset.h"

#ifndef FDT_GENERIC_ERR_DEBUG
#define FDT_GENERIC_ERR_DEBUG 0
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ": %s: ", __func__); \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0);

#define FDT_GENERIC_MAX_PATTERN_LEN 1024

typedef struct TableListNode {
    struct TableListNode *next;
    char key[FDT_GENERIC_MAX_PATTERN_LEN];
    FDTInitFn fdt_init;
    void *opaque;
} TableListNode;

/* add a node to the table specified by *head_p */

static void add_to_table(
        FDTInitFn fdt_init,
        const char *key,
        void *opaque,
        TableListNode **head_p)
{
    TableListNode *nn = malloc(sizeof(*nn));
    nn->next = *head_p;
    strcpy(nn->key, key);
    nn->fdt_init = fdt_init;
    nn->opaque = opaque;
    *head_p = nn;
}

/* FIXME: add return codes that differentiate between not found and error */

/* search a table for a key string and call the fdt init function if found.
 * Returns 0 if a match is found, 1 otherwise
 */

static int fdt_init_search_table(
        char *node_path,
        FDTMachineInfo *fdti,
        const char *key, /* string to match */
        TableListNode **head) /* head of the list to search */
{
    TableListNode *iter;

    for (iter = *head; iter != NULL; iter = iter->next) {
        if (!strcmp(key, iter->key)) {
            if (iter->fdt_init) {
                return iter->fdt_init(node_path, fdti, iter->opaque);
            }
            return 0;
        }
    }

    return 1;
}

TableListNode *compat_list_head;

void add_to_compat_table(FDTInitFn fdt_init, const char *compat, void *opaque)
{
    add_to_table(fdt_init, compat, opaque, &compat_list_head);
}

int fdt_init_compat(char *node_path, FDTMachineInfo *fdti, const char *compat)
{
    return fdt_init_search_table(node_path, fdti, compat, &compat_list_head);
}

TableListNode *inst_bind_list_head;

void add_to_inst_bind_table(FDTInitFn fdt_init, const char *name, void *opaque)
{
    add_to_table(fdt_init, name, opaque, &inst_bind_list_head);
}

int fdt_init_inst_bind(char *node_path, FDTMachineInfo *fdti,
        const char *name)
{
    return fdt_init_search_table(node_path, fdti, name, &inst_bind_list_head);
}

static void dump_table(TableListNode *head)
{
    TableListNode *iter;

    for (iter = head; iter != NULL; iter = iter->next) {
        printf("key : %s, opaque data %p\n", head->key, head->opaque);
    }
}

void dump_compat_table(void)
{
    printf("FDT COMPATIBILITY TABLE:\n");
    dump_table(compat_list_head);
}

void dump_inst_bind_table(void)
{
    printf("FDT INSTANCE BINDING TABLE:\n");
    dump_table(inst_bind_list_head);
}

void fdt_init_yield(FDTMachineInfo *fdti)
{
    static int yield_index;
    int this_yield = yield_index++;

    DB_PRINT(1, "Yield #%d\n", this_yield);
    qemu_co_queue_wait(fdti->cq, NULL);
    DB_PRINT(1, "Unyield #%d\n", this_yield);
}

void fdt_init_set_opaque(FDTMachineInfo *fdti, char *node_path, void *opaque)
{
    FDTDevOpaque *dp;
    for (dp = fdti->dev_opaques;
        dp->node_path && strcmp(dp->node_path, node_path);
        dp++);
    if (!dp->node_path) {
        dp->node_path = strdup(node_path);
    }
    dp->opaque = opaque;
}

int fdt_init_has_opaque(FDTMachineInfo *fdti, char *node_path)
{
    FDTDevOpaque *dp;
    for (dp = fdti->dev_opaques; dp->node_path; dp++) {
        if (!strcmp(dp->node_path, node_path)) {
            return 1;
         }
    }
    return 0;
}

static void *fdt_init_add_cpu_cluster(FDTMachineInfo *fdti, char *compat)
{
	static int i = 0;
	FDTCPUCluster *cl = g_malloc0(sizeof(*cl));
	char *name = g_strdup_printf("cluster%d", i);
	Object *obj;

	obj = object_new(TYPE_CPU_CLUSTER);
	object_property_add_child(object_get_root(), name, OBJECT(obj));
	qdev_prop_set_uint32(DEVICE(obj), "cluster-id", i++);

	cl->cpu_type = g_strdup(compat);
	cl->cpu_cluster = obj;
	cl->next = fdti->clusters;

	fdti->clusters = cl;

	g_free(name);

	return obj;
}

void *fdt_init_get_cpu_cluster(FDTMachineInfo *fdti, char *compat)
{
	FDTCPUCluster *cl = fdti->clusters;

	while (cl) {
		if (!strcmp(compat, cl->cpu_type)) {
			return cl->cpu_cluster;
		}
		cl = cl->next;
	}

	/* No cluster found so create and return a new one */
	return fdt_init_add_cpu_cluster(fdti, compat);
}

void *fdt_init_get_opaque(FDTMachineInfo *fdti, char *node_path)
{
    FDTDevOpaque *dp;
    for (dp = fdti->dev_opaques; dp->node_path; dp++) {
        if (!strcmp(dp->node_path, node_path)) {
            return dp->opaque;
        }
    }
    return NULL;
}

FDTMachineInfo *fdt_init_new_fdti(void *fdt)
{
    FDTMachineInfo *fdti = g_malloc0(sizeof(*fdti));
    fdti->fdt = fdt;
    fdti->cq = g_malloc0(sizeof(*(fdti->cq)));
    qemu_co_queue_init(fdti->cq);
    fdti->dev_opaques = g_malloc0(sizeof(*(fdti->dev_opaques)) *
        (devtree_get_num_nodes(fdt) + 1));
    return fdti;
}

void fdt_init_destroy_fdti(FDTMachineInfo *fdti)
{
    FDTCPUCluster *cl = fdti->clusters;
    FDTDevOpaque *dp;

    while (cl) {
	FDTCPUCluster *tmp = cl;
	cl = cl->next;
        g_free(tmp->cpu_type);
        g_free(tmp);
    }
    for (dp = fdti->dev_opaques; dp->node_path; dp++) {
        g_free(dp->node_path);
    }
    g_free(fdti->dev_opaques);
    g_free(fdti);
}
