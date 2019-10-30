/*
 * Model of Petalogix linux reference design for all boards
 *
 * Copyright (c) 2009 Edgar E. Iglesias.
 * Copyright (c) 2009 Michal Simek.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.croshtwaite@petalogix.com)
 * Copyright (c) 2012 Petalogix Pty Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/boards.h"
#include "sysemu/device_tree.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/qtest.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

#include "boot.h"

#include <libfdt.h>

#define IS_PETALINUX_MACHINE \
    (!strcmp(MACHINE_GET_CLASS(machine)->name, MACHINE_NAME "-plnx"))

#define QTEST_RUNNING (qtest_enabled() && qtest_driver())

#define LMB_BRAM_SIZE  (128 * 1024)

#define MACHINE_NAME "microblaze-fdt"

#ifdef TARGET_WORDS_BIGENDIAN
int endian = 1;
#else
int endian;
#endif

static void
microblaze_generic_fdt_init(MachineState *machine)
{
    ram_addr_t ram_kernel_base = 0, ram_kernel_size = 0;
    void *fdt = NULL;
    const char *dtb_arg, *hw_dtb_arg;
    const char *kernel_filename;
    QemuOpts *machine_opts;
    int fdt_size;

    /* for memory node */
    char node_path[DT_PATH_LENGTH];
    FDTMachineInfo *fdti;
    MemoryRegion *main_mem;

    /* For DMA node */
    char dma_path[DT_PATH_LENGTH] = { 0 };
    uint32_t memory_phandle;

    /* For Ethernet nodes */
    char **eth_paths;
    char *phy_path;
    char *mdio_path;
    uint32_t n_eth;
    uint32_t prop_val;

    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (!machine_opts) {
        goto no_dtb_arg;
    }
    dtb_arg = qemu_opt_get(machine_opts, "dtb");
    hw_dtb_arg = qemu_opt_get(machine_opts, "hw-dtb");
    if (!dtb_arg && !hw_dtb_arg) {
        goto no_dtb_arg;
    }

    /* If the user only provided a -dtb, use it as the hw description.  */
    if (!hw_dtb_arg) {
        hw_dtb_arg = dtb_arg;
    }

    fdt = load_device_tree(hw_dtb_arg, &fdt_size);
    if (!fdt) {
        hw_error("Error: Unable to load Device Tree %s\n", hw_dtb_arg);
        return;
    }

    if (IS_PETALINUX_MACHINE) {
        /* Mark the simple-bus as incompatible as it breaks the Microblaze
         * PetaLinux boot
         */
        add_to_compat_table(NULL, "compatible:simple-bus", NULL);
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

    if (IS_PETALINUX_MACHINE) {
        /* If using a *-plnx machine, the AXI DMA memory links are not included
         * in the DTB by default. To avoid seg faults, add the links in here if
         * they have not already been added by the user
         */
        qemu_devtree_get_node_by_name(fdt, dma_path, "dma");

        if (strcmp(dma_path, "") != 0) {
            memory_phandle = qemu_fdt_check_phandle(fdt, node_path);

            if (!memory_phandle) {
                memory_phandle = qemu_fdt_alloc_phandle(fdt);

                qemu_fdt_setprop_cells(fdt, "/memory", "linux,phandle",
                                       memory_phandle);
                qemu_fdt_setprop_cells(fdt, "/memory", "phandle",
                                       memory_phandle);
            }

            if (!qemu_fdt_getprop(fdt, dma_path, "sg", NULL, 0, NULL)) {
                qemu_fdt_setprop_phandle(fdt, dma_path, "sg", node_path);
            }

            if (!qemu_fdt_getprop(fdt, dma_path, "s2mm", NULL, 0, NULL)) {
                qemu_fdt_setprop_phandle(fdt, dma_path, "s2mm", node_path);
            }

            if (!qemu_fdt_getprop(fdt, dma_path, "mm2s", NULL, 0, NULL)) {
                qemu_fdt_setprop_phandle(fdt, dma_path, "mm2s", node_path);
            }
        }

        /* Copy phyaddr value from phy node reg property */
        n_eth = qemu_devtree_get_n_nodes_by_name(fdt, &eth_paths, "ethernet");

        while (n_eth--) {
            mdio_path = qemu_devtree_get_child_by_name(fdt, eth_paths[n_eth],
                                                       "mdio");
            if (mdio_path) {
                phy_path = qemu_devtree_get_child_by_name(fdt, mdio_path,
                                                          "phy");
                if (phy_path) {
                    prop_val = qemu_fdt_getprop_cell(fdt, phy_path, "reg", 0,
                                                     NULL, &error_abort);
                    qemu_fdt_setprop_cell(fdt, eth_paths[n_eth], "xlnx,phyaddr",
                                          prop_val);
                    g_free(phy_path);
                } else {
                    qemu_log_mask(LOG_GUEST_ERROR, "phy not found in %s",
                                  mdio_path);
                }
                g_free(mdio_path);
            }
            g_free(eth_paths[n_eth]);
        }
        g_free(eth_paths);
    }

    /* Instantiate peripherals from the FDT.  */
    fdti = fdt_generic_create_machine(fdt, NULL);
    main_mem = MEMORY_REGION(object_resolve_path(node_path, NULL));

    ram_kernel_base = object_property_get_int(OBJECT(main_mem), "addr", NULL);
    ram_kernel_size = object_property_get_int(OBJECT(main_mem), "size", NULL);

    if (!memory_region_is_mapped(main_mem)) {
        /* If the memory region is not mapped, map it here.
         * It has to be mapped somewhere, so guess that the base address
         * is where the kernel starts
         */
        memory_region_add_subregion(get_system_memory(), ram_kernel_base,
                                    main_mem);

        if (ram_kernel_base && IS_PETALINUX_MACHINE) {
            /* If the memory added is at an offset from zero QEMU will error
             * when an ISR/exception is triggered. Add a small amount of hack
             * RAM to handle this.
             */
            MemoryRegion *hack_ram = g_new(MemoryRegion, 1);
            memory_region_init_ram_nomigrate(hack_ram, NULL, "hack_ram",
                                             0x1000, &error_abort);
            vmstate_register_ram_global(hack_ram);
            memory_region_add_subregion(get_system_memory(), 0, hack_ram);
        }
    }

    fdt_init_destroy_fdti(fdti);

    kernel_filename = qemu_opt_get(machine_opts, "kernel");
    if (kernel_filename) {
        microblaze_load_kernel(MICROBLAZE_CPU(first_cpu), ram_kernel_base,
                               ram_kernel_size, machine->initrd_filename, NULL,
                               NULL, fdt, fdt_size);
    }

    return;
no_dtb_arg:
    if (!QTEST_RUNNING) {
        hw_error("DTB must be specified for %s machine model\n", MACHINE_NAME);
    }
    return;
}

static void microblaze_generic_fdt_machine_init(MachineClass *mc)
{
    mc->desc = "Microblaze device tree driven machine model";
    mc->init = microblaze_generic_fdt_init;
}

static void microblaze_generic_fdt_plnx_machine_init(MachineClass *mc)
{
    mc->desc = "Microblaze device tree driven machine model for PetaLinux";
    mc->init = microblaze_generic_fdt_init;
}

fdt_register_compatibility_opaque(pflash_cfi01_fdt_init, "compatible:cfi-flash",
                                  0, &endian);

DEFINE_MACHINE(MACHINE_NAME, microblaze_generic_fdt_machine_init)
DEFINE_MACHINE(MACHINE_NAME "-plnx", microblaze_generic_fdt_plnx_machine_init)
