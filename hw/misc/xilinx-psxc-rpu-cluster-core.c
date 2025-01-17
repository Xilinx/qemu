/*
 * PSX and PSXC RPU core control registers
 *
 * This model covers versal-net and versal2 versions and can be used for both
 * core 0 and core 1 (one instance per core). Core 1 has the slsplit input GPIO
 * connected while core 0 hasn't.
 *
 * Copyright (c) 2024, Advanced Micro Device, Inc.
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/fdt_generic_util.h"
#include "hw/misc/xilinx-psxc-rpu-cluster-core.h"
#include "cpu.h"
#include "trace.h"

REG32(CFG0, 0x0)
    FIELD(CFG0, CPUHALT, 0, 1)
    FIELD(CFG0, TCMBOOT, 4, 1)
    FIELD(CFG0, REMAP_AXIM_ADDR, 5, 1)
#define CFG0_WRITE_MASK 0x31
#define CFG0_RESET_VAL 0x10

REG32(CFG1, 0x4)
    FIELD(CFG1, L1CACHEINVDIS, 0, 1)
    FIELD(CFG1, ENDIANESS, 4, 1)
    FIELD(CFG1, THUMBEXCEPTIONS, 8, 1)
#define CFG1_WRITE_MASK 0x111
#define CFG1_RESET_VAL 0x0

REG32(VECTABLE, 0x8)
REG32(VERSAL_NET_VECTABLE, 0x10)
    FIELD(VECTABLE, BASE_LO, 5, 3)
    FIELD(VECTABLE, BASE_HI, 8, 24)
#define VECTABLE_WRITE_MASK 0xffffffe0

REG32(PRIMERRIDX, 0xc)
REG32(VERSAL_NET_PRIMERRIDX, 0x40)
    FIELD(PRIMERRIDX, IDX, 0, 25)

REG32(PRIMERRMEM, 0x10)
REG32(VERSAL_NET_PRIMERRMEM, 0x44)
    FIELD(PRIMERRMEM, MEM, 0, 15)

REG32(PRIMERRV, 0x14)
REG32(VERSAL_NET_PRIMERRV, 0x48)
    FIELD(PRIMERRV, VLD, 0, 1)

REG32(SECMERRIDX, 0x18)
REG32(VERSAL_NET_SECMERRIDX, 0x50)
    FIELD(SECMERRIDX, IDX, 0, 25)

REG32(SECMERRMEM, 0x1c)
REG32(VERSAL_NET_SECMERRMEM, 0x54)
    FIELD(SECMERRMEM, MEM, 0, 15)

REG32(SECMERRV, 0x20)
REG32(VERSAL_NET_SECMERRV, 0x58)
    FIELD(SECMERRV, VLD, 0, 1)

REG32(PRIMERRIDX_ALIAS, 0x24)
REG32(VERSAL_NET_PRIMERRIDX_ALIAS, 0x5c)
    FIELD(PRIMERRIDX_ALIAS, IDX, 0, 25)
    FIELD(PRIMERRIDX_ALIAS, PRIMEVLD, 25, 1)

REG32(PRIMERRMEM_ALIAS, 0x28)
REG32(VERSAL_NET_PRIMERRMEM_ALIAS, 0x60)
    FIELD(PRIMERRMEM_ALIAS, MEM, 0, 15)

REG32(SECMERRIDX_ALIAS, 0x2c)
REG32(VERSAL_NET_SECMERRIDX_ALIAS, 0x64)
    FIELD(SECMERRIDX_ALIAS, IDX, 0, 25)
    FIELD(SECMERRIDX_ALIAS, SECMVLD, 25, 1)

REG32(SECMERRMEM_ALIAS, 0x30)
REG32(VERSAL_NET_SECMERRMEM_ALIAS, 0x68)
    FIELD(SECMERRMEM_ALIAS, MEM, 0, 15)

REG32(STATUS, 0x34)
REG32(VERSAL_NET_STATUS, 0x70)

REG32(ERREVENT_FATAL_STS, 0x3c)
REG32(VERSAL_NET_FATAL_STATUS, 0x100)
    FIELD(ERREVENT_FATAL_STS, PAR_MON_ERR0, 26, 1)
    FIELD(ERREVENT_FATAL_STS, PAR_MON_ERR1, 27, 1)

REG32(ERREVENT_FATAL_MASK, 0x40)
REG32(VERSAL_NET_FATAL_MASK, 0x104)
    FIELD(ERREVENT_FATAL_MASK, PAR_MON_ERR0, 26, 1)
    FIELD(ERREVENT_FATAL_MASK, PAR_MON_ERR1, 27, 1)

REG32(ERREVENT_FATAL_EN, 0x44)
REG32(VERSAL_NET_FATAL_EN, 0x108)
    FIELD(ERREVENT_FATAL_EN, PAR_MON_ERR0, 26, 1)
    FIELD(ERREVENT_FATAL_EN, PAR_MON_ERR1, 27, 1)

REG32(ERREVENT_FATAL_DIS, 0x48)
REG32(VERSAL_NET_FATAL_DIS, 0x10c)
    FIELD(ERREVENT_FATAL_DIS, PAR_MON_ERR0, 26, 1)
    FIELD(ERREVENT_FATAL_DIS, PAR_MON_ERR1, 27, 1)

REG32(ERREVENT_FATAL_TRIG, 0x4c)
REG32(VERSAL_NET_FATAL_TRIGG, 0x120)
    FIELD(ERREVENT_FATAL_TRIG, PAR_MON_ERR0, 26, 1)
    FIELD(ERREVENT_FATAL_TRIG, PAR_MON_ERR1, 27, 1)

REG32(ERREVENT_NONFATAL_STS, 0x50)
REG32(VERSAL_NET_CORR_STATUS, 0x124)
    FIELD(ERREVENT_NONFATAL_STS, PWR_ERR, 17, 1)

REG32(ERREVENT_NONFATAL_MASK, 0x54)
REG32(VERSAL_NET_CORR_MASK, 0x128)
    FIELD(ERREVENT_NONFATAL_MASK, PWR_ERR, 17, 1)

REG32(ERREVENT_NONFATAL_EN, 0x58)
REG32(VERSAL_NET_CORR_EN, 0x12c)

REG32(ERREVENT_NONFATAL_DIS, 0x5c)
REG32(VERSAL_NET_CORR_DIS, 0x130)
    FIELD(ERREVENT_NONFATAL_EN, PWR_ERR, 17, 1)
    FIELD(ERREVENT_NONFATAL_DIS, PWR_ERR, 17, 1)

REG32(ERREVENT_NONFATAL_TRIG, 0x60)
REG32(VERSAL_NET_CORR_TRIGG, 0x134)
    FIELD(ERREVENT_NONFATAL_TRIG, PWR_ERR, 17, 1)

REG32(PAR_MON, 0x124)
REG32(VERSAL_NET_PAR_MON, 0x210)
    FIELD(PAR_MON, EN, 0, 1)
    FIELD(PAR_MON, ERROR, 1, 1)

REG32(PWRDWN, 0x200)
REG32(VERSAL_NET_PWRDWN, 0x80)
    FIELD(PWRDWN, EN, 0, 1)

/* versal-net only */
REG32(VERSAL_NET_TIME_OUT_STATUS, 0x138)
REG32(VERSAL_NET_TIME_OUT_MASK, 0x13c)
REG32(VERSAL_NET_TIME_OUT_EN, 0x140)
REG32(VERSAL_NET_TIME_OUT_DIS, 0x144)
REG32(VERSAL_NET_TIME_OUT_TRIGG, 0x148)
REG32(VERSAL_NET_EXCEPTION_STATUS, 0x14c)
REG32(VERSAL_NET_EXCEPTION_MASK, 0x150)
REG32(VERSAL_NET_EXCEPTION_EN, 0x154)
REG32(VERSAL_NET_EXCEPTION_DIS, 0x158)
REG32(VERSAL_NET_EXCEPTION_TRIGG, 0x15c)
REG32(VERSAL_NET_IRQ_STATUS, 0x160)
REG32(VERSAL_NET_IRQ_MASK, 0x164)
REG32(VERSAL_NET_IRQ_EN, 0x168)
REG32(VERSAL_NET_IRQ_DIS, 0x16c)
REG32(VERSAL_NET_IRQ_TRIGG, 0x170)
REG32(VERSAL_NET_IMP_INTMONR_STATUS, 0x174)
REG32(VERSAL_NET_IMP_INTMONR_MASK, 0x178)
REG32(VERSAL_NET_IMP_INTMONR_EN, 0x17c)
REG32(VERSAL_NET_IMP_INTMONR_DIS, 0x180)
REG32(VERSAL_NET_IMP_INTMONR_TRIGG, 0x184)
REG32(VERSAL_NET_ISR, 0x200)
REG32(VERSAL_NET_IMR, 0x204)
REG32(VERSAL_NET_IEN, 0x208)
REG32(VERSAL_NET_IDS, 0x20c)

