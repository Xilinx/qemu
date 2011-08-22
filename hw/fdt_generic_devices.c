
#include "blockdev.h"

#include "fdt_generic_util.h"
#include "fdt_generic_devices.h"

#define FLASH_SECTOR_SIZE (64 * 1024)

/* FIXME: This file should go away. When these devices are properly QOMified
 * then these FDT creations should happen automatically without need for these
 * explict shim functions
 */

int pflash_cfi01_fdt_init(char *node_path, FDTMachineInfo *fdti, void *opaque)
{

    int flash_base = 0;
    int flash_size = 0;
    Error *errp = NULL;

    int be = *((int *)opaque);

    DriveInfo *dinfo;
    int bank_width;

    flash_base = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg", 0,
                                                false, &errp);
    flash_size = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg", 1,
                                                false, &errp);
    bank_width = qemu_devtree_getprop_cell(fdti->fdt, node_path, "bank-width",
                                                0, false, &errp);
    assert_no_error(errp);

    printf("FDT: FLASH: baseaddr: 0x%x, size: 0x%x\n",
           flash_base, flash_size);

    dinfo = drive_get_next(IF_PFLASH);
    pflash_cfi01_register(flash_base, NULL, node_path, flash_size,
                            dinfo ? dinfo->bdrv : NULL, FLASH_SECTOR_SIZE,
                            flash_size/FLASH_SECTOR_SIZE,
                            bank_width, 0x89, 0x18, 0x0000, 0x0, be);
    return 0;
}

#endif /* CONFIG_FDT */
