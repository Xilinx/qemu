/*
 * Header with function prototypes to help device tree manipulation using
 * libfdt. It also provides functions to read entries from device tree proc
 * interface.
 *
 * Copyright 2008 IBM Corporation.
 * Authors: Jerone Young <jyoung5@us.ibm.com>
 *          Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#ifndef __DEVICE_TREE_H__
#define __DEVICE_TREE_H__

#include "qemu-common.h"
#include "qapi/qmp/qerror.h"

void *create_device_tree(int *sizep);
void *load_device_tree(const char *filename_path, int *sizep);

/* property setters */

int qemu_devtree_setprop(void *fdt, const char *node_path,
                         const char *property, const void *val_array, int size);
int qemu_devtree_setprop_cell(void *fdt, const char *node_path,
                              const char *property, uint32_t val);
int qemu_devtree_setprop_u64(void *fdt, const char *node_path,
                             const char *property, uint64_t val);
int qemu_devtree_setprop_string(void *fdt, const char *node_path,
                                const char *property, const char *string);
int qemu_devtree_setprop_phandle(void *fdt, const char *node_path,
                                 const char *property,
                                 const char *target_node_path);
void *qemu_devtree_getprop(void *fdt, const char *node_path,
                                 const char *property, int *lenp,
                                 bool inherit, Error **errp);
uint32_t qemu_devtree_getprop_cell(void *fdt, const char *node_path,
                                   const char *property, int offset,
                                   bool inherit, Error **errp);
uint32_t qemu_devtree_get_phandle(void *fdt, const char *path);
uint32_t qemu_devtree_alloc_phandle(void *fdt);
int qemu_devtree_nop_node(void *fdt, const char *node_path);
int qemu_devtree_add_subnode(void *fdt, const char *name);

#define qemu_devtree_setprop_cells(fdt, node_path, property, ...)             \
    do {                                                                      \
        uint32_t qdt_tmp[] = { __VA_ARGS__ };                                 \
        int i;                                                                \
                                                                              \
        for (i = 0; i < ARRAY_SIZE(qdt_tmp); i++) {                           \
            qdt_tmp[i] = cpu_to_be32(qdt_tmp[i]);                             \
        }                                                                     \
        qemu_devtree_setprop(fdt, node_path, property, qdt_tmp,               \
                             sizeof(qdt_tmp));                                \
    } while (0)

void qemu_devtree_dumpdtb(void *fdt, int size);

typedef struct QEMUDevtreeProp {
    char *name;
    int len;
    void *value;
} QEMUDevtreeProp;

/* node queries */

char *qemu_devtree_get_node_name(void *fdt, const char *node_path);
int qemu_devtree_get_node_depth(void *fdt, const char *node_path);
int qemu_devtree_get_num_children(void *fdt, const char *node_path, int depth);
char **qemu_devtree_get_children(void *fdt, const char *node_path, int depth);
int qemu_devtree_num_props(void *fdt, const char *node_path);
QEMUDevtreeProp *qemu_devtree_get_props(void *fdt, const char *node_path);

/* node getters */

int qemu_devtree_node_by_compatible(void *fdt, char *node_path,
                        const char *compats);
int qemu_devtree_get_node_by_name(void *fdt, char *node_path,
                        const char *cmpname);
int qemu_devtree_get_node_by_phandle(void *fdt, char *node_path, int phandle);
int qemu_devtree_getparent(void *fdt, char *node_path,
                        const char *current);
int qemu_devtree_get_root_node(void *fdt, char *node_path);

/* misc */

int devtree_get_num_nodes(void *fdt);
void devtree_info_dump(void *fdt);

#define DT_PATH_LENGTH 1024

#endif /* __DEVICE_TREE_H__ */