static void update_gpios(XilinxPsxcRpuClusterCoreState *s)
{
    bool halt, thumb;

    halt = FIELD_EX32(s->cfg0, CFG0, CPUHALT);
    thumb = FIELD_EX32(s->cfg1, CFG1, THUMBEXCEPTIONS);

    qemu_set_irq(s->halt, s->cpu_rst || !s->slsplit || halt);
    qemu_set_irq(s->thumb, thumb);
}

static void update_rvbar(XilinxPsxcRpuClusterCoreState *s)
{
    if (!s->core) {
        return;
    }

    object_property_set_int(OBJECT(s->core), "rvbar", s->vectable,
                            &error_abort);

    if (s->cpu_rst) {
        cpu_set_pc(CPU(s->core), s->vectable);
    }
}

/*
 * Translate versal-net register address to a versal2 one.
 */
static inline hwaddr fixup_addr(XilinxPsxcRpuClusterCoreState *s, hwaddr addr)
{
    if (s->version == XILINX_PSXC_RPU_CLUSTER_CORE_VERSAL2) {
        return addr;
    }

    switch (addr) {
    case A_CFG0 ... A_CFG1:
        return addr;

    case A_VERSAL_NET_VECTABLE:
        return A_VECTABLE;

    case A_VERSAL_NET_PRIMERRIDX ... A_VERSAL_NET_STATUS:
        return addr - (A_VERSAL_NET_PRIMERRIDX - A_PRIMERRIDX);

    case A_VERSAL_NET_FATAL_STATUS ... A_VERSAL_NET_CORR_TRIGG:
        return addr - (A_VERSAL_NET_FATAL_STATUS - A_ERREVENT_FATAL_STS);

    case A_VERSAL_NET_PAR_MON:
        return A_PAR_MON;

    case A_VERSAL_NET_PWRDWN:
        return A_PWRDWN;

    case A_VERSAL_NET_TIME_OUT_STATUS ... A_VERSAL_NET_IMP_INTMONR_TRIGG:
    case A_VERSAL_NET_ISR ... A_VERSAL_NET_IDS:
        /* those don't exist on versal2 */
        return addr;

    default:
        /*
         * Make sure we return an invalid offset for the rest so versal2
         * registers offset are treated as invalid for versal-net.
         */
        return -1;
    }
}

