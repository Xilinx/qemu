/*
 * Functions to help device tree manipulation using libfdt.
 * It also provides functions to read entries from device tree proc
 * interface.
 *
 * Copyright 2008 IBM Corporation.
 * Authors: Jerone Young <jyoung5@us.ibm.com>
 *          Hollis Blanchard <hollisb@us.ibm.com>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 */

#include "qemu/osdep.h"

#ifdef CONFIG_LINUX
#include <dirent.h>
#endif

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/bswap.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "qemu/config-file.h"
#include "qemu/log.h"

#include <libfdt.h>

#define FDT_MAX_SIZE  0x100000

void *create_device_tree(int *sizep)
{
    void *fdt;
    int ret;

    *sizep = FDT_MAX_SIZE;
    fdt = g_malloc0(FDT_MAX_SIZE);
    ret = fdt_create(fdt, FDT_MAX_SIZE);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_finish_reservemap(fdt);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_begin_node(fdt, "");
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_end_node(fdt);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_finish(fdt);
    if (ret < 0) {
        goto fail;
    }
    ret = fdt_open_into(fdt, fdt, *sizep);
    if (ret) {
        error_report("Unable to copy device tree in memory");
        exit(1);
    }

    return fdt;
fail:
    error_report("%s Couldn't create dt: %s", __func__, fdt_strerror(ret));
    exit(1);
}

void *load_device_tree(const char *filename_path, int *sizep)
{
    int dt_size;
    int dt_file_load_size;
    int ret;
    void *fdt = NULL;

    if (sizep) {
        *sizep = 0;
    }
    dt_size = get_image_size(filename_path);
    if (dt_size < 0) {
        error_report("Unable to get size of device tree file '%s'",
                     filename_path);
        goto fail;
    }
    if (dt_size > INT_MAX / 2 - 10000) {
        error_report("Device tree file '%s' is too large", filename_path);
        goto fail;
    }

    /* Expand to 2x size to give enough room for manipulation.  */
    dt_size += 10000;
    dt_size *= 2;
    /* First allocate space in qemu for device tree */
    fdt = g_malloc0(dt_size);

    dt_file_load_size = load_image_size(filename_path, fdt, dt_size);
    if (dt_file_load_size < 0) {
        error_report("Unable to open device tree file '%s'",
                     filename_path);
        goto fail;
    }

    ret = fdt_open_into(fdt, fdt, dt_size);
    if (ret) {
        error_report("Unable to copy device tree in memory");
        goto fail;
    }

    /* Check sanity of device tree */
    if (fdt_check_header(fdt)) {
        error_report("Device tree file loaded into memory is invalid: %s",
                     filename_path);
        goto fail;
    }
    if (sizep) {
        *sizep = dt_size;
    }
    return fdt;

fail:
    g_free(fdt);
    return NULL;
}

#ifdef CONFIG_LINUX

#define SYSFS_DT_BASEDIR "/proc/device-tree"

/**
 * read_fstree: this function is inspired from dtc read_fstree
 * @fdt: preallocated fdt blob buffer, to be populated
 * @dirname: directory to scan under SYSFS_DT_BASEDIR
 * the search is recursive and the tree is searched down to the
 * leaves (property files).
 *
 * the function asserts in case of error
 */
static void read_fstree(void *fdt, const char *dirname)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    const char *root_dir = SYSFS_DT_BASEDIR;
    const char *parent_node;

    if (strstr(dirname, root_dir) != dirname) {
        error_report("%s: %s must be searched within %s",
                     __func__, dirname, root_dir);
        exit(1);
    }
    parent_node = &dirname[strlen(SYSFS_DT_BASEDIR)];

    d = opendir(dirname);
    if (!d) {
        error_report("%s cannot open %s", __func__, dirname);
        exit(1);
    }

    while ((de = readdir(d)) != NULL) {
        char *tmpnam;

        if (!g_strcmp0(de->d_name, ".")
            || !g_strcmp0(de->d_name, "..")) {
            continue;
        }

        tmpnam = g_strdup_printf("%s/%s", dirname, de->d_name);

        if (lstat(tmpnam, &st) < 0) {
            error_report("%s cannot lstat %s", __func__, tmpnam);
            exit(1);
        }

        if (S_ISREG(st.st_mode)) {
            gchar *val;
            gsize len;

            if (!g_file_get_contents(tmpnam, &val, &len, NULL)) {
                error_report("%s not able to extract info from %s",
                             __func__, tmpnam);
                exit(1);
            }

            if (strlen(parent_node) > 0) {
                qemu_fdt_setprop(fdt, parent_node,
                                 de->d_name, val, len);
            } else {
                qemu_fdt_setprop(fdt, "/", de->d_name, val, len);
            }
            g_free(val);
        } else if (S_ISDIR(st.st_mode)) {
            char *node_name;

            node_name = g_strdup_printf("%s/%s",
                                        parent_node, de->d_name);
            qemu_fdt_add_subnode(fdt, node_name);
            g_free(node_name);
            read_fstree(fdt, tmpnam);
        }

        g_free(tmpnam);
    }

    closedir(d);
}

