#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu-common.h"

static const TypeInfo fdt_qom_aliases [] = {
    {   .name = "xlnx.microblaze",          .parent = "microblaze-cpu"      },
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