static uint64_t xilinx_psxc_rpu_cluster_core_read(void *opaque,
                                                  hwaddr real_addr,
                                                  unsigned int size)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(opaque);
    uint64_t ret;
    hwaddr addr;

    addr = fixup_addr(s, real_addr);

    switch (addr) {
    case A_CFG0:
        ret = s->cfg0;
        break;

    case A_CFG1:
        ret = s->cfg1;
        break;

    case A_PWRDWN:
        ret = s->pwrdwn;
        break;

    case A_VECTABLE:
        ret = s->vectable;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_XILINX_PSXC_RPU_CLUSTER_CORE
                      ": read to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_xilinx_psxc_rpu_cluster_core_read(real_addr, ret, size);
    return ret;
}

static void xilinx_psxc_rpu_cluster_core_write(void *opaque, hwaddr real_addr,
                                               uint64_t value,
                                               unsigned int size)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(opaque);
    hwaddr addr;

    addr = fixup_addr(s, real_addr);

    trace_xilinx_psxc_rpu_cluster_core_write(real_addr, value, size);

    switch (addr) {
    case A_CFG0:
        s->cfg0 = value & CFG0_WRITE_MASK;
        update_gpios(s);
        break;

    case A_CFG1:
        s->cfg1 = value & CFG1_WRITE_MASK;
        update_gpios(s);
        break;

    case A_PWRDWN:
        s->pwrdwn = value & 0x1;
        break;

    case A_VECTABLE:
        s->vectable = value & VECTABLE_WRITE_MASK;
        update_rvbar(s);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_XILINX_PSXC_RPU_CLUSTER_CORE
                      ": write to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps xilinx_psxc_rpu_cluster_core_ops = {
    .read = xilinx_psxc_rpu_cluster_core_read,
    .write = xilinx_psxc_rpu_cluster_core_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void slsplit_handler(void *opaque, int irq, int level)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(opaque);

    s->slsplit = level;
    update_gpios(s);
}