/* load_device_tree_from_sysfs: extract the dt blob from host sysfs */
void *load_device_tree_from_sysfs(void)
{
    void *host_fdt;
    int host_fdt_size;

    host_fdt = create_device_tree(&host_fdt_size);
    read_fstree(host_fdt, SYSFS_DT_BASEDIR);
    if (fdt_check_header(host_fdt)) {
        error_report("%s host device tree extracted into memory is invalid",
                     __func__);
        exit(1);
    }
    return host_fdt;
}

#endif /* CONFIG_LINUX */

static int findnode_nofail(void *fdt, const char *node_path)
{
    int offset;

    offset = fdt_path_offset(fdt, node_path);
    if (offset < 0) {
        error_report("%s Couldn't find node %s: %s", __func__, node_path,
                     fdt_strerror(offset));
        exit(1);
    }

    return offset;
}

char **qemu_fdt_node_unit_path(void *fdt, const char *name, Error **errp)
{
    char *prefix =  g_strdup_printf("%s@", name);
    unsigned int path_len = 16, n = 0;
    GSList *path_list = NULL, *iter;
    const char *iter_name;
    int offset, len, ret;
    char **path_array;

    offset = fdt_next_node(fdt, -1, NULL);

    while (offset >= 0) {
        iter_name = fdt_get_name(fdt, offset, &len);
        if (!iter_name) {
            offset = len;
            break;
        }
        if (!strcmp(iter_name, name) || g_str_has_prefix(iter_name, prefix)) {
            char *path;

            path = g_malloc(path_len);
            while ((ret = fdt_get_path(fdt, offset, path, path_len))
                  == -FDT_ERR_NOSPACE) {
                path_len += 16;
                path = g_realloc(path, path_len);
            }
            path_list = g_slist_prepend(path_list, path);
            n++;
        }
        offset = fdt_next_node(fdt, offset, NULL);
    }
    g_free(prefix);

    if (offset < 0 && offset != -FDT_ERR_NOTFOUND) {
        error_setg(errp, "%s: abort parsing dt for %s node units: %s",
                   __func__, name, fdt_strerror(offset));
        for (iter = path_list; iter; iter = iter->next) {
            g_free(iter->data);
        }
        g_slist_free(path_list);
        return NULL;
    }

    path_array = g_new(char *, n + 1);
    path_array[n--] = NULL;

    for (iter = path_list; iter; iter = iter->next) {
        path_array[n--] = iter->data;
    }

    g_slist_free(path_list);

    return path_array;
}

char **qemu_fdt_node_path(void *fdt, const char *name, const char *compat,
                          Error **errp)
{
    int offset, len, ret;
    const char *iter_name;
    unsigned int path_len = 16, n = 0;
    GSList *path_list = NULL, *iter;
    char **path_array;

    offset = fdt_node_offset_by_compatible(fdt, -1, compat);

    while (offset >= 0) {
        iter_name = fdt_get_name(fdt, offset, &len);
        if (!iter_name) {
            offset = len;
            break;
        }
        if (!name || !strcmp(iter_name, name)) {
            char *path;

            path = g_malloc(path_len);
            while ((ret = fdt_get_path(fdt, offset, path, path_len))
                  == -FDT_ERR_NOSPACE) {
                path_len += 16;
                path = g_realloc(path, path_len);
            }
            path_list = g_slist_prepend(path_list, path);
            n++;
        }
        offset = fdt_node_offset_by_compatible(fdt, offset, compat);
    }

    if (offset < 0 && offset != -FDT_ERR_NOTFOUND) {
        error_setg(errp, "%s: abort parsing dt for %s/%s: %s",
                   __func__, name, compat, fdt_strerror(offset));
        for (iter = path_list; iter; iter = iter->next) {
            g_free(iter->data);
        }
        g_slist_free(path_list);
        return NULL;
    }

    path_array = g_new(char *, n + 1);
    path_array[n--] = NULL;

    for (iter = path_list; iter; iter = iter->next) {
        path_array[n--] = iter->data;
    }

    g_slist_free(path_list);

    return path_array;
}

