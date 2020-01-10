/*
 * Instantiate TYPE_USER_CREATABLE from fdt generic framework
 *
 * Copyright (c) 2019 Xilinx, Inc.
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
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qobject-input-visitor.h"
#include "qom/object_interfaces.h"
#include "hw/fdt_generic.h"

#include <libfdt.h>

/*
 * Create a QDict using:
 * 1. Keys from defined properties of given TYPE_USER_CREATBLE class, and
 * 2. String values of same keys from given FDT node.
 *
 * Only writable properties of the given class are transferred.
 *
 * It is valid to support only string-valued properties from given
 * FDT node because user-creatable objects are normally created from
 * string-valued cmdline options.
 */
static QDict *user_creatble_fdt_props_to_qdict(void *fdt,
                                               const char *node_path,
                                               ObjectClass *klass,
                                               Error **errp)
{
    QDict *qprops;
    int offset;

    /*
     * To gracefully default properties missing from FDT-node,
     * use enumeration instead of direct lookup.
     */
    qprops = qdict_new();
    offset = fdt_path_offset(fdt, node_path);
    for (offset = fdt_first_property_offset(fdt, offset);
         offset != -FDT_ERR_NOTFOUND;
         offset = fdt_next_property_offset(fdt, offset)) {
        ObjectProperty *klass_prop;
        const char *key, *val;
        int len;

        len = 0;
        key = NULL;
        val = fdt_getprop_by_offset(fdt, offset, &key, &len);
        if (!val) {
            if (len < 0) {
                error_setg(errp, "%s: fdt_getprop offset=%d error: %d",
                           node_path, offset, len);
                qobject_unref(qprops);
                return NULL;
            }

            continue;
        }

        if ((len <= 0) || (val[len - 1] != '\0')) {
            continue;   /* Not a string value */
        }

        if (!key || !key[0]) {
            continue;   /* Just sanity check */
        }

        klass_prop = object_class_property_find(klass, key, NULL);
        if (!klass_prop || !klass_prop->set) {
            continue;   /* Not a class property, or non-settable */
        }

        qdict_put_str(qprops, key, val);
    }

    return qprops;
}

static Object *user_creatable_from_fdt(void *fdt, const char *node_path,
                                       ObjectClass *klass, const char *id,
                                       Error **errp)
{
    QDict *props;
    Visitor *v;
    Object *obj;

    props = user_creatble_fdt_props_to_qdict(fdt, node_path, klass, errp);
    if (!props) {
        return NULL;
    }

    v = qobject_input_visitor_new(QOBJECT(props));
    obj = user_creatable_add_type(object_class_get_name(klass), id,
                                  props, v, errp);

    visit_free(v);
    qobject_unref(props);

    return obj;
}

static int user_creatable_fdt_init(char *node_path, FDTMachineInfo *fdti,
                                   void *priv)
{
    const char *type = priv;
    Error **errp = &error_abort;
    void *fdt = fdti->fdt;

    ObjectClass *klass;
    Object *obj;
    char *obj_id;

    /* check usage of fdt_register_compatibility_opaque() */
    assert(type != NULL);

    /* Validate FDT path and type */
    obj_id = qemu_devtree_get_node_name(fdt, node_path);
    if (!obj_id) {
        error_setg(&error_abort, "FDT '%s<%s>': Failed to get name.",
                   node_path, type);
    }

    klass = object_class_by_name(type);
    if (!klass) {
        error_setg(&error_abort, "FDT '%s<%s>': Unsupported type.",
                   node_path, type);
    }

    /*
     * Cmdline-created instance takes precedence over FDT, but
     * the type must be compatible.
     */
    obj = object_resolve_path_component(object_get_objects_root(), obj_id);
    if (!obj) {
        obj = user_creatable_from_fdt(fdt, node_path, klass, obj_id, errp);
    } else if (!object_dynamic_cast(obj, type)) {
        error_setg(errp,
                   "FDT '%s<%s>': incompatible with cmdline-created '%s<%s>'",
                   node_path, type,
                   object_get_canonical_path(obj), object_get_typename(obj));
    }

    fdt_init_set_opaque(fdti, node_path, obj);
    g_free(obj_id);
    return 0;
}

fdt_register_compatibility_opaque(user_creatable_fdt_init,
                                  "compatible:secret", 1,
                                  (char *)"secret");

/*
 * This is just in case DTB places a user-creatable node inside
 * a container, .e.g, "/objects".
 */
static int container_fdt_init(char *node_path, FDTMachineInfo *fdti,
                              void *priv)
{
    fdt_init_set_opaque(fdti, node_path,
                        container_get(object_get_root(), node_path));
    return 0;
}
fdt_register_compatibility(container_fdt_init, "compatible:container");
