#include "qemu/osdep.h"
#include "qom/object.h"
#include "qemu-common.h"

static const TypeInfo fdt_qom_aliases [] = {
    {   .name = "arm.cortex-a9-twd-timer",  .parent = "arm_mptimer"         },
    {   .name = "xlnx.ps7-slcr",            .parent = "xilinx,zynq_slcr"    },
    {   .name = "xlnx.zynq-slcr",           .parent = "xilinx,zynq_slcr"    },
    {   .name = "arm.cortex-a9-gic",        .parent = "arm_gic"             },
    {   .name = "arm.gic",                  .parent = "arm_gic"             },
    {   .name = "arm.cortex-a9-scu",        .parent = "a9-scu"              },
#ifdef TARGET_AARCH64
    {   .name = "xilinx.cxtsgen",           .parent = "arm.generic-timer"   },
#endif
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