REG32(XTCMREGIONR, 0)
    FIELD(XTCMREGIONR, ENABLE_EL1_EL0, 0, 1)
    FIELD(XTCMREGIONR, ENABLE_EL2, 1, 1)
    FIELD(XTCMREGIONR, SIZE, 2, 5)
    FIELD(XTCMREGIONR, WAITSTATES, 8, 1)
    FIELD(XTCMREGIONR, BASEADDRESS, 13, 19)

static inline uint32_t format_imp_xtcmregionr_reg(uint32_t base,
                                                  bool waitstates,
                                                  size_t size,
                                                  bool enable_el2,
                                                  bool enable_el1_el0)
{
    uint32_t ret = 0;

    if (size == 0) {
        return ret;
    }

    g_assert(size >= 8 * KiB);
    g_assert(size <= 1 * MiB);
    g_assert(is_power_of_2(size));

    ret = FIELD_DP32(ret, XTCMREGIONR, ENABLE_EL1_EL0, enable_el1_el0);
    ret = FIELD_DP32(ret, XTCMREGIONR, ENABLE_EL2, enable_el2);
    ret = FIELD_DP32(ret, XTCMREGIONR, SIZE, size >> 11);
    ret = FIELD_DP32(ret, XTCMREGIONR, WAITSTATES, waitstates);
    ret = FIELD_DP32(ret, XTCMREGIONR, BASEADDRESS, base >> 13);

    return ret;
}

static void rpu_core_reset_tcm_regions(XilinxPsxcRpuClusterCoreState *s)
{
    ARMCPU *cpu;

    if (!s->core) {
        return;
    }

    cpu = ARM_CPU(s->core);

    cpu->env.tcmregion.a = format_imp_xtcmregionr_reg(0x0, false,
                                                      64 * KiB, true, true);
    cpu->env.tcmregion.b = format_imp_xtcmregionr_reg(0x10000, false,
                                                      32 * KiB, true, true);
    cpu->env.tcmregion.c = format_imp_xtcmregionr_reg(0x20000, false,
                                                      32 * KiB, true, true);
}

static void rpu_core_rst_handler(void *opaque, int irq, int level)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(opaque);

    s->cpu_rst = level;
    update_gpios(s);
    rpu_core_reset_tcm_regions(s);
}

