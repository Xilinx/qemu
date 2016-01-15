/*
 * Xilinx Zynq Baseboard System emulation.
 *
 * Copyright (c) 2010 Xilinx.
 * Copyright (c) 2012 Peter A.G. Crosthwaite (peter.croshtwaite@petalogix.com)
 * Copyright (c) 2012 Petalogix Pty Ltd.
 * Written by Haibing Ma
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "net/net.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/block/flash.h"
#include "sysemu/block-backend.h"
#include "hw/loader.h"
#include "hw/ssi.h"
#include "qemu/error-report.h"
#include "hw/i2c/i2c.h"
#include "hw/cpu/a9mpcore.h"

#define MAX_CPUS 2

#define NUM_SPI_FLASHES 4
#define NUM_QSPI_FLASHES 1
#define NUM_QSPI_BUSSES 2

#define FLASH_SIZE (64 * 1024 * 1024)
#define FLASH_SECTOR_SIZE (128 * 1024)

#define NUM_I2C_EEPROMS 2

#define IRQ_OFFSET 32 /* pic interrupts start from index 32 */

#define MPCORE_PERIPHBASE   0xF8F00000
#define OCM_BASE            0xFFFC0000
#define OCM_SIZE            (256 << 10)

/* Put SMP bootloader up top of OCM  */
#define SMP_BOOT_ADDR ((uint64_t)OCM_BASE + OCM_SIZE - sizeof(zynq_smpboot))

#define ZYNQ_BOARD_MIDR 0x413FC090

static const int dma_irqs[8] = {
    46, 47, 48, 49, 72, 73, 74, 75
};

/* Entry point for secondary CPU. Zynq Linux SMP protocol is to just reset
 * the secondary to unpen, so any infinite loop will do the trick. Use a WFI
 * loop as that will cause the emulated CPU to halt (and remove itself from
 * the work queue pending an interrupt that never comes).
 */
static uint32_t zynq_smpboot[] = {
    0xe320f003, /* wfi */
    0xeafffffd, /* b       <b wfi> */
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

static void zynq_reset_secondary(ARMCPU *cpu,
                                  const struct arm_boot_info *info)
{
    CPUARMState *env = &cpu->env;

    env->regs[15] = info->smp_loader_start;
}

static struct arm_boot_info zynq_binfo = {};

static void gem_init(NICInfo *nd, uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "cadence_gem");
    if (nd->used) {
        qemu_check_nic_model(nd, "cadence_gem");
        qdev_set_nic_properties(dev, nd);
    }
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, base);
    sysbus_connect_irq(s, 0, irq);
}

static inline void zynq_init_spi_flashes(uint32_t base_addr, qemu_irq irq,
                                         bool is_qspi)
{
    DeviceState *dev;
    SysBusDevice *busdev;
    SSIBus *spi;
    DeviceState *flash_dev;
    int i, j;
    int num_busses =  is_qspi ? NUM_QSPI_BUSSES : 1;
    int num_ss = is_qspi ? NUM_QSPI_FLASHES : NUM_SPI_FLASHES;

    dev = qdev_create(NULL, is_qspi ? "xlnx.ps7-qspi" : "cdns.spi-r1p6");
    qdev_prop_set_uint8(dev, "num-txrx-bytes", is_qspi ? 4 : 1);
    qdev_prop_set_uint8(dev, "num-ss-bits", num_ss);
    qdev_prop_set_uint8(dev, "num-busses", num_busses);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, base_addr);
    if (is_qspi) {
        sysbus_mmio_map(busdev, 1, 0xFC000000);
    }
    sysbus_connect_irq(busdev, 0, irq);

    for (i = 0; i < num_busses; ++i) {
        char bus_name[16];
        qemu_irq cs_line;

        snprintf(bus_name, 16, "spi%d", i);
        spi = (SSIBus *)qdev_get_child_bus(dev, bus_name);

        for (j = 0; j < num_ss; ++j) {
            flash_dev = ssi_create_slave(spi, "n25q128");

            cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
            sysbus_connect_irq(busdev, i * num_ss + j + 1, cs_line);
        }
    }

}

