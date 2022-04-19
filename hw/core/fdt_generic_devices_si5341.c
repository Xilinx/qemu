#include "qemu/osdep.h"
#include "qom/object.h"

static const TypeInfo fdt_qom_aliases[] = {
    {
        .name = "silabs,si5341",
        .parent = "si5341"
    },
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
