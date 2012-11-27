#include "blockdev.h"

#include "fdt_generic_util.h"
#include "fdt_generic_devices.h"
#include "exec-memory.h"

#include "serial.h"
#include "flash.h"

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
#define DB_PRINT(...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

int pflash_cfi01_fdt_init(char *node_path, FDTMachineInfo *fdti, void *opaque)
{

    uint32_t flash_base = 0;
    uint32_t flash_size = 0;
    Error *errp = NULL;

    int be = *((int *)opaque);

    DriveInfo *dinfo;
    uint32_t bank_width;

    flash_base = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg", 0,
                                                false, &errp);
    flash_size = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg", 1,
                                                false, &errp);
    bank_width = qemu_devtree_getprop_cell(fdti->fdt, node_path, "bank-width",
                                                0, false, &errp);
    assert_no_error(errp);

    DB_PRINT("FDT: FLASH: baseaddr: 0x%x, size: 0x%x\n",
             flash_base, flash_size);

    dinfo = drive_get_next(IF_PFLASH);
    pflash_cfi01_register(flash_base, NULL, node_path, flash_size,
                            dinfo ? dinfo->bdrv : NULL, FLASH_SECTOR_SIZE,
                            flash_size/FLASH_SECTOR_SIZE,
                            bank_width, 0x89, 0x18, 0x0000, 0x0, be);
    return 0;
}

static int uart16550_fdt_init(char *node_path, FDTMachineInfo *fdti,
    void *priv)
{
    /* FIXME: Pass in dynamically */
    MemoryRegion *address_space_mem = get_system_memory();
    hwaddr base;
    uint32_t baudrate;
    qemu_irq irqline;
    char irq_info[1024];
    void *serial = NULL;
    Error *errp = NULL;

    base = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg", 0,
                                        false, &errp);
    base += qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg-offset", 0,
                                        false, &errp);
    assert_no_error(errp);
    base &= ~3ULL; /* qemu uart16550 model starts with 3* 8bit offset */

    baudrate = qemu_devtree_getprop_cell(fdti->fdt, node_path, "current-speed",
                                            0, false, &errp);
    if (errp) {
        baudrate = 115200;
    }

    irqline = fdt_get_irq_info(fdti, node_path, 0 , NULL, irq_info);
    DB_PRINT("FDT: UART16550a: baseaddr: 0x"
             TARGET_FMT_plx ", irq: %s, baud %d\n", base, irq_info, baudrate);

    /* it_shift = 2, reg-shift in DTS - for Xilnx IP is hardcoded */
    serial = serial_mm_init(address_space_mem, base, 2, irqline, baudrate,
                            qemu_char_get_next_serial(), DEVICE_LITTLE_ENDIAN);
    return 0;
}

fdt_register_compatibility_n(uart16550_fdt_init, "ns16550", 0);
fdt_register_compatibility_n(uart16550_fdt_init, "ns16550a", 1);