static inline void zynq_init_zc70x_i2c(uint32_t base_addr, qemu_irq irq)
{
    DeviceState *dev = sysbus_create_simple("cdns.i2c-r1p10", base_addr, irq);
    I2CBus *i2c = (I2CBus *)qdev_get_child_bus(dev, "i2c");
    int i, bus;

    dev = i2c_create_slave(i2c, "pca9548", 0);
    for (bus = 2; bus <= 3; bus++) {
        char bus_name[16];

        snprintf(bus_name, sizeof(bus_name), "i2c@%d", bus);
        i2c = (I2CBus *)qdev_get_child_bus(dev, bus_name);
        assert(i2c);

        assert(NUM_I2C_EEPROMS <= 2); /* not enough address space for anymore */
        for (i = 0; i < NUM_I2C_EEPROMS; ++i) {
            DeviceState *eeprom_dev = i2c_create_slave_no_init(i2c, "at.24c08",
                                                               0x50 + 0x4 * i);
            qdev_prop_set_uint16(eeprom_dev, "size", 1024); /* M24C08 */
            qdev_init_nofail(eeprom_dev);
        }
    }
}

static void zynq_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    A9MPPrivState *mpcore;
    ObjectClass *cpu_oc;
    ARMCPU *cpu[MAX_CPUS];
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ext_ram = g_new(MemoryRegion, 1);
    MemoryRegion *ocm_ram = g_new(MemoryRegion, 1);
    DeviceState *dev;
    SysBusDevice *busdev;
    qemu_irq pic[64];
    Error *err = NULL;
    int n;

    if (machine->cpu_model) {
        error_report("Zynq does not support CPU model override!\n");
        exit(1);
    }
    if (!cpu_model) {
        cpu_model = "cortex-a9";
    }
    cpu_oc = cpu_class_by_name(TYPE_ARM_CPU, cpu_model);

    for (n = 0; n < smp_cpus; n++) {
        cpu[n] = ARM_CPU(object_new(object_class_get_name(cpu_oc)));

        /* By default A9 CPUs have EL3 enabled.  This board does not
         * currently support EL3 so the CPU EL3 property is disabled before
         * realization.
         */
        if (object_property_find(OBJECT(cpu[n]), "has_el3", NULL)) {
            object_property_set_bool(OBJECT(cpu[n]), false, "has_el3", &err);
            if (err) {
                error_report("%s", error_get_pretty(err));
                exit(1);
            }
        }

        object_property_set_int(OBJECT(cpu[n]), ZYNQ_BOARD_MIDR, "midr", &err);
        if (err) {
            error_report("%s", error_get_pretty(err));
            exit(1);
        }

        object_property_set_int(OBJECT(cpu[n]), MPCORE_PERIPHBASE,
                                "reset-cbar", &err);
        if (err) {
            error_report("%s", error_get_pretty(err));
            exit(1);
        }

        object_property_set_bool(OBJECT(cpu[n]), true, "realized", &err);
        if (err) {
            error_report("%s", error_get_pretty(err));
            exit(1);
        }
    }

    /* max 2GB ram */
    if (ram_size > 0x80000000) {
        ram_size = 0x80000000;
    }

    /* pl353 */
    dev = qdev_create(NULL, "arm.pl35x");
    /* FIXME: handle this somewhere central */
    object_property_add_child(container_get(qdev_get_machine(), "/unattached"),
                              "pl353", OBJECT(dev), NULL);
    qdev_prop_set_uint8(dev, "x", 3);
    {
        DriveInfo *dinfo = drive_get_next(IF_PFLASH);
        BlockBackend *blk =  dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
        DeviceState *att_dev = qdev_create(NULL, "cfi.pflash02");
        Error *errp = NULL;

        if (blk && qdev_prop_set_drive(att_dev, "drive", blk)) {
            abort();
        }
        qdev_prop_set_uint32(att_dev, "num-blocks",
                             FLASH_SIZE/FLASH_SECTOR_SIZE);
        qdev_prop_set_uint32(att_dev, "sector-length", FLASH_SECTOR_SIZE);
        qdev_prop_set_uint8(att_dev, "width", 1);
        qdev_prop_set_uint8(att_dev, "mappings", 1);
        qdev_prop_set_uint8(att_dev, "big-endian", 0);
        qdev_prop_set_uint16(att_dev, "id0", 0x0066);
        qdev_prop_set_uint16(att_dev, "id1", 0x0022);
        qdev_prop_set_uint16(att_dev, "id2", 0x0000);
        qdev_prop_set_uint16(att_dev, "id3", 0x0000);
        qdev_prop_set_uint16(att_dev, "unlock-addr0", 0x0aaa);
        qdev_prop_set_uint16(att_dev, "unlock-addr1", 0x0555);
        qdev_prop_set_string(att_dev, "name", "pl353.pflash");
        qdev_init_nofail(att_dev);
        object_property_set_link(OBJECT(dev), OBJECT(att_dev), "dev0", &errp);
        if (err) {
            error_report("%s", error_get_pretty(err));
            exit(1);
        }

        dinfo = drive_get_next(IF_PFLASH);
        att_dev = nand_init(dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                            NAND_MFR_STMICRO, 0xaa);
        object_property_set_link(OBJECT(dev), OBJECT(att_dev), "dev1", &errp);
        if (err) {
            error_report("%s", error_get_pretty(err));
            exit(1);
        }
    }
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xe000e000);
    sysbus_mmio_map(busdev, 1, 0xe2000000);
    sysbus_mmio_map(busdev, 2, 0xe1000000);

    /* DDR remapped to address zero.  */
    memory_region_allocate_system_memory(ext_ram, NULL, "zynq.ext_ram",
                                         ram_size);
    memory_region_add_subregion(address_space_mem, 0, ext_ram);

    /* 256K of on-chip memory */
    memory_region_init_ram(ocm_ram, NULL, "zynq.ocm_ram", 256 << 10,
                           &error_abort);
    vmstate_register_ram_global(ocm_ram);
    memory_region_add_subregion(address_space_mem, OCM_BASE, ocm_ram);

    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);

    /* AMD */
    pflash_cfi02_register(0xe2000000, NULL, "zynq.pflash", FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          FLASH_SECTOR_SIZE,
                          FLASH_SIZE/FLASH_SECTOR_SIZE, 1,
                          1, 0x0066, 0x0022, 0x0000, 0x0000, 0x0555, 0x2aa,
                              0);

    dev = qdev_create(NULL, "xilinx,zynq_slcr");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xF8000000);
    for (n = 0; n < smp_cpus; n++) {
        qdev_connect_gpio_out(dev, n,
                              qdev_get_gpio_in(DEVICE(cpu[n]), 0));
    }

    mpcore = A9MPCORE_PRIV(object_new("a9mpcore_priv"));
    qdev_prop_set_uint32(DEVICE(mpcore), "num-cpu", smp_cpus);
    qdev_prop_set_uint32(DEVICE(mpcore), "midr", ZYNQ_BOARD_MIDR);
    qdev_prop_set_uint64(DEVICE(mpcore), "reset-cbar", MPCORE_PERIPHBASE);
    object_property_set_bool(OBJECT(mpcore), true, "realized", &err);
    if (err != NULL) {
        error_report("Couldn't realize the Zynq A9MPCore: %s",
                     error_get_pretty(err));
        exit(1);
    }
    busdev = SYS_BUS_DEVICE(DEVICE(mpcore));
    sysbus_mmio_map(busdev, 0, MPCORE_PERIPHBASE);
    for (n = 0; n < smp_cpus; n++) {
        sysbus_connect_irq(busdev, n,
                           qdev_get_gpio_in(DEVICE(cpu[n]), ARM_CPU_IRQ));
    }

    for (n = 0; n < 64; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    zynq_init_zc70x_i2c(0xE0004000, pic[57-IRQ_OFFSET]);
    zynq_init_zc70x_i2c(0xE0005000, pic[80-IRQ_OFFSET]);
    dev = qdev_create(NULL, "xlnx,ps7-usb");
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xE0002000);
    sysbus_connect_irq(busdev, 0, pic[53-IRQ_OFFSET]);

    dev = qdev_create(NULL, "xlnx,ps7-usb");
    busdev = SYS_BUS_DEVICE(dev);
    qdev_init_nofail(dev);
    sysbus_mmio_map(busdev, 0, 0xE0003000);
    sysbus_connect_irq(busdev, 0, pic[76-IRQ_OFFSET]);

    zynq_init_spi_flashes(0xE0006000, pic[58-IRQ_OFFSET], false);
    zynq_init_spi_flashes(0xE0007000, pic[81-IRQ_OFFSET], false);
    zynq_init_spi_flashes(0xE000D000, pic[51-IRQ_OFFSET], true);

    sysbus_create_simple("cadence_uart", 0xE0000000, pic[59-IRQ_OFFSET]);
    sysbus_create_simple("cadence_uart", 0xE0001000, pic[82-IRQ_OFFSET]);

    sysbus_create_varargs("cadence_ttc", 0xF8001000,
            pic[42-IRQ_OFFSET], pic[43-IRQ_OFFSET], pic[44-IRQ_OFFSET], NULL);
    sysbus_create_varargs("cadence_ttc", 0xF8002000,
            pic[69-IRQ_OFFSET], pic[70-IRQ_OFFSET], pic[71-IRQ_OFFSET], NULL);

    gem_init(&nd_table[0], 0xE000B000, pic[54-IRQ_OFFSET]);
    gem_init(&nd_table[1], 0xE000C000, pic[77-IRQ_OFFSET]);

    dev = qdev_create(NULL, "generic-sdhci");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xE0100000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[56-IRQ_OFFSET]);

    dev = qdev_create(NULL, "generic-sdhci");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xE0101000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[79-IRQ_OFFSET]);

    dev = qdev_create(NULL, "pl330");
    qdev_prop_set_uint8(dev, "num_chnls",  8);
    qdev_prop_set_uint8(dev, "num_periph_req",  4);
    qdev_prop_set_uint8(dev, "num_events",  16);

    qdev_prop_set_uint8(dev, "data_width",  64);
    qdev_prop_set_uint8(dev, "wr_cap",  8);
    qdev_prop_set_uint8(dev, "wr_q_dep",  16);
    qdev_prop_set_uint8(dev, "rd_cap",  8);
    qdev_prop_set_uint8(dev, "rd_q_dep",  16);
    qdev_prop_set_uint16(dev, "data_buffer_dep",  256);

    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xF8003000);
    sysbus_connect_irq(busdev, 0, pic[45-IRQ_OFFSET]); /* abort irq line */
    for (n = 0; n < 8; ++n) { /* event irqs */
        sysbus_connect_irq(busdev, n + 1, pic[dma_irqs[n] - IRQ_OFFSET]);
    }

    dev = qdev_create(NULL, "xlnx.ps7-dev-cfg");
    object_property_add_child(qdev_get_machine(), "xilinx-devcfg", OBJECT(dev),
                              NULL);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, pic[40-IRQ_OFFSET]);
    sysbus_mmio_map(busdev, 0, 0xF8007000);

    zynq_binfo.ram_size = ram_size;
    zynq_binfo.kernel_filename = kernel_filename;
    zynq_binfo.kernel_cmdline = kernel_cmdline;
    zynq_binfo.initrd_filename = initrd_filename;
    zynq_binfo.nb_cpus = smp_cpus;
    zynq_binfo.write_secondary_boot = zynq_write_secondary_boot;
    zynq_binfo.secondary_cpu_reset_hook = zynq_reset_secondary;
    zynq_binfo.smp_loader_start = SMP_BOOT_ADDR;
    zynq_binfo.board_id = 0xd32;
    zynq_binfo.loader_start = 0;

    arm_load_kernel(ARM_CPU(first_cpu), &zynq_binfo);
}

static QEMUMachine zynq_machine = {
    .name = "xilinx-zynq-a9",
    .desc = "Xilinx Zynq Platform Baseboard for Cortex-A9",
    .init = zynq_init,
    .block_default_type = IF_SCSI,
    .max_cpus = MAX_CPUS,
    .no_sdcard = 1,
};

static void zynq_machine_init(void)
{
    qemu_register_machine(&zynq_machine);
}

machine_init(zynq_machine_init);