static void xilinx_psxc_rpu_cluster_core_reset_enter(Object *obj,
                                                     ResetType type)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(obj);

    s->cfg0 = CFG0_RESET_VAL;
    s->cfg1 = CFG1_RESET_VAL;
    s->vectable = 0;
    s->pwrdwn = false;
}

static void xilinx_psxc_rpu_cluster_core_reset_hold(Object *obj)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(obj);

    update_gpios(s);
    rpu_core_reset_tcm_regions(s);
}

static void xilinx_psxc_rpu_cluster_core_realize(DeviceState *dev,
                                                 Error **errp)
{
    XilinxPsxcRpuClusterCoreState *s = XILINX_PSXC_RPU_CLUSTER_CORE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_init_gpio_in_named(dev, rpu_core_rst_handler, "core-rst", 1);
    qdev_init_gpio_in_named(dev, slsplit_handler, "slsplit", 1);
    qdev_init_gpio_out_named(dev, &s->halt, "halt", 1);
    qdev_init_gpio_out_named(dev, &s->thumb, "thumb", 1);

    memory_region_init_io(&s->iomem, OBJECT(dev),
                          &xilinx_psxc_rpu_cluster_core_ops,
                          s, TYPE_XILINX_PSXC_RPU_CLUSTER_CORE,
                          XILINX_PSXC_RPU_CLUSTER_CORE_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    /*
     * When the slsplit GPIO is left unconnected, this device controls core 0
     * and not core 1. In this case slsplit is set to true here and will stay
     * like this for the entire lifetime of the device.
     */
    s->slsplit = true;
}

static Property xilinx_psxc_rpu_cluster_core_properties[] = {
    DEFINE_PROP_LINK("core", XilinxPsxcRpuClusterCoreState, core,
                     TYPE_ARM_CPU, DeviceState *),
    DEFINE_PROP_LINK("tcm-mr", XilinxPsxcRpuClusterCoreState, tcm_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_UINT32("version", XilinxPsxcRpuClusterCoreState, version,
                       XILINX_PSXC_RPU_CLUSTER_CORE_VERSAL_NET),
    DEFINE_PROP_END_OF_LIST()
};

static const FDTGenericGPIOSet xilinx_psxc_rpu_cluster_core_cntrl_gpio[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "halt", .fdt_index = 0, .range = 1},
            { .name = "thumb", .fdt_index = 1, .range = 1},
            { },
        },
    },
    { },
};

static const FDTGenericGPIOSet xilinx_psxc_rpu_cluster_core_client_gpio[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "core-rst", .fdt_index = 0, .range = 1},
            { .name = "slsplit", .fdt_index = 1, .range = 1},
            { },
        },
    },
    { },
};

static void xilinx_psxc_rpu_cluster_core_class_init(ObjectClass *klass,
                                                    void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);

    dc->realize = xilinx_psxc_rpu_cluster_core_realize;
    rc->phases.enter = xilinx_psxc_rpu_cluster_core_reset_enter;
    rc->phases.hold = xilinx_psxc_rpu_cluster_core_reset_hold;
    fggc->controller_gpios = xilinx_psxc_rpu_cluster_core_cntrl_gpio;
    fggc->client_gpios = xilinx_psxc_rpu_cluster_core_client_gpio;
    device_class_set_props(dc, xilinx_psxc_rpu_cluster_core_properties);
}

static const TypeInfo xilinx_psxc_rpu_cluster_core_info = {
    .name = TYPE_XILINX_PSXC_RPU_CLUSTER_CORE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxPsxcRpuClusterCoreState),
    .class_init = xilinx_psxc_rpu_cluster_core_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
};

static void xilinx_psxc_rpu_cluster_core_register_types(void)
{
    type_register_static(&xilinx_psxc_rpu_cluster_core_info);
}

type_init(xilinx_psxc_rpu_cluster_core_register_types)
