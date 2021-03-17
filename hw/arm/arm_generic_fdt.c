/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2012 Xilinx. Inc
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.crosthwaite@xilinx.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "qapi/error.h"
#include "hw/block/flash.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "hw/arm/xlnx-zynqmp.h"

#include <libfdt.h>
#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

#ifndef ARM_GENERIC_FDT_DEBUG
#define ARM_GENERIC_FDT_DEBUG 3
#endif
#define DB_PRINT(lvl, ...) do { \
    if (ARM_GENERIC_FDT_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ": %s: ", __func__); \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0);

#define DB_PRINT_RAW(lvl, ...) do { \
    if (ARM_GENERIC_FDT_DEBUG > (lvl)) { \
        qemu_log_mask(LOG_FDT, ## __VA_ARGS__); \
    } \
} while (0);

#define GENERAL_MACHINE_NAME "arm-generic-fdt"
#define ZYNQ7000_MACHINE_NAME "arm-generic-fdt-7series"
#define DEP_GENERAL_MACHINE_NAME "arm-generic-fdt-plnx"

#define QTEST_RUNNING (qtest_enabled() && qtest_driver())

#define ZYNQ7000_MPCORE_PERIPHBASE 0xF8F00000

#define SMP_BOOT_ADDR 0xfffffff0
/* Meaningless, but keeps arm boot happy */
#define SMP_BOOTREG_ADDR 0xfffffffc

static struct arm_boot_info arm_generic_fdt_binfo = {};

/* Entry point for secondary CPU */
static uint32_t zynq_smpboot[] = {
    0xe320f003, /* wfi */
    0xeafffffd, /* beq     <wfi> */
};

typedef struct {
    ram_addr_t ram_kernel_base;
    ram_addr_t ram_kernel_size;
} memory_info;

static void arm_write_secondary_boot(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
    int n;

    for (n = 0; n < ARRAY_SIZE(zynq_smpboot); n++) {
        zynq_smpboot[n] = tswap32(zynq_smpboot[n]);
    }
    rom_add_blob_fixed("smpboot", zynq_smpboot, sizeof(zynq_smpboot),
                       SMP_BOOT_ADDR);
}

static void zynq7000_usb_nuke_phy(void *fdt)
{
    char usb_node_path[DT_PATH_LENGTH];

    int ret = qemu_devtree_node_by_compatible(fdt, usb_node_path,
                                              "xlnx,ps7-usb-1.00.a");
    if (!ret) {
        qemu_fdt_setprop_string(fdt, usb_node_path, "dr_mode", "host");
    }
}

static int zynq7000_mdio_phy_connect(char *node_path, FDTMachineInfo *fdti,
                                     void *Opaque)
{
    Object *parent;
    char parent_node_path[DT_PATH_LENGTH];

    /* Register MDIO obj instance to fdti, useful during child registration */
    fdt_init_set_opaque(fdti, node_path, Opaque);
    if (qemu_devtree_getparent(fdti->fdt, parent_node_path, node_path)) {
        abort();
    }

    /* Wait for the parent to be created */
    while (!fdt_init_has_opaque(fdti, parent_node_path)) {
        fdt_init_yield(fdti);
    }

    /* Get the parent obj (i.e gem object), which was registerd in fdti */
    parent = fdt_init_get_opaque(fdti, parent_node_path);

    /* Add parent to mdio node */
    if (!OBJECT(Opaque)->parent) {
        object_property_add_child(OBJECT(parent), "mdio_child", OBJECT(Opaque));
    }

    /* Set mdio property of gem device */
    object_property_set_link(OBJECT(parent), "mdio", OBJECT(Opaque), NULL);
    return 0;
}

