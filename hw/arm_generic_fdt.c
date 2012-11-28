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

#include "sysbus.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "boards.h"
#include "exec-memory.h"
#include "loader.h"

#include "fdt_generic_util.h"

#define MACHINE_NAME "arm-generic-fdt"

#define MAX_CPUS 4

#define SMP_BOOT_ADDR 0xfffc0000
#define SMP_BOOTREG_ADDR 0xfffffff0

/* Entry point for secondary CPU */
static uint32_t zynq_smpboot[] = {
    0xe3e0000f, /* ldr r0, =0xfffffff0 (mvn r0, #15) */
    0xe320f002, /* wfe */
    0xe5901000, /* ldr     r1, [r0] */
    0xe1110001, /* tst     r1, r1 */
    0x0afffffb, /* beq     <wfe> */
    0xe12fff11, /* bx      r1 */
    0,
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

static struct arm_boot_info arm_generic_fdt_binfo = {};

static void arm_generic_fdt_init(QEMUMachineInitArgs *args)
{
    const char *cpu_model = args->cpu_model;
    ARMCPU *cpus[MAX_CPUS];
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    ram_addr_t ram_base, ram_size;
    qemu_irq cpu_irq[MAX_CPUS+1];
    memset(cpu_irq, 0, sizeof(cpu_irq));

    void *fdt;
    int fdt_size;
    const char *dtb_arg;
    QemuOpts *machine_opts;
    char node_path[DT_PATH_LENGTH];

    Error *errp = NULL;
    int n;

    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (!machine_opts) {
        goto no_dtb_arg;
    }
    dtb_arg = qemu_opt_get(machine_opts, "dtb");
    if (!dtb_arg) {
        goto no_dtb_arg;
    }

    fdt = load_device_tree(dtb_arg, &fdt_size);
    if (!fdt) {
        hw_error("Error: Unable to load Device Tree %s\n", dtb_arg);
        return;
    }

    if (!cpu_model) {
        cpu_model = "cortex-a9";
    }

    for (n = 0; n < smp_cpus; n++) {
        qemu_irq *irqp;

        cpus[n] = cpu_arm_init(cpu_model);
        if (!cpus[n]) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        irqp = arm_pic_init_cpu(cpus[n]);
        cpu_irq[n] = irqp[ARM_PIC_CPU_IRQ];
        /* FIXME: handle this somewhere central */
        object_property_add_child(container_get(qdev_get_machine(),
                                  "/unattached"), g_strdup_printf("cpu[%d]", n),
                                  OBJECT(cpus[n]), NULL);
    }

    /* find memory node */
    /* FIXME it could be good to fix case when you don't find memory node */
    qemu_devtree_get_node_by_name(fdt, node_path, "memory@");
    ram_base = qemu_devtree_getprop_cell(fdt, node_path, "reg", 0,
                                         false, &errp);
    ram_size = qemu_devtree_getprop_cell(fdt, node_path, "reg", 1,
                                         false, &errp);
    assert_no_error(errp);

    /* Memory node */
    memory_region_init_ram(ram, "zynq.ext_ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space_mem, ram_base, ram);

    /*FIXME: Describe OCM in DTB and delete this */
    /* ZYNQ OCM: */
    {
        MemoryRegion *ocm_ram = g_new(MemoryRegion, 1);
        memory_region_init_ram(ocm_ram, "zynq.ocm_ram", 256 << 10);
        vmstate_register_ram_global(ocm_ram);
        memory_region_add_subregion(address_space_mem, 0xFFFC0000, ocm_ram);
    }

    /* Instantiate peripherals from the FDT.  */
    fdt_init_destroy_fdti(fdt_generic_create_machine(fdt, cpu_irq));
    arm_generic_fdt_binfo.fdt = fdt;
    arm_generic_fdt_binfo.fdt_size = fdt_size;

    arm_generic_fdt_binfo.ram_size = ram_size;
    arm_generic_fdt_binfo.kernel_filename = args->kernel_filename;
    arm_generic_fdt_binfo.kernel_cmdline = args->kernel_cmdline;
    arm_generic_fdt_binfo.initrd_filename = args->initrd_filename;
    arm_generic_fdt_binfo.nb_cpus = smp_cpus;
    arm_generic_fdt_binfo.write_secondary_boot = zynq_write_secondary_boot;
    arm_generic_fdt_binfo.smp_loader_start = SMP_BOOT_ADDR;
    arm_generic_fdt_binfo.smp_bootreg_addr = SMP_BOOTREG_ADDR;
    arm_generic_fdt_binfo.board_id = 0xd32;
    arm_generic_fdt_binfo.loader_start = 0;
    arm_load_kernel(arm_env_get_cpu(first_cpu), &arm_generic_fdt_binfo);

    return;

no_dtb_arg:
    hw_error("DTB must be specified for %s machine model\n", MACHINE_NAME);
    return;

}

static QEMUMachine arm_generic_fdt_machine = {
    .name = MACHINE_NAME,
    .desc = "ARM device tree driven machine model",
    .init = arm_generic_fdt_init,
    .use_scsi = 1,
    .max_cpus = MAX_CPUS,
    .no_sdcard = 1
};

static void arm_generic_fdt_machine_init(void)
{
    qemu_register_machine(&arm_generic_fdt_machine);
}

machine_init(arm_generic_fdt_machine_init);
