#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"
#include "sysemu/blockdev.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/log.h"
#include "sysemu/char.h"
#include "block/coroutine.h"

#include "hw/char/serial.h"
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
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_NP(lvl, ...) do { \
    if (FDT_GENERIC_UTIL_ERR_DEBUG > (lvl)) { \
        fprintf(stderr,  "%s", node_path); \
        DB_PRINT((lvl), ## __VA_ARGS__); \
    } \
} while (0);

int fdt_generic_num_cpus;

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

    DB_PRINT_NP(0, "FLASH: baseaddr: 0x%x, size: 0x%x\n",
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

    irqline = *fdt_get_irq_info(fdti, node_path, 0, irq_info);
    DB_PRINT_NP(0, "UART16550a: baseaddr: 0x" TARGET_FMT_plx
                ", irq: %s, baud %d\n", base, irq_info, baudrate);

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
        DB_PRINT_NP(0, "parenting i2c bus to %s bus %s\n", parent_node_path,
                 node_name);
        fdt_init_set_opaque(fdti, node_path,
                            qdev_get_child_bus(dev, node_name));
    } else {
        DB_PRINT_NP(0, "orphaning i2c bus\n");
    }
    return 0;
}

static int memory_fdt_init(char *node_path, FDTMachineInfo *fdti, void *priv)
{
    MemoryRegion *address_space_mem = get_system_memory();
    FDTMemoryInfo *meminfo;
    unsigned int ram_num_regions = 0;
    Error *errp = NULL;
    char *ro_str;
    int readonly;

    ro_str = qemu_devtree_getprop(fdti->fdt, node_path,
                                  "read-only", NULL, false, &errp);
    /* Found the read-only property?  */
    readonly = errp == NULL;
    g_free(ro_str);

    meminfo = g_new(FDTMemoryInfo, 1);
    errp = NULL;
    while (1) {
        char ram_region_name[50];
        uint64_t size = 0;
        MemoryRegion *mr;
        hwaddr base;

        base = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg",
                                         (ram_num_regions * 2) + 0, false,
                                         &errp);
        size = qemu_devtree_getprop_cell(fdti->fdt, node_path, "reg",
                                         (ram_num_regions * 2) + 1, false,
                                         &errp);
        /* If an error is set, then no more cells exist in the property and thus
         * no more memory regions are defined.
         */
        if (error_is_set(&errp)) {
            break;
        }
        ram_num_regions++;
        meminfo->last_base = base;
        meminfo->last_size = size;

        /* XXX: node_path might not be the friendliest name for QEMU.  */
        snprintf(ram_region_name, 50, "%s.%d", node_path, ram_num_regions);

        mr = g_new(MemoryRegion, 1);
        DB_PRINT_NP(0, "memory created at %#llx size %#lx ro=%d\n",
                    (unsigned long long)base, (unsigned long)size, readonly);

        /* Create the RAM/ROM.  */
        memory_region_init_ram(mr, node_path, size);
        memory_region_set_readonly(mr, readonly);

        vmstate_register_ram_global(mr);
        memory_region_add_subregion(address_space_mem, base, mr);
    }

    meminfo->nr_regions = ram_num_regions;
    fdt_init_set_opaque(fdti, node_path, meminfo);
    return 0;
}

static inline void razwi_unimp_rw(void *opaque, hwaddr addr, uint64_t val64,
                           unsigned int size, bool rnw) {
    char str[1024];

    snprintf(str, sizeof(str), "%s: RAZ/WI device %s: addr: %#llx data: %#llx"
             " size: %d\n",
             opaque ? (const char *)opaque : "(none)", rnw ? "read" : "write",
             (unsigned long long)addr, (unsigned long long)val64, size);

    DB_PRINT(0, "%s", str);
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

fdt_register_compatibility_n(memory_fdt_init, "device_type:memory", 0);

fdt_register_compatibility_n(uart16550_fdt_init, "compatible:ns16550", 0);
fdt_register_compatibility_n(uart16550_fdt_init, "compatible:ns16550a", 1);

static const void *null;

fdt_register_compatibility_n(null, "compatible:simple-bus", 0);
fdt_register_compatibility_n(null, "compatible:marvell,88e1111", 1);
fdt_register_compatibility_n(null, "compatible:arm,pl310-cache", 2);
fdt_register_compatibility_n(null, "compatible:xlnx,ps7-cortexa9-1.00.a", 3);
fdt_register_compatibility_n(null, "compatible:xlnx,zynq_remoteproc", 4);
fdt_register_compatibility_n(null, "compatible:xlnx,ps7-smcc-1.00.a", 5);
fdt_register_compatibility_n(null, "compatible:xlnx,ps7-smc", 6)
fdt_register_compatibility_n(null, "compatible:xlnx,ps7-nand-1.00.a", 7);
fdt_register_compatibility_n(null, "compatible:xlnx,ps7-ram-1.00.a", 8);
fdt_register_compatibility_n(null, "compatible:xlnx,ps7-ocm", 9);

fdt_register_instance_n(i2c_bus_fdt_init, "i2c@0", 0);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@1", 1);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@2", 2);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@3", 3);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@4", 4);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@5", 5);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@6", 6);
fdt_register_instance_n(i2c_bus_fdt_init, "i2c@7", 7);

static const TypeInfo fdt_qom_aliases [] = {
    {   .name = "generic-ahci",             .parent = "sysbus-ahci"         },
    {   .name = "xlnx.ps7-ethernet",        .parent = "cadence_gem"         },
    {   .name = "xlnx.ps7-ttc",             .parent = "cadence_ttc"         },
    {   .name = "xlnx.ps7-uart",            .parent = "cadence_uart"        },
    {   .name = "arm.cortex-a9-twd-timer",  .parent = "arm_mptimer"         },
    {   .name = "xlnx.ps7-slcr",            .parent = "xilinx,zynq_slcr"    },
    {   .name = "xlnx.zynq-slcr",           .parent = "xilinx,zynq_slcr"    },
    {   .name = "arm.cortex-a9-gic",        .parent = "arm_gic"             },
    {   .name = "arm.gic",                  .parent = "arm_gic"             },
    {   .name = "arm_a9_scu",               .parent = "a9-scu"              },
    {   .name = "xlnx.ps7-usb",             .parent = "xlnx,ps7-usb"        },
};

static void fdt_generic_register_types(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(fdt_qom_aliases); ++i) {
        type_register_static(&fdt_qom_aliases[i]);
    }
}

type_init(fdt_generic_register_types)