static int zynq7000_mdio_phy_create(char *node_path, FDTMachineInfo *fdti,
                                     void *Opaque)
{
    bool has_mdio = false;
    char parent_node_path[DT_PATH_LENGTH];
    DeviceState *dev;
    uint32_t reg;

    if (qemu_devtree_getparent(fdti->fdt, parent_node_path, node_path)) {
        abort();
    }

    if (!strcmp(qemu_devtree_get_node_name(fdti->fdt, parent_node_path),
                "mdio")) {
        /* To not break the backward compatiblity lets also consider mdio node
         * dts can as be as below, with mdio node, when which we do not connect
         * mdio to ethernet instance as mdio node has other instance handler
         *
         * mdio {
         *    ethernet-phy@7 {
         *        reg = <0x7>;
         *        device_type = "ethernet-phy";
         *    };
         * };
         */
        has_mdio = true;
    }

    if (!has_mdio) {
        zynq7000_mdio_phy_connect(node_path, fdti, Opaque);
    }

    /* Wait for the parent to be created */
    while (!fdt_init_has_opaque(fdti, parent_node_path)) {
        fdt_init_yield(fdti);
    }

    dev = DEVICE(object_new("88e1116r"));
    qdev_set_parent_bus(dev, qdev_get_child_bus(Opaque, "mdio-bus"));
    reg = qemu_fdt_getprop_cell(fdti->fdt, node_path, "reg", 0, false,
                                NULL);
    object_property_set_int(OBJECT(dev), "reg", reg, NULL);
    return 0;
}

#define ZYNQ7000_QSPI_DUMMY_NAME "/ps7-qspi-dummy@0"

static char *zynq7000_qspi_flash_node_clone(void *fdt)
{
    char qspi_node_path[DT_PATH_LENGTH];
    char qspi_new_node_path[DT_PATH_LENGTH + strlen(ZYNQ7000_QSPI_DUMMY_NAME)];
    char *qspi_clone_name = NULL;
    uint32_t val[2];

    /* clear node paths */
    memset(qspi_node_path, 0, sizeof(qspi_node_path));
    memset(qspi_new_node_path, 0, sizeof(qspi_new_node_path));

    /* search for ps7 qspi node */
    int ret = qemu_devtree_node_by_compatible(fdt, qspi_node_path,
                                              "xlnx,zynq-qspi-1.0");
    if (ret == 0) {
        int qspi_is_dual = qemu_fdt_getprop_cell(fdt, qspi_node_path,
                                                 "is-dual", 0, false, NULL);
        /* Set bus-cells property to 1 */
        val[0] = cpu_to_be32(1);
        val[1] = 0;
        fdt_setprop(fdt, fdt_path_offset(fdt, qspi_node_path),
                    "#bus-cells", val, 4);

        /* Generate dummy name */
        snprintf(qspi_new_node_path, sizeof(qspi_new_node_path),
                 "%s" ZYNQ7000_QSPI_DUMMY_NAME, qspi_node_path);
        if (strlen(qspi_new_node_path) > DT_PATH_LENGTH) {
            error_report("%s, %zd chars too long DT path!", qspi_new_node_path,
                         strlen(qspi_new_node_path));
        }

        /* get the spi flash node to clone from (assume first child node) */
        int child_num = qemu_devtree_get_num_children(fdt, qspi_node_path, 1);
        char **child_flash = qemu_devtree_get_children(fdt, qspi_node_path, 1);
        if (child_num > 0) {
            char *compat_str = NULL;
            compat_str = qemu_fdt_getprop(fdt, child_flash[0],
                                          "compatible", NULL, false, NULL);
        /* Attach Default flash node to bus 1 */
        val[0] = 0;
        val[1] = 0;
        fdt_setprop(fdt, fdt_path_offset(fdt, child_flash[0]), "reg", val, 8);

            /* Create the cloned node if the qspi controller is in dual spi mode
             * and the compatible string is avaliable */
            if (compat_str != NULL) {
                if (qspi_is_dual == 1) {
                    /* Clone first node, preserving only 'compatible' value */
                    qemu_fdt_add_subnode(fdt, qspi_new_node_path);
                    qemu_fdt_setprop_string(fdt, qspi_new_node_path,
                                             "compatible", compat_str);
                    qspi_clone_name = g_strdup(qspi_new_node_path);

                    /* Attach Dummy flash node to bus 0 */
                    val[0] = 0;
                    val[1] = cpu_to_be32(1);
                    fdt_setprop(fdt, fdt_path_offset(fdt, qspi_new_node_path),
                                "reg", val, 8);
                }
                g_free(compat_str);
            }
        }
        g_free(child_flash);
    }

    return qspi_clone_name;
}

