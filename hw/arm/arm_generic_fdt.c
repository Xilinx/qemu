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

#include "qemu/config-file.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/blockdev.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/block/flash.h"

#include <libfdt.h>
#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

#include "hw/arm/arm.h"

#define MACHINE_NAME "arm-generic-fdt"

#define MAX_CPUS 4

#define SMP_BOOT_ADDR 0xfffffff0
/* Meaningless, but keeps arm boot happy */
#define SMP_BOOTREG_ADDR 0xfffffffc

/* Entry point for secondary CPU */
static uint32_t zynq_smpboot[] = {
    0xe320f003, /* wfi */
    0xeafffffd, /* beq     <wfi> */
};

static void zynq_write_secondary_boot(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
    int n;

    for (n = 0; n < ARRAY_SIZE(zynq_smpboot); n++) {
        zynq_smpboot[n] = tswap32(zynq_smpboot[n]);
    }
    rom_add_blob_fixed("smpboot", zynq_smpboot, sizeof(zynq_smpboot),
                       SMP_BOOT_ADDR);
}

static void zynq_ps7_usb_nuke_phy(void *fdt)
{
    char usb_node_path[DT_PATH_LENGTH];

    int ret = qemu_devtree_node_by_compatible(fdt, usb_node_path,
                                              "xlnx,ps7-usb-1.00.a");
    if (!ret) {
        qemu_fdt_setprop_string(fdt, usb_node_path, "phy_type", "none");
        qemu_fdt_setprop_string(fdt, usb_node_path, "dr_mode", "host");
    }
}

static char *zynq_ps7_qspi_flash_node_clone(void *fdt)
{
    char qspi_node_path[DT_PATH_LENGTH];
    char qspi_new_node_path[DT_PATH_LENGTH];
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
        snprintf(qspi_new_node_path, DT_PATH_LENGTH, "%s/ps7-qspi-dummy@0",
                 qspi_node_path);

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

static struct arm_boot_info arm_generic_fdt_binfo = {};

static void arm_generic_fdt_init(MachineState *machine)
{
    ram_addr_t ram_kernel_base = 0, ram_kernel_size = 0;

    void *fdt = NULL;
    void *sw_fdt = NULL;
    int fdt_size, sw_fdt_size;
    const char *dtb_arg, *hw_dtb_arg;
    char node_path[DT_PATH_LENGTH];
    FDTMachineInfo *fdti;
    MemoryRegion *main_mem;
    char *qspi_clone_spi_flash_node_name = NULL;

    /*char *qspi_clone_spi_flash_node_name; */

    dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "dtb");
    hw_dtb_arg = qemu_opt_get(qemu_get_machine_opts(), "hw-dtb");
    if (!dtb_arg && !hw_dtb_arg) {
        goto no_dtb_arg;
    }

    /* Software dtb is always the -dtb arg */
    if (dtb_arg) {
        sw_fdt = load_device_tree(dtb_arg, &sw_fdt_size);
        if (!sw_fdt) {
            error_report("Error: Unable to load Device Tree %s\n", dtb_arg);
            exit(1);
        }
    }

    /* If the user provided a -hw-dtb, use it as the hw description.  */
    if (hw_dtb_arg) {
        fdt = load_device_tree(hw_dtb_arg, &fdt_size);
        if (!fdt) {
            hw_error("Error: Unable to load Device Tree %s\n", hw_dtb_arg);
            return;
        }
    } else if (sw_fdt) {
        fdt = sw_fdt;
        fdt_size = sw_fdt_size;
    }


    /* If booting PetaLinux ARM (Zynq Machine)*/
    if (!strcmp(MACHINE_GET_CLASS(machine)->name, "arm-generic-fdt-plnx")) {
        /* Added a dummy flash node, if is-dual property is set to 1*/
        qspi_clone_spi_flash_node_name = zynq_ps7_qspi_flash_node_clone(fdt);

        /* Ensure that an interrupt controller exists before disabling it */
        if (!qemu_devtree_get_node_by_name(fdt, node_path,
                                           "interrupt-controller")) {
            qemu_fdt_setprop_cells(fdt, node_path,
                                   "disable-linux-gic-init", true);
        }
    }

    /* find memory node or add new one if needed */
    while (qemu_devtree_get_node_by_name(fdt, node_path, "memory")) {
        qemu_fdt_add_subnode(fdt, "/memory@0");
        qemu_fdt_setprop_cells(fdt, "/memory@0", "reg", 0, machine->ram_size);
    }

    if (!qemu_fdt_getprop(fdt, "/memory", "compatible", NULL, 0, NULL)) {
        qemu_fdt_setprop_string(fdt, "/memory", "compatible",
                                "qemu:memory-region");
        qemu_fdt_setprop_cells(fdt, "/memory", "qemu,ram", 1);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);

    main_mem = MEMORY_REGION(object_resolve_path(node_path, NULL));
    ram_kernel_base = object_property_get_int(OBJECT(main_mem), "addr", NULL);
    ram_kernel_size = object_property_get_int(OBJECT(main_mem), "size", NULL);

    if (!memory_region_is_mapped(main_mem)) {
        /* If is not mapped, map it here */
        memory_region_add_subregion(get_system_memory(), 0, main_mem);
    }

    fdt_init_destroy_fdti(fdti);

    arm_generic_fdt_binfo.fdt = sw_fdt;
    arm_generic_fdt_binfo.fdt_size = sw_fdt_size;
    arm_generic_fdt_binfo.ram_size = ram_kernel_size;
    arm_generic_fdt_binfo.kernel_filename = machine->kernel_filename;
    arm_generic_fdt_binfo.kernel_cmdline = machine->kernel_cmdline;
    arm_generic_fdt_binfo.initrd_filename = machine->initrd_filename;
    arm_generic_fdt_binfo.nb_cpus = fdt_generic_num_cpus;
    arm_generic_fdt_binfo.write_secondary_boot = zynq_write_secondary_boot;
    arm_generic_fdt_binfo.smp_loader_start = SMP_BOOT_ADDR;
    arm_generic_fdt_binfo.smp_bootreg_addr = SMP_BOOTREG_ADDR;
    arm_generic_fdt_binfo.board_id = 0xd32;
    arm_generic_fdt_binfo.loader_start = ram_kernel_base;
    arm_generic_fdt_binfo.secure_boot = true;

    if (qspi_clone_spi_flash_node_name != NULL) {
        /* Remove cloned DTB node */
        int offset = fdt_path_offset(fdt, qspi_clone_spi_flash_node_name);
        fdt_del_node(fdt, offset);
        g_free(qspi_clone_spi_flash_node_name);
    }

    /*
     * FIXME: Probably better implemented as a plnx-specific pre-boot dtb
     * modifier
     */
    zynq_ps7_usb_nuke_phy(fdt);

    if (machine->kernel_filename) {
        arm_load_kernel(ARM_CPU(first_cpu), &arm_generic_fdt_binfo);
    }
    return;

no_dtb_arg:
    hw_error("DTB must be specified for %s machine model\n", MACHINE_NAME);
    return;
}

