#include "qemu/osdep.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
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

#include "hw/block/flash.h"
#include "hw/qdev-core.h"

#define FLASH_SECTOR_SIZE (64 * 1024)

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

int pflash_cfi01_fdt_init(char *node_path, FDTMachineInfo *fdti, void *opaque)
{

    uint32_t flash_base = 0;
    uint32_t flash_size = 0;

    DriveInfo *dinfo;
    uint32_t bank_width;

    /* FIXME: respect #address and size cells */
    flash_base = qemu_fdt_getprop_cell(fdti->fdt, node_path, "reg", 0,
                                       false, &error_abort);
    flash_size = qemu_fdt_getprop_cell(fdti->fdt, node_path, "reg", 1,
                                       false, &error_abort);
    bank_width = qemu_fdt_getprop_cell(fdti->fdt, node_path, "bank-width",
                                       0, false, &error_abort);

    DB_PRINT_NP(0, "FLASH: baseaddr: 0x%x, size: 0x%x\n",
                flash_base, flash_size);

    dinfo = drive_get_next(IF_PFLASH);
    pflash_cfi01_register(flash_base, node_path, flash_size,
                            dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                            FLASH_SECTOR_SIZE,
                            bank_width, 0x89, 0x18, 0x0000, 0x0, 0);
    return 0;
}