static memory_info init_memory(void *fdt, ram_addr_t ram_size, bool zynq_7000)
{
    FDTMachineInfo *fdti;
    char node_path[DT_PATH_LENGTH];
    MemoryRegion *mem_area;
    memory_info kernel_info;
    ram_addr_t start_addr;
    int mem_offset = 0;

    if (zynq_7000) {
        if (!qemu_devtree_node_by_compatible(fdt, node_path,
                                             "arm,cortex-a9-gic")) {
            qemu_fdt_setprop_cell(fdt, node_path, "num-priority-bits", 5);
        } else {
            warn_report("%s(): Unable to find GIC", __func__);
        }
    }

    /* Find a memory node or add new one if needed */
    while (qemu_devtree_get_node_by_name(fdt, node_path, "memory")) {
        qemu_fdt_add_subnode(fdt, "/memory@0");
        qemu_fdt_setprop_cells(fdt, "/memory@0", "reg", 0, ram_size);
    }

    if (!qemu_fdt_getprop(fdt, "/memory", "compatible", NULL, 0, NULL)) {
        qemu_fdt_setprop_string(fdt, "/memory", "compatible",
                                "qemu:memory-region");
        qemu_fdt_setprop_cells(fdt, "/memory", "qemu,ram", 1);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);

    mem_area = MEMORY_REGION(object_resolve_path(node_path, NULL));
    kernel_info.ram_kernel_base = object_property_get_int(OBJECT(mem_area),
                                                          "addr", NULL);
    kernel_info.ram_kernel_size = object_property_get_int(OBJECT(mem_area),
                                                          "size", NULL);

    if (kernel_info.ram_kernel_size == -1) {
        kernel_info.ram_kernel_size = ram_size;
    }

    if (zynq_7000) {
        do {
            mem_offset = fdt_node_offset_by_compatible(fdt, mem_offset,
                                                       "qemu:memory-region");
            if (mem_offset > 0) {
                fdt_get_path(fdt, mem_offset, node_path, DT_PATH_LENGTH);
                mem_area = MEMORY_REGION(object_resolve_path(node_path, NULL));

                if (!memory_region_is_mapped(mem_area)) {
                    start_addr =  object_property_get_int(OBJECT(mem_area),
                                                          "addr", NULL);
                    memory_region_add_subregion(get_system_memory(),
                                                start_addr, mem_area);
                }
            }
        } while (mem_offset > 0);
    } else {
        /* Let's find out how much memory we have already created, then
         * based on what the user ser with '-m' let's add more if needed.
         */
        uint64_t reg_value, mem_created = 0;
        int mem_container;
        char mem_node_path[DT_PATH_LENGTH];
        int size_cells;

        do {
            mem_offset =
                fdt_node_offset_by_compatible(fdt, mem_offset,
                                              "qemu:memory-region");

            /* Check if we found anything and that it is top level memory */
            if (mem_offset > 0 &&
                    fdt_node_depth(fdt, mem_offset) == 1) {
                fdt_get_path(fdt, mem_offset, mem_node_path,
                             DT_PATH_LENGTH);

                mem_container = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                      "container",
                                                      0, 0, NULL);

                /* We only want RAM, so we filter to make sure the container of
                 * what we are looking at is the same as the main memory@0 node
                 * we just found above.
                 */
                if (mem_container != qemu_fdt_get_phandle(fdt, node_path)) {
                    continue;
                }

                DB_PRINT(0, "Found top level memory region %s\n",
                         mem_node_path);
                size_cells = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                "#size-cells", 0, true, NULL);

                reg_value = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                  "reg", 0, 0, NULL);
                reg_value = reg_value << 32;
                reg_value += qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                   "reg", 1, 0, NULL);

                DB_PRINT(1, "    Address: 0x%" PRIx64 " ", reg_value);

                reg_value += qemu_fdt_getprop_sized_cell(fdt, mem_node_path,
                                                   "reg", 2, size_cells, NULL);

                DB_PRINT_RAW(1, "Size: 0x%" PRIx64 "\n", reg_value);

                /* Find the largest address (start address + size) */
                if (mem_created < reg_value) {
                    mem_created = reg_value;
                }
            }
        } while (mem_offset > 0);

        DB_PRINT(0, "Highest memory address from DTS is: " \
                 "0x%" PRIx64 "/0x" RAM_ADDR_FMT "\n", mem_created, ram_size);

        /* We now have the maximum amount of DDR that has been created. */
        if (mem_created == 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "No memory was specified in the " \
                          "device tree, are there no memory nodes on the " \
                          "root level?\n");
        } else if (mem_created < ram_size) {
            /* We need to create more memory to match what the user asked for.
             * We do this based on the qemu:memory-region-spec values.
             */
            do {
                mem_offset =
                    fdt_node_offset_by_compatible(fdt, mem_offset,
                                                  "qemu:memory-region-spec");

                if (mem_offset > 0) {
                    MemoryRegion *container;
                    MemoryRegion *ram_region = g_new(MemoryRegion, 1);
                    const char *region_name;
                    uint64_t region_start, region_size;
                    int container_offset, container_phandle, ram_prop;

                    fdt_get_path(fdt, mem_offset, mem_node_path,
                                 DT_PATH_LENGTH);

                    DB_PRINT(0, "Connecting %s\n", mem_node_path);

                    region_name = fdt_get_name(fdt, mem_offset, NULL);

                    /* This assumes two address cells and two size cells */
                    region_start = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                       "reg", 0, 0, NULL);
                    region_start = region_start << 32;
                    region_start += qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                       "reg", 1, 0, NULL);

                    DB_PRINT(1, "    Address: 0x%" PRIx64 " ", region_start);

                    region_size = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                         "reg", 2, 0, NULL);
                    region_size = region_size << 32;
                    region_size += qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                         "reg", 3, 0, NULL);

                    region_size = MIN(region_size, ram_size - mem_created);
                    mem_created += region_size;

                    DB_PRINT_RAW(1, "Size: 0x%" PRIx64 "\n", region_size);

                    container_phandle = qemu_fdt_getprop_cell(fdt,
                                                              mem_node_path,
                                                              "container", 0,
                                                              0, NULL);
                    container_offset =
                            fdt_node_offset_by_phandle(fdt, container_phandle);
                    fdt_get_path(fdt, container_offset,
                                 node_path, DT_PATH_LENGTH);
                    container = MEMORY_REGION(
                                        object_resolve_path(node_path, NULL));

                    ram_prop = qemu_fdt_getprop_cell(fdt, mem_node_path,
                                                     "qemu,ram", 0,
                                                     0, NULL);
                    memory_region_init(ram_region, NULL, region_name,
                                           region_size);
                    object_property_set_int(OBJECT(ram_region), "ram", ram_prop, &error_abort);
                    memory_region_add_subregion(container, region_start,
                                                ram_region);
                }
            } while (mem_offset > 0 && ram_size > mem_created);
        } else {
            /* The device tree generated more or equal amount of memory then
             * the user specified. Set that internally in QEMU.
             */
            DB_PRINT(0, "No extra memory is required\n");

            ram_size = mem_created;
            qemu_opt_set_number(qemu_find_opts_singleton("memory"), "size",
                                mem_created, &error_fatal);
        }
    }

    fdt_init_destroy_fdti(fdti);

    return kernel_info;
}

