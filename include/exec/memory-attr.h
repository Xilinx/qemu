#ifndef MEMORY_ATTR_H__
#define MEMORY_ATTR_H__

#include "qom/object.h"
#include "qapi/visitor-impl.h"

typedef struct MemoryTransactionAttr
{
    Object parent_obj;
    bool secure;
    uint64_t master_id;
} MemoryTransactionAttr;

void cpu_set_mr(Object *obj, Visitor *v, void *opaque,
                const char *name, Error **errp);

#endif
