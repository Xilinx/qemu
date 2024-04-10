#include "qemu/osdep.h"
#include "qom/object.h"

static const TypeInfo fdt_qom_aliases[] = {
    {   .name = "ethernet-phy-id2000.a231", .parent = "dp83867"        },
    {   .name = "ethernet-phy-id2000.a131", .parent = "dp83826"        },
    {   .name = "ethernet-phy-id0141.0e50", .parent = "88e1116"        },
    {   .name = "ethernet-phy-id0141.0e10", .parent = "88e1118r"       },
    {   .name = "ethernet-phy-id0141.0dd0", .parent = "88e1510"        },
    {   .name = "ethernet-phy-id0283.bc30", .parent = "ADIN1300"       },
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