static void arm_generic_fdt_init(MachineState *machine)
{
    void *fdt = NULL, *sw_fdt = NULL;
    int fdt_size, sw_fdt_size;
    const char *dtb_arg, *hw_dtb_arg;
    char node_path[DT_PATH_LENGTH];
    char *qspi_clone_spi_flash_node_name = NULL;
    memory_info kernel_info;
    bool zynq_7000 = false;
    int is_linux;

    is_linux = object_property_get_bool(OBJECT(qdev_get_machine()),
                                        "linux", NULL);

    /* If booting a Zynq-7000 Machine*/
    if (!strcmp(MACHINE_GET_CLASS(machine)->name, ZYNQ7000_MACHINE_NAME)) {
        zynq_7000 = true;
    } else if (!strcmp(MACHINE_GET_CLASS(machine)->name,
                       DEP_GENERAL_MACHINE_NAME)) {
        if (!QTEST_RUNNING) {
            /* Don't print this error if running qtest */
            fprintf(stderr, "The '" DEP_GENERAL_MACHINE_NAME "' machine has " \
                    "been deprecated and will be removed after the 2017.4 " \
                    "release.Please use '" ZYNQ7000_MACHINE_NAME \
                    "' instead.\n");
        }
        zynq_7000 = true;
    }

    dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "dtb");
    hw_dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "hw-dtb");
    if (!dtb_arg && !hw_dtb_arg) {
        if (!QTEST_RUNNING) {
            /* Just return without error if running qtest, as we never have a
             * device tree
             */
            hw_error("DTB must be specified for %s machine model\n",
                     MACHINE_GET_CLASS(machine)->name);
        }
        return;
    }

    /* Software dtb is always the -dtb arg */
    if (dtb_arg) {
        sw_fdt = load_device_tree(dtb_arg, &sw_fdt_size);
        if (!sw_fdt) {
            error_report("Error: Unable to load Device Tree %s", dtb_arg);
            exit(1);
        }
    }

    /* If the user provided a -hw-dtb, use it as the hw description.  */
    if (hw_dtb_arg) {
        fdt = load_device_tree(hw_dtb_arg, &fdt_size);
        if (!fdt) {
            error_report("Error: Unable to load Device Tree %s", hw_dtb_arg);
            exit(1);
        }
    } else if (sw_fdt) {
        fdt = sw_fdt;
        fdt_size = sw_fdt_size;
    }

    if (zynq_7000) {
        int node_offset = 0;

        /* Added a dummy flash node, if is-dual property is set to 1*/
        qspi_clone_spi_flash_node_name = zynq7000_qspi_flash_node_clone(fdt);

        /* Ensure that an interrupt controller exists before disabling it */
        if (!qemu_devtree_get_node_by_name(fdt, node_path,
                                           "interrupt-controller")) {
            qemu_fdt_setprop_cells(fdt, node_path,
                                   "disable-linux-gic-init", true);
        }

        /* The Zynq-7000 device tree doesn't contain information about the
         * Configuation Base Address Register (reset-cbar) but we need to set
         * it in order for Linux to find the SCU. So add it into the device
         * tree for every A9 CPU.
         */
        do {
            node_offset = fdt_node_offset_by_compatible(fdt, node_offset,
                                                        "arm,cortex-a9");
            if (node_offset > 0) {
                fdt_get_path(fdt, node_offset, node_path, DT_PATH_LENGTH);
                qemu_fdt_setprop_cells(fdt, node_path, "reset-cbar",
                                       ZYNQ7000_MPCORE_PERIPHBASE);
            }
        } while (node_offset > 0);
    }

    kernel_info = init_memory(fdt, machine->ram_size, zynq_7000);

    arm_generic_fdt_binfo.fdt = sw_fdt;
    arm_generic_fdt_binfo.fdt_size = sw_fdt_size;
    arm_generic_fdt_binfo.ram_size = kernel_info.ram_kernel_size;
    arm_generic_fdt_binfo.kernel_filename = machine->kernel_filename;
    arm_generic_fdt_binfo.kernel_cmdline = machine->kernel_cmdline;
    arm_generic_fdt_binfo.initrd_filename = machine->initrd_filename;
    arm_generic_fdt_binfo.nb_cpus = fdt_generic_num_cpus;
    arm_generic_fdt_binfo.write_secondary_boot = arm_write_secondary_boot;
    arm_generic_fdt_binfo.smp_loader_start = SMP_BOOT_ADDR;
    arm_generic_fdt_binfo.smp_bootreg_addr = SMP_BOOTREG_ADDR;
    arm_generic_fdt_binfo.board_id = 0xd32;
    arm_generic_fdt_binfo.loader_start = kernel_info.ram_kernel_base;
    arm_generic_fdt_binfo.secure_boot = is_linux && !zynq_7000 ? false : true;

    if (qspi_clone_spi_flash_node_name != NULL) {
        /* Remove cloned DTB node */
        int offset = fdt_path_offset(fdt, qspi_clone_spi_flash_node_name);
        fdt_del_node(fdt, offset);
        g_free(qspi_clone_spi_flash_node_name);
    }

    if (zynq_7000) {
        zynq7000_usb_nuke_phy(fdt);
    }

    if (machine->kernel_filename) {
        arm_load_kernel(ARM_CPU(first_cpu), machine, &arm_generic_fdt_binfo);
    }

    return;
}

