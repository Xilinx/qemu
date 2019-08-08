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
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/device_tree.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/qtest.h"
#include "qemu/config-file.h"
#include "qemu/option.h"

#include "hw/fdt_generic_util.h"
#include "hw/fdt_generic_devices.h"

#include "boot.h"

#define VAL(name) qemu_fdt_getprop_cell(fdt_g, node_path, name, 0, false, NULL)

#define IS_PETALINUX_MACHINE \
    (!strcmp(MACHINE_GET_CLASS(machine)->name, MACHINE_NAME "-plnx"))

#define QTEST_RUNNING (qtest_enabled() && qtest_driver())

/* FIXME: delete */
static void *fdt_g;

static void
microblaze_generic_fdt_reset(MicroBlazeCPU *cpu)
{
    CPUMBState *env = &cpu->env;

    char node_path[DT_PATH_LENGTH];
    qemu_devtree_get_node_by_name(fdt_g, node_path, "cpu");
    int t;
    int use_exc = 0;

    env->pvr.regs[0] = 0;
    env->pvr.regs[2] = PVR2_D_OPB_MASK \
                        | PVR2_D_LMB_MASK \
                        | PVR2_I_OPB_MASK \
                        | PVR2_I_LMB_MASK \
                        | 0;

    /* Even if we don't have PVR's, we fill out everything
       because QEMU will internally follow what the pvr regs
       state about the HW.  */

    if (VAL("xlnx,use-barrel")) {
        env->pvr.regs[0] |= PVR0_USE_BARREL_MASK;
        env->pvr.regs[2] |= PVR2_USE_BARREL_MASK;
    }

    if (VAL("xlnx,use-div")) {
        env->pvr.regs[0] |= PVR0_USE_DIV_MASK;
        env->pvr.regs[2] |= PVR2_USE_DIV_MASK;
    }

    t = VAL("xlnx,use-hw-mul");
    if (t) {
        env->pvr.regs[0] |= PVR0_USE_HW_MUL_MASK;
        env->pvr.regs[2] |= PVR2_USE_HW_MUL_MASK;
        if (t >= 2) {
            env->pvr.regs[2] |= PVR2_USE_MUL64_MASK;
        }
    }

    if (VAL("xlnx,use-msr-instr")) {
        env->pvr.regs[2] |= PVR2_USE_MSR_INSTR;
    }

    if (VAL("xlnx,use-pcmp-instr")) {
        env->pvr.regs[2] |= PVR2_USE_PCMP_INSTR;
    }

    if (VAL("xlnx,opcode-0x0-illegal")) {
        env->pvr.regs[2] |= PVR2_OPCODE_0x0_ILL_MASK;
    }

    if (VAL("xlnx,unaligned-exceptions")) {
        env->pvr.regs[2] |= PVR2_UNALIGNED_EXC_MASK;
        use_exc = 1;
    }

    if (VAL("xlnx,ill-opcode-exception")) {
        env->pvr.regs[2] |= PVR2_ILL_OPCODE_EXC_MASK;
        use_exc = 1;
    }

    if (VAL("xlnx,iopb-bus-exception")) {
        env->pvr.regs[2] |= PVR2_IOPB_BUS_EXC_MASK;
        use_exc = 1;
    }

    if (VAL("xlnx,dopb-bus-exception")) {
        env->pvr.regs[2] |= PVR2_DOPB_BUS_EXC_MASK;
        use_exc = 1;
    }

    if (VAL("xlnx,div-zero-exception")) {
        env->pvr.regs[2] |= PVR2_DIV_ZERO_EXC_MASK;
        use_exc = 1;
    }

    env->pvr.regs[0] |= VAL("xlnx,pvr-user1") & 0xff;
    env->pvr.regs[1] = VAL("xlnx,pvr-user2");

    /* MMU regs.  */
    t = VAL("xlnx,use-mmu");
    if (use_exc || t) {
        env->pvr.regs[0] |= PVR0_USE_EXC_MASK ;
    }

    env->pvr.regs[11] = t << 30;
    t = VAL("xlnx,mmu-zones");
    env->pvr.regs[11] |= t << 17;
    env->mmu.c_mmu_zones = t;

    t = VAL("xlnx,mmu-tlb-access");
    env->mmu.c_mmu_tlb_access = t;
    env->pvr.regs[11] |= t << 22;

    {
        char *str;
        const struct {
            const char *name;
            unsigned int arch;
        } arch_lookup[] = {
            {"virtex2", 0x4},
            {"virtex2pro", 0x5},
            {"spartan3", 0x6},
            {"virtex4", 0x7},
            {"virtex5", 0x8},
            {"spartan3e", 0x9},
            {"spartan3a", 0xa},
            {"spartan3an", 0xb},
            {"spartan3adsp", 0xc},
            {"spartan6", 0xd},
            {"virtex6", 0xe},
            {"virtex7", 0xf},
            {"kintex7", 0x10},
            {"artix7", 0x11},
            {"zynq7000", 0x12},
            {"spartan2", 0xf0},
            {NULL, 0},
        };
        unsigned int i = 0;

        str = qemu_fdt_getprop(fdt_g, node_path, "xlnx,family", NULL, false, NULL);
        while (arch_lookup[i].name && str) {
            if (strcmp(arch_lookup[i].name, str) == 0) {
                break;
            }
            i++;
        }

        env->pvr.regs[10] &= ~0xff000000;
        if (!str || !arch_lookup[i].arch) {
            env->pvr.regs[10] |= 0x0c000000; /* spartan 3a dsp family.  */
        } else {
            env->pvr.regs[10] |= arch_lookup[i].arch << 24;
        }
        g_free(str);
    }

    {
        env->pvr.regs[4] = PVR4_USE_ICACHE_MASK
                           | (21 << 26) /* Tag size.  */
                           | (4 << 21)
                           | (11 << 16);
        env->pvr.regs[6] = VAL("d-cache-baseaddr");
        env->pvr.regs[7] = VAL("d-cache-highaddr");
        env->pvr.regs[5] = PVR5_USE_DCACHE_MASK
                           | (21 << 26) /* Tag size.  */
                           | (4 << 21)
                           | (11 << 16);

        if (VAL("xlnx,dcache-use-writeback")) {
            env->pvr.regs[5] |= PVR5_DCACHE_WRITEBACK_MASK;
        }

        env->pvr.regs[8] = VAL("i-cache-baseaddr");
        env->pvr.regs[9] = VAL("i-cache-highaddr");
    }
    if (VAL("qemu,halt")) {
        cpu_interrupt(env_cpu(env), CPU_INTERRUPT_HALT);
    }
}

static void secondary_cpu_reset(void *opaque)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(opaque);

    /* Configure secondary cores.  */
    microblaze_generic_fdt_reset(cpu);
}

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
    CPUState *cpu;
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

    fdt_g = fdt;
    kernel_filename = qemu_opt_get(machine_opts, "kernel");
    if (kernel_filename) {
        microblaze_load_kernel(MICROBLAZE_CPU(first_cpu), ram_kernel_base,
                               ram_kernel_size, machine->initrd_filename, NULL,
                               microblaze_generic_fdt_reset, fdt, fdt_size);
    }

    /* Register FDT to prop mapper for secondary cores.  */
    cpu = CPU_NEXT(first_cpu);
    while (cpu) {
        qemu_register_reset(secondary_cpu_reset, cpu);
        cpu = CPU_NEXT(cpu);
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
