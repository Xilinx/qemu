#include "blockdev.h"

#include "fdt_generic_util.h"
#include "fdt_generic_devices.h"
#include "exec-memory.h"
#include "qemu-log.h"

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
    serial_mm_init(address_space_mem, base, 2, irqline, baudrate,
                   qemu_char_get_next_serial(), DEVICE_LITTLE_ENDIAN);
    return 0;
}

static int i2c_bus_fdt_init(char *node_path, FDTMachineInfo *fdti, void *priv)
{
    Object *parent;
    DeviceState *dev;
    char parent_node_path[DT_PATH_LENGTH];
    char *node_name = qemu_devtree_get_node_name(fdti->fdt, node_path);

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
        DB_PRINT("%s: parenting i2c bus to %s bus %s\n", node_path,
                 parent_node_path, node_name);
        fdt_init_set_opaque(fdti, node_path,
                            qdev_get_child_bus(dev, node_name));
    } else {
        DB_PRINT("%s: orphaning i2c bus\n", node_path);
    }
    return 0;
}

static inline void razwi_unimp_rw(void *opaque, hwaddr addr, uint64_t val64,
                           unsigned int size, bool rnw) {
    char str[1024];

    snprintf(str, sizeof(str), "%s: RAWZI device %s: addr: %#llx data: %#llx"
             "size: %d\n",
             opaque ? (const char *)opaque : "(none)", rnw ? "read" : "write",
             (unsigned long long)addr, (unsigned long long)val64, size);

    DB_PRINT("%s", str);
    qemu_log_mask(LOG_UNIMP, "%s", str);
}

static void razwi_unimp_write(void *opaque, hwaddr addr, uint64_t val64,
                              unsigned int size) {
    razwi_unimp_rw(opaque, addr, val64, size, false);
}

static uint64_t razwi_unimp_read(void *opaque, hwaddr addr, unsigned int size)
{
    razwi_unimp_rw(opaque, addr, 0ull, size, true);
    return 0ull;
}

const MemoryRegionOps razwi_unimp_ops = {
    .read = razwi_unimp_read,
    .write = razwi_unimp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

fdt_register_compatibility_n(uart16550_fdt_init, "ns16550", 0);
fdt_register_compatibility_n(uart16550_fdt_init, "ns16550a", 1);

static const void *null;

fdt_register_compatibility_n(null, "simple-bus", 0);
fdt_register_compatibility_n(null, "marvell,88e1111", 1);
fdt_register_compatibility_n(null, "arm,pl310-cache", 2);
fdt_register_compatibility_n(null, "xlnx,ps7-cortexa9-1.00.a", 3);
fdt_register_compatibility_n(null, "xlnx,zynq_remoteproc", 4);
fdt_register_compatibility_n(null, "xlnx,ps7-smcc-1.00.a", 5);
fdt_register_compatibility_n(null, "xlnx,ps7-smc", 6)
fdt_register_compatibility_n(null, "xlnx,ps7-nand-1.00.a", 7);
fdt_register_compatibility_n(null, "xlnx,ps7-ram-1.00.a", 8);

fdt_register_instance_n(i2c_bus_fdt_init, "i2c@0", 0);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@1", 1);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@2", 2);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@3", 3);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@4", 4);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@5", 5);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@6", 6);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@7", 7);

static const TypeInfo fdt_qom_aliases [] = {
    {   .name = "generic-ahci", .parent = "sysbus-ahci" }
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