static void arm_generic_fdt_init_plnx(MachineState *machine)
{
    MemoryRegion *address_space_mem = get_system_memory();
    DeviceState *dev;
    SysBusDevice *busdev;

    /*FIXME: Describe OCM in DTB and delete this */
    /* ZYNQ OCM: */
    {
        MemoryRegion *ocm_ram = g_new(MemoryRegion, 1);
        memory_region_init_ram(ocm_ram, NULL, "zynq.ocm_ram", 256 << 10,
                               &error_abort);
        vmstate_register_ram_global(ocm_ram);
        memory_region_add_subregion(address_space_mem, 0xFFFC0000, ocm_ram);
    }

    /* FIXME: Descibe NAND in DTB and delete this */
    /* NAND: */
    dev = qdev_create(NULL, "arm.pl35x");
    /* FIXME: handle this somewhere central */
    object_property_add_child(container_get(qdev_get_machine(), "/unattached"),
                              "pl353", OBJECT(dev), NULL);
    qdev_prop_set_uint8(dev, "x", 3);
    {
        DriveInfo *dinfo = drive_get_next(IF_PFLASH);
        DeviceState *att_dev = nand_init(dinfo ? blk_by_legacy_dinfo(dinfo)
                                               : NULL,
                                         NAND_MFR_STMICRO, 0xaa);

        object_property_set_link(OBJECT(dev), OBJECT(att_dev), "dev1",
                                 &error_abort);
    }
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xe000e000);
    sysbus_mmio_map(busdev, 2, 0xe1000000);

    /* Mark the simple-bus as incompatible as it breaks the Zynq boot */
    add_to_compat_table(NULL, "compatible:simple-bus", NULL);

    arm_generic_fdt_init(machine);

    /* FIXME: Descibe SCU in DTB and delete this */
    /* ZYNQ SCU: */
    {
        DeviceState *dev = qdev_create(NULL, "a9-scu");
        SysBusDevice *busdev = SYS_BUS_DEVICE(dev);

        qdev_prop_set_uint32(dev, "num-cpu", fdt_generic_num_cpus);
        qdev_init_nofail(dev);
        sysbus_mmio_map(busdev, 0, 0xF8F00000);
    }

}

static QEMUMachine arm_generic_fdt_machine = {
    .name = MACHINE_NAME,
    .desc = "ARM device tree driven machine model",
    .init = arm_generic_fdt_init,
    .max_cpus = MAX_CPUS,
};

static QEMUMachine arm_generic_fdt_machine_plnx = {
    .name = MACHINE_NAME "-plnx",
    .desc = "ARM device tree driven machine model for PetaLinux Zynq",
    .init = arm_generic_fdt_init_plnx,
    .max_cpus = MAX_CPUS,
};

static void arm_generic_fdt_machine_init(void)
{
    qemu_register_machine(&arm_generic_fdt_machine);
    qemu_register_machine(&arm_generic_fdt_machine_plnx);
}

int endian = 0;

machine_init(arm_generic_fdt_machine_init);

fdt_register_compatibility_opaque(pflash_cfi01_fdt_init,
                                  "compatibile:cfi-flash", 0, &endian);
