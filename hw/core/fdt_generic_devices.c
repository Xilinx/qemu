#include "qemu/osdep.h"
#include "hw/fdt_generic_devices.h"
#include "qom/object.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"

#include "hw/qdev-core.h"

/* FIXME: This file should go away. When these devices are properly QOMified
 * then these FDT creations should happen automatically without need for these
 * explict shim functions
 */

/* Piggy back fdt_generic_util.c ERR_DEBUG symbol as these two are really the
 * same feature
 */

#ifndef FDT_GENERIC_UTIL_ERR_DEBUG
#define FDT_GENERIC_UTIL_ERR_DEBUG 1
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ": %s: ", __func__); \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0)

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, "%s", node_path); \
        DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0)

int fdt_generic_num_cpus;

static int i2c_bus_fdt_init(char *node_path, FDTMachineInfo *fdti, void *priv)
{
    Object *parent;
    DeviceState *dev;
    char parent_node_path[DT_PATH_LENGTH];
    char *node_name = qemu_devtree_get_node_name(fdti->fdt, node_path);

    DB_PRINT_NP(1, "\n");
    /* FIXME: share this code with fdt_generic_util.c/fdt_init_qdev() */
    if (qemu_devtree_getparent(fdti->fdt, parent_node_path, node_path)) {
        abort();
    }
    while (!fdt_init_has_opaque(fdti, parent_node_path)) {
        fdt_init_yield(fdti);
    }
    parent = fdt_init_get_opaque(fdti, parent_node_path);
    dev = (DeviceState *)object_dynamic_cast(parent, TYPE_DEVICE);
    if (parent && dev) {
        while (!dev->realized) {
            fdt_init_yield(fdti);
        }
        DB_PRINT_NP(0, "parenting i2c bus to %s bus %s\n", parent_node_path,
                 node_name);
        fdt_init_set_opaque(fdti, node_path,
                            qdev_get_child_bus(dev, node_name));
    } else {
        DB_PRINT_NP(0, "orphaning i2c bus\n");
    }
    return 0;
}

static int sysmem_fdt_init(char *node_path, FDTMachineInfo *fdti,
                           void *priv)
{
    fdt_init_set_opaque(fdti, node_path, OBJECT(get_system_memory()));
    return 0;
}

fdt_register_compatibility(sysmem_fdt_init, "compatible:qemu:system-memory");

static const void *null;

fdt_register_compatibility(null, "compatible:marvell,88e1111");
fdt_register_compatibility(null, "compatible:arm,pl310-cache");
fdt_register_compatibility(null, "compatible:xlnx,ps7-cortexa9-1.00.a");
fdt_register_compatibility(null, "compatible:xlnx,zynq_remoteproc");
fdt_register_compatibility(null, "compatible:xlnx,ps7-smcc-1.00.a");
fdt_register_compatibility(null, "compatible:xlnx,ps7-smc");
fdt_register_compatibility(null, "compatible:xlnx,ps7-nand-1.00.a");
fdt_register_compatibility(null, "compatible:xlnx,ps7-ram-1.00.a");
fdt_register_compatibility(null, "compatible:xlnx,ps7-ocm");
fdt_register_compatibility(null, "compatible:marvell,88e1118r");
fdt_register_compatibility(null, "compatible:xlnx,ps7-clkc");
fdt_register_compatibility(null, "compatible:xlnx,ps7-ddrc");
fdt_register_compatibility(null, "compatible:xlnx,ps7-scuc-1.00.a");
fdt_register_compatibility(null, "compatible:fixed-clock");
fdt_register_compatibility(null, "compatible:xlnx,pinctrl-zynq");
fdt_register_compatibility(null, "compatible:ulpi-phy");
fdt_register_compatibility(null, "compatible:xlnx,zynq-efuse");
fdt_register_compatibility(null, "compatible:qemu:memory-region-spec");
fdt_register_compatibility(null, "compatible:shared-dma-pool");

fdt_register_instance(i2c_bus_fdt_init, "i2c@0");
fdt_register_instance(i2c_bus_fdt_init, "i2c@1");
fdt_register_instance(i2c_bus_fdt_init, "i2c@2");
fdt_register_instance(i2c_bus_fdt_init, "i2c@3");
fdt_register_instance(i2c_bus_fdt_init, "i2c@4");
fdt_register_instance(i2c_bus_fdt_init, "i2c@5");
fdt_register_instance(i2c_bus_fdt_init, "i2c@6");
fdt_register_instance(i2c_bus_fdt_init, "i2c@7");

static const TypeInfo fdt_qom_aliases [] = {
    {   .name = "qemu:memory-region",       .parent = "memory-region"  },
    {   .name = "simple-bus",               .parent = "memory-region"  },
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