static void arm_generic_fdt_7000_init(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    SysBusDevice *busdev;
    MemoryRegion *ocm_ram;
    DriveInfo *dinfo;
    DeviceState *att_dev;

    ocm_ram = g_new(MemoryRegion, 1);
    memory_region_init_ram(ocm_ram, NULL, "zynq.ocm_ram", 256 << 10,
                           &error_abort);
    memory_region_add_subregion(address_space_mem, 0xFFFC0000, ocm_ram);

    dev = qdev_new("arm.pl35x");
    object_property_add_child(container_get(qdev_get_machine(), "/unattached"),
                              "pl353", OBJECT(dev));
    qdev_prop_set_uint8(dev, "x", 3);
    dinfo = drive_get_next(IF_PFLASH);
    att_dev = nand_init(dinfo ? blk_by_legacy_dinfo(dinfo)
                              : NULL,
                        NAND_MFR_STMICRO, 0xaa);
    object_property_set_link(OBJECT(dev), "dev1", OBJECT(att_dev), &error_abort);

    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0xe000e000);
    sysbus_mmio_map(busdev, 2, 0xe1000000);

    /* Mark the simple-bus as incompatible as it breaks the Zynq boot */
    add_to_compat_table(NULL, "compatible:simple-bus", NULL);

    dev = qdev_new("mdio");
    /* Add MDIO Connect Call back */
    add_to_inst_bind_table(zynq7000_mdio_phy_connect, "mdio", dev);
    add_to_compat_table(zynq7000_mdio_phy_create, "device_type:ethernet-phy",
                        dev);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_fatal);

    arm_generic_fdt_init(machine);

    dev = qdev_new("a9-scu");
    busdev = SYS_BUS_DEVICE(dev);
    qdev_prop_set_uint32(dev, "num-cpu", fdt_generic_num_cpus);
    sysbus_realize(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, ZYNQ7000_MPCORE_PERIPHBASE);
}