int qemu_fdt_setprop(void *fdt, const char *node_path,
                     const char *property, const void *val, int size)
{
    int r;

    r = fdt_setprop(fdt, findnode_nofail(fdt, node_path), property, val, size);
    if (r < 0) {
        error_report("%s: Couldn't set %s/%s: %s", __func__, node_path,
                     property, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_setprop_cell(void *fdt, const char *node_path,
                          const char *property, uint32_t val)
{
    int r;

    r = fdt_setprop_cell(fdt, findnode_nofail(fdt, node_path), property, val);
    if (r < 0) {
        error_report("%s: Couldn't set %s/%s = %#08x: %s", __func__,
                     node_path, property, val, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_setprop_u64(void *fdt, const char *node_path,
                         const char *property, uint64_t val)
{
    val = cpu_to_be64(val);
    return qemu_fdt_setprop(fdt, node_path, property, &val, sizeof(val));
}

int qemu_fdt_setprop_string(void *fdt, const char *node_path,
                            const char *property, const char *string)
{
    int r;

    r = fdt_setprop_string(fdt, findnode_nofail(fdt, node_path), property, string);
    if (r < 0) {
        error_report("%s: Couldn't set %s/%s = %s: %s", __func__,
                     node_path, property, string, fdt_strerror(r));
        exit(1);
    }

    return r;
}

void *qemu_fdt_getprop(void *fdt, const char *node_path,
                             const char *property, int *lenp,
                             bool inherit, Error **errp)
{
    int len;
    const void *r;

    if (!lenp) {
        lenp = &len;
    }
    r = fdt_getprop(fdt, findnode_nofail(fdt, node_path), property, lenp);
    if (!r) {
        char parent[DT_PATH_LENGTH];
        if (inherit && !qemu_devtree_getparent(fdt, parent, node_path)) {
            return qemu_fdt_getprop(fdt, parent, property, lenp, true, errp);
        }
        error_setg(errp, "%s: Couldn't get %s/%s: %s", __func__,
                  node_path, property, fdt_strerror(*lenp));
        return NULL;
    }
    return g_memdup(r, *lenp);
}

char *qemu_fdt_getprop_string(void *fdt, const char*node_path,
                              const char *property, int cell,
                              bool inherit, Error **errp)
{
    int len;
    void *prop;
    Error *err= NULL;

    if (!errp) {
        errp = &err;
    }

    prop = qemu_fdt_getprop(fdt, node_path, property, &len, inherit, errp);
    if (*errp) {
        return NULL;
    }
    while (cell) {
        void *term = memchr(prop, '\0', len);
        size_t diff;

        if (!term) {
            error_setg(errp, "%s: Couldn't get %s/%s: %s", __func__,
                      node_path, property, fdt_strerror(len));
            return NULL;
        }
        diff = term - prop + 1;
        len -= diff;
        assert(len >= 0);
        prop += diff;
        cell--;
    }
    if (!*(char *)prop) {
        error_setg(errp, "%s: Couldn't get %s/%s: %s", __func__,
                  node_path, property, fdt_strerror(len));
        return NULL;
    }
    return len ? prop : NULL;
}

uint32_t qemu_fdt_getprop_cell(void *fdt, const char *node_path,
                               const char *property, int offset,
                               bool inherit, Error **errp)
{
    int len;
    uint32_t ret;
    uint32_t *p = qemu_fdt_getprop(fdt, node_path, property, &len,
                                         inherit, errp);
    if (errp && *errp) {
        return 0;
    }
    if (len < (offset+1)*4) {
        error_setg(errp, "%s: %s/%s not 4 bytes long (not a cell?)",
                   __func__, node_path, property);
        return 0;
    }
    ret = be32_to_cpu(p[offset]);
    g_free(p);
    return ret;
}

uint64_t qemu_fdt_getprop_sized_cell(void *fdt, const char *node_path,
                                     const char *property, int offset,
                                     int size, Error **errp)
{
    uint64_t ret = 0;
    for (;size ;size--) {
        ret <<= 32;
        ret |= qemu_fdt_getprop_cell(fdt, node_path, property, offset++, false,
                                     errp);
        if (errp && *errp) {
            return 0;
        }
    }
    return ret;
}

uint32_t qemu_fdt_check_phandle(void *fdt, const char *path)
{
    uint32_t r;

    r = fdt_get_phandle(fdt, findnode_nofail(fdt, path));
    if (r == 0) {
        qemu_log("%s: Couldn't find phandle for %s: %s", __func__,
                 path, fdt_strerror(r));
    }

    return r;
}

uint32_t qemu_fdt_get_phandle(void *fdt, const char *path)
{
    uint32_t r;

    r = fdt_get_phandle(fdt, findnode_nofail(fdt, path));
    if (r == 0) {
        error_report("%s: Couldn't get phandle for %s: %s", __func__,
                     path, fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_setprop_phandle(void *fdt, const char *node_path,
                             const char *property,
                             const char *target_node_path)
{
    uint32_t phandle = qemu_fdt_get_phandle(fdt, target_node_path);
    return qemu_fdt_setprop_cell(fdt, node_path, property, phandle);
}

uint32_t qemu_fdt_alloc_phandle(void *fdt)
{
    static int phandle = 0x0;

    /*
     * We need to find out if the user gave us special instruction at
     * which phandle id to start allocating phandles.
     */
    if (!phandle) {
        phandle = machine_phandle_start(current_machine);
    }

    if (!phandle) {
        /*
         * None or invalid phandle given on the command line, so fall back to
         * default starting point.
         */
        phandle = 0x8000;
    }

    return phandle++;
}

int qemu_fdt_nop_node(void *fdt, const char *node_path)
{
    int r;

    r = fdt_nop_node(fdt, findnode_nofail(fdt, node_path));
    if (r < 0) {
        error_report("%s: Couldn't nop node %s: %s", __func__, node_path,
                     fdt_strerror(r));
        exit(1);
    }

    return r;
}

int qemu_fdt_add_subnode(void *fdt, const char *name)
{
    char *dupname = g_strdup(name);
    char *basename = strrchr(dupname, '/');
    int retval;
    int parent = 0;

    if (!basename) {
        g_free(dupname);
        return -1;
    }

    basename[0] = '\0';
    basename++;

    if (dupname[0]) {
        parent = findnode_nofail(fdt, dupname);
    }

    retval = fdt_add_subnode(fdt, parent, basename);
    if (retval < 0) {
        error_report("FDT: Failed to create subnode %s: %s", name,
                     fdt_strerror(retval));
        exit(1);
    }

    g_free(dupname);
    return retval;
}

void qemu_fdt_dumpdtb(void *fdt, int size)
{
    const char *dumpdtb = qemu_opt_get(qemu_get_machine_opts(), "dumpdtb");

    if (dumpdtb) {
        /* Dump the dtb to a file and quit */
        if (g_file_set_contents(dumpdtb, fdt, size, NULL)) {
            info_report("dtb dumped to %s. Exiting.", dumpdtb);
            exit(0);
        }
        error_report("%s: Failed dumping dtb to %s", __func__, dumpdtb);
        exit(1);
    }
}

int qemu_fdt_setprop_sized_cells_from_array(void *fdt,
                                            const char *node_path,
                                            const char *property,
                                            int numvalues,
                                            uint64_t *values)
{
    uint32_t *propcells;
    uint64_t value;
    int cellnum, vnum, ncells;
    uint32_t hival;
    int ret;

    propcells = g_new0(uint32_t, numvalues * 2);

    cellnum = 0;
    for (vnum = 0; vnum < numvalues; vnum++) {
        ncells = values[vnum * 2];
        if (ncells != 1 && ncells != 2) {
            ret = -1;
            goto out;
        }
        value = values[vnum * 2 + 1];
        hival = cpu_to_be32(value >> 32);
        if (ncells > 1) {
            propcells[cellnum++] = hival;
        } else if (hival != 0) {
            ret = -1;
            goto out;
        }
        propcells[cellnum++] = cpu_to_be32(value);
    }

    ret = qemu_fdt_setprop(fdt, node_path, property, propcells,
                           cellnum * sizeof(uint32_t));
out:
    g_free(propcells);
    return ret;
}

char *qemu_devtree_get_node_name(void *fdt, const char *node_path)
{
    const char *ret = fdt_get_name(fdt, fdt_path_offset(fdt, node_path), NULL);
    return ret ? strdup(ret) : NULL;
}

int qemu_devtree_get_node_depth(void *fdt, const char *node_path)
{
    return fdt_node_depth(fdt, fdt_path_offset(fdt, node_path));
}


int qemu_devtree_num_props(void *fdt, const char *node_path)
{
    int offset = fdt_path_offset(fdt, node_path);
    int ret = 0;

    for (offset = fdt_first_property_offset(fdt, offset);
            offset != -FDT_ERR_NOTFOUND;
            offset = fdt_next_property_offset(fdt, offset)) {
        ret++;
    }
    return ret;
}

QEMUDevtreeProp *qemu_devtree_prop_search(QEMUDevtreeProp *props,
                                          const char *name)
{
    while (props->name) {
        if (!strcmp(props->name, name)) {
            return props;
        }
        props++;
    }
    return NULL;
}

QEMUDevtreeProp *qemu_devtree_get_props(void *fdt, const char *node_path)
{
    QEMUDevtreeProp *ret = g_new0(QEMUDevtreeProp,
                                    qemu_devtree_num_props(fdt, node_path) + 1);
    int offset = fdt_path_offset(fdt, node_path);
    int i = 0;

    for (offset = fdt_first_property_offset(fdt, offset);
            offset != -FDT_ERR_NOTFOUND;
            offset = fdt_next_property_offset(fdt, offset)) {
        const char *propname;
        const void *val = fdt_getprop_by_offset(fdt, offset, &propname,
                                                    &ret[i].len);

        ret[i].name = g_strdup(propname);
        ret[i].value = g_memdup(val, ret[i].len);
        i++;
    }
    return ret;
}

static void qemu_devtree_children_info(void *fdt, const char *node_path,
        int depth, int *num, char **returned_paths) {
    int offset = fdt_path_offset(fdt, node_path);
    int root_depth = fdt_node_depth(fdt, offset);
    int cur_depth = root_depth;

    if (num) {
        *num = 0;
    }
    for (;;) {
        offset = fdt_next_node(fdt, offset, &cur_depth);
        if (cur_depth <= root_depth) {
            break;
        }
        if (cur_depth <= root_depth + depth || depth == 0) {
            if (returned_paths) {
                returned_paths[*num] = g_malloc0(DT_PATH_LENGTH);
                fdt_get_path(fdt, offset, returned_paths[*num], DT_PATH_LENGTH);
            }
            if (num) {
                (*num)++;
            }
        }
    }
}

char **qemu_devtree_get_children(void *fdt, const char *node_path, int depth)
{
    int num_children = qemu_devtree_get_num_children(fdt, node_path, depth);
    char **ret = g_malloc0(sizeof(*ret) * num_children);

    qemu_devtree_children_info(fdt, node_path, depth, &num_children, ret);
    return ret;
}

int qemu_devtree_get_num_children(void *fdt, const char *node_path, int depth)
{
    int ret;

    qemu_devtree_children_info(fdt, node_path, depth, &ret, NULL);
    return ret;
}

int qemu_devtree_node_by_compatible(void *fdt, char *node_path,
                        const char *compats)
{
    int offset = fdt_node_offset_by_compatible(fdt, 0, compats);
    return offset > 0 ?
        fdt_get_path(fdt, offset, node_path, DT_PATH_LENGTH) : 1;
}

int qemu_devtree_get_node_by_name(void *fdt, char *node_path,
        const char *cmpname) {
    int offset = 0;
    char *name = NULL;

    do {
        char *at;

        offset = fdt_next_node(fdt, offset, NULL);
        name = (void *)fdt_get_name(fdt, offset, NULL);
        if (!name) {
            continue;
        }
        at = memchr(name, '@', strlen(name));
        if (!strncmp(name, cmpname, at ? at - name : strlen(name) )) {
            break;
        }
    } while (offset > 0);
    return offset > 0 ?
        fdt_get_path(fdt, offset, node_path, DT_PATH_LENGTH) : 1;
}

int qemu_devtree_get_n_nodes_by_name(void *fdt, char ***array,
                                     const char *cmpname)
{
    int offset = 0;
    char *name = NULL;
    uint32_t n = 0;
    char node_p[DT_PATH_LENGTH];
    char **node_path = NULL;

    do {
        char *at;

        offset = fdt_next_node(fdt, offset, NULL);
        name = (void *)fdt_get_name(fdt, offset, NULL);

        if (!name) {
            continue;
        }

        at = memchr(name, '@', strlen(name));
        if (!strncmp(name, cmpname, at ? at - name : strlen(name))) {
            if (fdt_get_path(fdt, offset, node_p, DT_PATH_LENGTH) >= 0) {
                if (node_path == NULL) {
                    node_path = (char **) g_new(char *, 1);
                } else {
                    node_path = (char **) g_renew(char *, *node_path, n);
                }
                node_path[n] = g_strdup(node_p);
                n++;
            }
        }
    } while (offset > 0);

    *array = node_path;
    return n;
}

char *qemu_devtree_get_child_by_name(void *fdt, char *parent_path,
                                     const char *cmpname)
{
    int offset = 0;
    int parent_offset;
    int namelen = strlen(cmpname);
    char child_path[DT_PATH_LENGTH];

    parent_offset = fdt_path_offset(fdt, parent_path);

    if (parent_offset > 0) {
        offset = fdt_subnode_offset_namelen(fdt, parent_offset,
                                            cmpname, namelen);
        if (fdt_get_path(fdt, offset, child_path, DT_PATH_LENGTH) == 0) {
            return g_strdup(child_path);
        }
    }

    return NULL;
}

int qemu_devtree_get_node_by_phandle(void *fdt, char *node_path, int phandle)
{
    return fdt_get_path(fdt, fdt_node_offset_by_phandle(fdt, phandle),
                            node_path, DT_PATH_LENGTH);
}

int qemu_devtree_getparent(void *fdt, char *node_path, const char *current)
{
    int offset = fdt_path_offset(fdt, current);
    int parent_offset = fdt_supernode_atdepth_offset(fdt, offset,
        fdt_node_depth(fdt, offset) - 1, NULL);

    return parent_offset >= 0 ?
        fdt_get_path(fdt, parent_offset, node_path, DT_PATH_LENGTH) : 1;
}

int qemu_devtree_get_root_node(void *fdt, char *node_path)
{
    return fdt_get_path(fdt, 0, node_path, DT_PATH_LENGTH);
}

static void devtree_scan(void *fdt, int *num_nodes, int info_dump)
{
    int depth = 0, offset = 0;

    if (num_nodes) {
        *num_nodes = 0;
    }
    for (;;) {
        offset = fdt_next_node(fdt, offset, &depth);
        if (num_nodes) {
            (*num_nodes)++;
        }
        if (offset <= 0 || depth <= 0) {
            break;
        }

        if (info_dump) {
            char node_path[DT_PATH_LENGTH];
            char *all_compats = NULL;
            int compat_len;
            Error *errp = NULL;

            if (fdt_get_path(fdt, offset, node_path, DT_PATH_LENGTH)) {
                sprintf(node_path, "(none)");
            } else {
                all_compats = qemu_fdt_getprop(fdt, node_path, "compatible",
                                               &compat_len, false, &errp);
            }
            if (!errp) {
                char *i = all_compats;
                for (;;) {
                    char *j = memchr(i, '\0', DT_PATH_LENGTH);
                    compat_len -= ((j+1)-i);
                    if (!compat_len) {
                        break;
                    }
                    *j = ' ';
                    i = j+1;
                }
            }
            printf("OFFSET: %d, DEPTH: %d, PATH: %s, COMPATS: %s\n",
                    offset, depth, node_path,
                    all_compats ? all_compats : "(none)");
        }
    }
}

void devtree_info_dump(void *fdt)
{
    devtree_scan(fdt, NULL, 1);
}

int devtree_get_num_nodes(void *fdt)
{
    int ret;

    devtree_scan(fdt, &ret, 0);
    return ret;
}
