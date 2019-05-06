#include "qemu/osdep.h"
#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"
#include "qom/object.h"
#include "sysemu/blockdev.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "qemu/coroutine.h"

#include "hw/qdev-core.h"

/* FIXME: This file should go away. When these devices are properly QOMified
 * then these FDT creations should happen automatically without need for these
 * explict shim functions
 */

/* Piggy back fdt_generic_util.c ERR_DEBUG symbol as these two are really the
 * same feature
 */

#ifndef FDT_GENERIC_UTIL_ERR_DEBUG
#define FDT_GENERIC_UTIL_ERR_DEBUG 0
#endif
#define DB_PRINT(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(lvl, ": %s: ", __func__); \
        qemu_log_mask(lvl, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        qemu_log_mask(lvl, "%s", node_path); \
        DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0);

static const TypeInfo fdt_qom_aliases[] = {
    {   .name = "xlnx.ps7-ethernet",        .parent = "cadence_gem"         },
    {   .name = "cdns,gem",                 .parent = "cadence_gem"         },
    {   .name = "cdns,zynq-gem",            .parent = "cadence_gem"         },
    {   .name = "cdns,zynqmp-gem",          .parent = "cadence_gem"         },
    {   .name = "xlnx.ps7-ttc",             .parent = "cadence_ttc"         },
    {   .name = "cdns.ttc",                 .parent = "cadence_ttc"         },
    {   .name = "cdns.uart",                .parent = "cadence_uart"        },
    {   .name = "xlnx.ps7-uart",            .parent = "cadence_uart"        },
    {   .name = "cdns.spi-r1p6",            .parent = "xlnx.ps7-spi"        },
    {   .name = "xlnx.xuartps",             .parent = "cadence_uart"        },
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