static void arm_generic_fdt_machine_init(MachineClass *mc)
{
    mc->desc = "ARM device tree driven machine model";
    mc->init = arm_generic_fdt_init;
    mc->ignore_memory_transaction_failures = true;
    /* X A53s and 2 R5s */
    mc->max_cpus = 64;
    mc->default_cpus = 64;
}

static void arm_generic_fdt_7000_machine_init(MachineClass *mc)
{
    mc->desc = "ARM device tree driven machine model for the Zynq-7000";
    mc->init = arm_generic_fdt_7000_init;
    mc->ignore_memory_transaction_failures = true;
    mc->max_cpus = 2;
    mc->default_cpus = 2;
}

/* Deprecated, remove this */
static void arm_generic_fdt_dep_machine_init(MachineClass *mc)
{
    mc->desc = "Deprecated ARM device tree driven machine for the Zynq-7000";
    mc->init = arm_generic_fdt_7000_init;
    mc->ignore_memory_transaction_failures = true;
    mc->max_cpus = 2;
    mc->default_cpus = 2;
}

fdt_register_compatibility_opaque(pflash_cfi01_fdt_init,
                                  "compatibile:cfi-flash", 0, NULL);

DEFINE_MACHINE(GENERAL_MACHINE_NAME, arm_generic_fdt_machine_init)
DEFINE_MACHINE(ZYNQ7000_MACHINE_NAME, arm_generic_fdt_7000_machine_init)
DEFINE_MACHINE(DEP_GENERAL_MACHINE_NAME, arm_generic_fdt_dep_machine_init)
