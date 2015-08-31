#ifndef MEMORY_ATTR_H__
#define MEMORY_ATTR_H__
#include "qom/object.h"

typedef struct MemoryTransactionAttr
{
    Object parent_obj;
    bool secure;
    uint64_t master_id;
} MemoryTransactionAttr;
#endif
