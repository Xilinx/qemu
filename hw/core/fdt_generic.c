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

#include "hw/fdt_generic.h"
#include "block/coroutine.h"

#ifndef FDT_GENERIC_ERR_DEBUG
#define FDT_GENERIC_ERR_DEBUG 0
#endif
#define DB_PRINT(...) do { \
    if (FDT_GENERIC_ERR_DEBUG) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

#define FDT_GENERIC_MAX_PATTERN_LEN 1024

typedef struct TableListNode {
    void *next;
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
    nn->next = (void *)(*head_p);
    strcpy(nn->key, key);
    nn->fdt_init = fdt_init;
    nn->opaque = opaque;
    *head_p = nn;
}

/* FIXME: add return codes that differentiate between not found and error */

/* search a table for a key string and call the fdt init function if found.
 * Returns 0 if a match if found, 1 otherwise
 */

static int fdt_init_search_table(
        char *node_path,
        FDTMachineInfo *fdti,
        const char *key, /* string to match */
        TableListNode **head) /* head of the list to search */
{
    TableListNode *c = *head;
    if (c == NULL) {
        return 1;
    } else if (!strcmp(key, c->key)) {
        return c->fdt_init ? c->fdt_init(node_path, fdti, c->opaque) : 0;
    }
    return fdt_init_search_table(node_path, fdti, key,
        (TableListNode **)(&(*head)->next));
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

TableListNode *force_list_head;

void add_to_force_table(FDTInitFn fdt_init, const char *name, void *opaque)
{
    add_to_table(fdt_init, name, opaque, &force_list_head);
}

int fdt_force_bind_all(FDTMachineInfo *fdti)
{
    int ret = 0;
    while (force_list_head != NULL) {
        TableListNode *to_delete = force_list_head;
        ret |= force_list_head->fdt_init(NULL, fdti, force_list_head->opaque);
        force_list_head = force_list_head->next;
        free(to_delete);
    }
    return ret;
}

static void dump_table(TableListNode *head)
{
    if (head == NULL) {
        return;
    }
    printf("key : %s, opaque data %p\n", head->key, head->opaque);
    dump_table(head->next);
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

    DB_PRINT("yield #%d %p\n", this_yield, fdti->cq);
    qemu_co_queue_wait(fdti->cq);
    DB_PRINT("unyield #%d\n", this_yield);
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
    FDTDevOpaque *dp;
    for (dp = fdti->dev_opaques; dp->node_path; dp++) {
        g_free(dp->node_path);
    }
    g_free(fdti->dev_opaques);
    g_free(fdti);
}
