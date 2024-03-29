/*
 * QEMU model of the PSX_CRF APB control registers for clock controller.
 *
 * Copyright (c) 2021 Xilinx Inc.
 *
 * Partially autogenerated by xregqemu.py 2021-04-20.
 * Written by Edgar E. Iglesias.
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
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/arm/linux-boot-if.h"

#include "hw/fdt_generic_util.h"

#ifndef XILINX_PSX_CRF_ERR_DEBUG
#define XILINX_PSX_CRF_ERR_DEBUG 0
#endif

#define TYPE_XILINX_PSX_CRF "xlnx.versal-psx-crf"

#define XILINX_PSX_CRF(obj) \
     OBJECT_CHECK(PSX_CRF, (obj), TYPE_XILINX_PSX_CRF)

REG32(ERR_CTRL, 0x0)
REG32(WPROT, 0x1c)
    FIELD(WPROT, ACTIVE, 0, 1)
REG32(APLL1_CTRL, 0x40)
    FIELD(APLL1_CTRL, POST_SRC, 24, 3)
    FIELD(APLL1_CTRL, PRE_SRC, 20, 3)
    FIELD(APLL1_CTRL, CLKOUTDIV, 16, 2)
    FIELD(APLL1_CTRL, FBDIV, 8, 8)
    FIELD(APLL1_CTRL, BYPASS, 3, 1)
    FIELD(APLL1_CTRL, RESET, 0, 1)
REG32(APLL1_CFG, 0x44)
    FIELD(APLL1_CFG, LOCK_DLY, 25, 7)
    FIELD(APLL1_CFG, LOCK_CNT, 13, 10)
    FIELD(APLL1_CFG, LFHF, 10, 2)
    FIELD(APLL1_CFG, CP, 5, 4)
    FIELD(APLL1_CFG, RES, 0, 4)
REG32(APLL2_CTRL, 0x50)
    FIELD(APLL2_CTRL, POST_SRC, 24, 3)
    FIELD(APLL2_CTRL, PRE_SRC, 20, 3)
    FIELD(APLL2_CTRL, CLKOUTDIV, 16, 2)
    FIELD(APLL2_CTRL, FBDIV, 8, 8)
    FIELD(APLL2_CTRL, BYPASS, 3, 1)
    FIELD(APLL2_CTRL, RESET, 0, 1)
REG32(APLL2_CFG, 0x54)
    FIELD(APLL2_CFG, LOCK_DLY, 25, 7)
    FIELD(APLL2_CFG, LOCK_CNT, 13, 10)
    FIELD(APLL2_CFG, LFHF, 10, 2)
    FIELD(APLL2_CFG, CP, 5, 4)
    FIELD(APLL2_CFG, RES, 0, 4)
REG32(PLL_STATUS, 0x60)
    FIELD(PLL_STATUS, APLL2_STABLE, 3, 1)
    FIELD(PLL_STATUS, APLL1_STABLE, 2, 1)
    FIELD(PLL_STATUS, APLL2_LOCK, 1, 1)
    FIELD(PLL_STATUS, APLL1_LOCK, 0, 1)
REG32(FPX_TOP_SWITCH_CTRL, 0x104)
    FIELD(FPX_TOP_SWITCH_CTRL, CLKACT, 25, 1)
    FIELD(FPX_TOP_SWITCH_CTRL, DIVISOR0, 8, 10)
    FIELD(FPX_TOP_SWITCH_CTRL, SRCSEL, 0, 3)
REG32(FPX_LSBUS_CTRL, 0x108)
    FIELD(FPX_LSBUS_CTRL, CLKACT, 25, 1)
    FIELD(FPX_LSBUS_CTRL, DIVISOR0, 8, 10)
    FIELD(FPX_LSBUS_CTRL, SRCSEL, 0, 3)
REG32(ACPU0_CLK_CTRL, 0x10c)
    FIELD(ACPU0_CLK_CTRL, CLKACT, 25, 1)
    FIELD(ACPU0_CLK_CTRL, DIVISOR0, 8, 10)
    FIELD(ACPU0_CLK_CTRL, SRCSEL, 0, 3)
REG32(ACPU1_CLK_CTRL, 0x110)
    FIELD(ACPU1_CLK_CTRL, CLKACT, 25, 1)
    FIELD(ACPU1_CLK_CTRL, DIVISOR0, 8, 10)
    FIELD(ACPU1_CLK_CTRL, SRCSEL, 0, 3)
REG32(ACPU2_CLK_CTRL, 0x114)
    FIELD(ACPU2_CLK_CTRL, CLKACT, 25, 1)
    FIELD(ACPU2_CLK_CTRL, DIVISOR0, 8, 10)
    FIELD(ACPU2_CLK_CTRL, SRCSEL, 0, 3)
REG32(ACPU3_CLK_CTRL, 0x118)
    FIELD(ACPU3_CLK_CTRL, CLKACT, 25, 1)
    FIELD(ACPU3_CLK_CTRL, DIVISOR0, 8, 10)
    FIELD(ACPU3_CLK_CTRL, SRCSEL, 0, 3)
REG32(DBG_TRACE_CTRL, 0x120)
    FIELD(DBG_TRACE_CTRL, CLKACT, 25, 1)
    FIELD(DBG_TRACE_CTRL, DIVISOR0, 8, 10)
    FIELD(DBG_TRACE_CTRL, SRCSEL, 0, 3)
REG32(DBG_FPX_CTRL, 0x124)
    FIELD(DBG_FPX_CTRL, CLKACT, 25, 1)
    FIELD(DBG_FPX_CTRL, DIVISOR0, 8, 10)
    FIELD(DBG_FPX_CTRL, SRCSEL, 0, 3)
REG32(PERIPH_CLK_CTRL, 0x128)
    FIELD(PERIPH_CLK_CTRL, DIVISOR0, 8, 10)
    FIELD(PERIPH_CLK_CTRL, SRCSEL, 0, 3)
REG32(WWDT_PLL_CLK_CTRL, 0x12c)
    FIELD(WWDT_PLL_CLK_CTRL, DIVISOR0, 8, 10)
    FIELD(WWDT_PLL_CLK_CTRL, SRCSEL, 0, 3)
REG32(FPX_PKI_DIV_CLK_CTRL, 0x130)
    FIELD(FPX_PKI_DIV_CLK_CTRL, DIVISOR0, 8, 10)
REG32(RCLK_CTRL, 0x134)
    FIELD(RCLK_CTRL, CLKACT, 14, 12)
    FIELD(RCLK_CTRL, SELECT, 0, 12)
REG32(SAFETY_CHK, 0x150)
REG32(RST_APU0, 0x300)
    FIELD(RST_APU0, CLUSTER_COLD_RESET, 9, 1)
    FIELD(RST_APU0, CLUSTER_WARM_RESET, 8, 1)
    FIELD(RST_APU0, CORE3_WARM_RESET, 7, 1)
    FIELD(RST_APU0, CORE2_WARM_RESET, 6, 1)
    FIELD(RST_APU0, CORE1_WARM_RESET, 5, 1)
    FIELD(RST_APU0, CORE0_WARM_RESET, 4, 1)
    FIELD(RST_APU0, CORE3_COLD_RESET, 3, 1)
    FIELD(RST_APU0, CORE2_COLD_RESET, 2, 1)
    FIELD(RST_APU0, CORE1_COLD_RESET, 1, 1)
    FIELD(RST_APU0, CORE0_COLD_RESET, 0, 1)
REG32(RST_APU1, 0x304)
    FIELD(RST_APU1, CLUSTER_COLD_RESET, 9, 1)
    FIELD(RST_APU1, CLUSTER_WARM_RESET, 8, 1)
    FIELD(RST_APU1, CORE3_WARM_RESET, 7, 1)
    FIELD(RST_APU1, CORE2_WARM_RESET, 6, 1)
    FIELD(RST_APU1, CORE1_WARM_RESET, 5, 1)
    FIELD(RST_APU1, CORE0_WARM_RESET, 4, 1)
    FIELD(RST_APU1, CORE3_COLD_RESET, 3, 1)
    FIELD(RST_APU1, CORE2_COLD_RESET, 2, 1)
    FIELD(RST_APU1, CORE1_COLD_RESET, 1, 1)
    FIELD(RST_APU1, CORE0_COLD_RESET, 0, 1)
REG32(RST_APU2, 0x308)
    FIELD(RST_APU2, CLUSTER_COLD_RESET, 9, 1)
    FIELD(RST_APU2, CLUSTER_WARM_RESET, 8, 1)
    FIELD(RST_APU2, CORE3_WARM_RESET, 7, 1)
    FIELD(RST_APU2, CORE2_WARM_RESET, 6, 1)
    FIELD(RST_APU2, CORE1_WARM_RESET, 5, 1)
    FIELD(RST_APU2, CORE0_WARM_RESET, 4, 1)
    FIELD(RST_APU2, CORE3_COLD_RESET, 3, 1)
    FIELD(RST_APU2, CORE2_COLD_RESET, 2, 1)
    FIELD(RST_APU2, CORE1_COLD_RESET, 1, 1)
    FIELD(RST_APU2, CORE0_COLD_RESET, 0, 1)
REG32(RST_APU3, 0x30c)
    FIELD(RST_APU3, CLUSTER_COLD_RESET, 9, 1)
    FIELD(RST_APU3, CLUSTER_WARM_RESET, 8, 1)
    FIELD(RST_APU3, CORE3_WARM_RESET, 7, 1)
    FIELD(RST_APU3, CORE2_WARM_RESET, 6, 1)
    FIELD(RST_APU3, CORE1_WARM_RESET, 5, 1)
    FIELD(RST_APU3, CORE0_WARM_RESET, 4, 1)
    FIELD(RST_APU3, CORE3_COLD_RESET, 3, 1)
    FIELD(RST_APU3, CORE2_COLD_RESET, 2, 1)
    FIELD(RST_APU3, CORE1_COLD_RESET, 1, 1)
    FIELD(RST_APU3, CORE0_COLD_RESET, 0, 1)
REG32(RST_DBG_FPX, 0x310)
    FIELD(RST_DBG_FPX, RESET, 0, 1)
REG32(RST_SYSMON, 0x318)
    FIELD(RST_SYSMON, CFG_RST, 0, 1)
REG32(RST_FMU, 0x31c)
    FIELD(RST_FMU, RESET, 0, 1)
REG32(RST_GIC, 0x320)
    FIELD(RST_GIC, RESET, 0, 1)
REG32(RST_MMU, 0x324)
    FIELD(RST_MMU, GLOBAL_RESET, 15, 1)
    FIELD(RST_MMU, TBU10_RESET, 10, 1)
    FIELD(RST_MMU, TBU9_RESET, 9, 1)
    FIELD(RST_MMU, TBU8_RESET, 8, 1)
    FIELD(RST_MMU, TBU7_RESET, 7, 1)
    FIELD(RST_MMU, TBU6_RESET, 6, 1)
    FIELD(RST_MMU, TBU5_RESET, 5, 1)
    FIELD(RST_MMU, TBU4_RESET, 4, 1)
    FIELD(RST_MMU, TBU3_RESET, 3, 1)
    FIELD(RST_MMU, TBU2_RESET, 2, 1)
    FIELD(RST_MMU, TBU1_RESET, 1, 1)
    FIELD(RST_MMU, TBU0_RESET, 0, 1)
REG32(RST_CMN, 0x328)
    FIELD(RST_CMN, RESET_CGL, 2, 1)
    FIELD(RST_CMN, RESET_CXS, 1, 1)
    FIELD(RST_CMN, RESET, 0, 1)
REG32(RST_FPX_SWDT0, 0x32c)
    FIELD(RST_FPX_SWDT0, RESET, 0, 1)
REG32(RST_FPX_SWDT1, 0x330)
    FIELD(RST_FPX_SWDT1, RESET, 0, 1)
REG32(RST_FPX_SWDT2, 0x334)
    FIELD(RST_FPX_SWDT2, RESET, 0, 1)
REG32(RST_FPX_SWDT3, 0x338)
    FIELD(RST_FPX_SWDT3, RESET, 0, 1)
REG32(RST_TIMESTAMP, 0x33c)
    FIELD(RST_TIMESTAMP, RESET, 0, 1)
REG32(RST_PKI, 0x340)
    FIELD(RST_PKI, RESET, 0, 1)
REG32(RST_CPI, 0x344)
    FIELD(RST_CPI, RESET, 0, 1)

#define PSX_CRF_R_MAX (R_RST_CPI + 1)

typedef struct PSX_CRF {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_ir;

    qemu_irq rst_acpu[16];
    qemu_irq rst_acpu_gic;
    qemu_irq rst_dbg_fpd;
    qemu_irq rst_fpd_swdt[4];
    qemu_irq rst_sysmon_cfg;
    qemu_irq rst_sysmon_seq;

    struct {
        uint32_t cores_per_cluster;
    } cfg;

    bool linux_direct_boot;
    uint32_t regs[PSX_CRF_R_MAX];
    RegisterInfo regs_info[PSX_CRF_R_MAX];
} PSX_CRF;

#define PROPAGATE_GPIO(reg, f, irq) { \
    bool val = ARRAY_FIELD_EX32(s->regs, reg, f); \
    qemu_set_irq(irq, val); \
}

#define PROPAGATE_RST_CLUSTER(s, x) do {                                    \
    int i = x * s->cfg.cores_per_cluster;                                   \
    PROPAGATE_GPIO(RST_APU ## x, CORE0_WARM_RESET, s->rst_acpu[i]);         \
    PROPAGATE_GPIO(RST_APU ## x, CORE1_WARM_RESET, s->rst_acpu[i + 1]);     \
    if (s->cfg.cores_per_cluster >= 4) {                                    \
        PROPAGATE_GPIO(RST_APU ## x, CORE2_WARM_RESET, s->rst_acpu[i + 2]); \
        PROPAGATE_GPIO(RST_APU ## x, CORE3_WARM_RESET, s->rst_acpu[i + 3]); \
    }                                                                       \
} while (0)

static void crf_update_gpios(PSX_CRF *s)
{
    if (!s->linux_direct_boot) {
        PROPAGATE_RST_CLUSTER(s, 0);
        PROPAGATE_RST_CLUSTER(s, 1);
        PROPAGATE_RST_CLUSTER(s, 2);
        PROPAGATE_RST_CLUSTER(s, 3);
        PROPAGATE_GPIO(RST_GIC, RESET, s->rst_acpu_gic);
    }
    PROPAGATE_GPIO(RST_DBG_FPX, RESET, s->rst_dbg_fpd);
    PROPAGATE_GPIO(RST_SYSMON, CFG_RST, s->rst_sysmon_cfg);

    PROPAGATE_GPIO(RST_FPX_SWDT0, RESET, s->rst_fpd_swdt[0]);
    PROPAGATE_GPIO(RST_FPX_SWDT1, RESET, s->rst_fpd_swdt[1]);
    PROPAGATE_GPIO(RST_FPX_SWDT2, RESET, s->rst_fpd_swdt[2]);
    PROPAGATE_GPIO(RST_FPX_SWDT3, RESET, s->rst_fpd_swdt[3]);
}

static void crf_update_gpios_pw(RegisterInfo *reg, uint64_t val64)
{
    PSX_CRF *s = XILINX_PSX_CRF(reg->opaque);
    crf_update_gpios(s);
}

static const RegisterAccessInfo psx_crf_regs_info[] = {
    {   .name = "ERR_CTRL",  .addr = A_ERR_CTRL,
        .reset = 0x1,
    },{ .name = "WPROT",  .addr = A_WPROT,
    },{ .name = "APLL1_CTRL",  .addr = A_APLL1_CTRL,
        .reset = 0x24809,
        .rsvd = 0xf88c00f6,
    },{ .name = "APLL1_CFG",  .addr = A_APLL1_CFG,
        .reset = 0x2000000,
        .rsvd = 0x1801210,
    },{ .name = "APLL2_CTRL",  .addr = A_APLL2_CTRL,
        .reset = 0x24809,
        .rsvd = 0xf88c00f6,
    },{ .name = "APLL2_CFG",  .addr = A_APLL2_CFG,
        .reset = 0x2000000,
        .rsvd = 0x1801210,
    },{ .name = "PLL_STATUS",  .addr = A_PLL_STATUS,
        .reset = 0xc | R_PLL_STATUS_APLL2_LOCK_MASK |
                 R_PLL_STATUS_APLL1_LOCK_MASK,
        .rsvd = 0xf0,
        .ro = 0xf,
    },{ .name = "FPX_TOP_SWITCH_CTRL",  .addr = A_FPX_TOP_SWITCH_CTRL,
        .reset = 0x2000200,
        .rsvd = 0xfdfc00f8,
    },{ .name = "FPX_LSBUS_CTRL",  .addr = A_FPX_LSBUS_CTRL,
        .reset = 0x2000800,
        .rsvd = 0xfdfc00f8,
    },{ .name = "ACPU0_CLK_CTRL",  .addr = A_ACPU0_CLK_CTRL,
        .reset = 0x2000200,
        .rsvd = 0xfdfc00f8,
    },{ .name = "ACPU1_CLK_CTRL",  .addr = A_ACPU1_CLK_CTRL,
        .reset = 0x2000200,
        .rsvd = 0xfdfc00f8,
    },{ .name = "ACPU2_CLK_CTRL",  .addr = A_ACPU2_CLK_CTRL,
        .reset = 0x2000200,
        .rsvd = 0xfdfc00f8,
    },{ .name = "ACPU3_CLK_CTRL",  .addr = A_ACPU3_CLK_CTRL,
        .reset = 0x2000200,
        .rsvd = 0xfdfc00f8,
    },{ .name = "DBG_TRACE_CTRL",  .addr = A_DBG_TRACE_CTRL,
        .reset = 0x500,
        .rsvd = 0xfdfc00f8,
    },{ .name = "DBG_FPX_CTRL",  .addr = A_DBG_FPX_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "PERIPH_CLK_CTRL",  .addr = A_PERIPH_CLK_CTRL,
        .reset = 0x300,
        .rsvd = 0xfffc00f8,
    },{ .name = "SAFETY_CHK",  .addr = A_SAFETY_CHK,
    },{ .name = "RST_APU0",  .addr = A_RST_APU0,
        .reset = 0x3ff,
        .rsvd = 0xfc00,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_APU1",  .addr = A_RST_APU1,
        .reset = 0x3ff,
        .rsvd = 0xfc00,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_APU2",  .addr = A_RST_APU2,
        .reset = 0x3ff,
        .rsvd = 0xfc00,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_APU3",  .addr = A_RST_APU3,
        .reset = 0x3ff,
        .rsvd = 0xfc00,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_DBG_FPX",  .addr = A_RST_DBG_FPX,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_SYSMON",  .addr = A_RST_SYSMON,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_FMU",  .addr = A_RST_FMU,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_GIC",  .addr = A_RST_GIC,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_MMU",  .addr = A_RST_MMU,
        .rsvd = 0x7800,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_CMN",  .addr = A_RST_CMN,
    },{ .name = "RST_FPX_SWDT0",  .addr = A_RST_FPX_SWDT0,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_FPX_SWDT1",  .addr = A_RST_FPX_SWDT1,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_FPX_SWDT2",  .addr = A_RST_FPX_SWDT2,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_FPX_SWDT3",  .addr = A_RST_FPX_SWDT3,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_TIMESTAMP",  .addr = A_RST_TIMESTAMP,
        .reset = 0x1,
        .post_write = crf_update_gpios_pw,
    },{ .name = "RST_PKI",  .addr = A_RST_PKI,
        .reset = 0x1,
    },{ .name = "RST_CPI",  .addr = A_RST_CPI,
        .reset = 0x1,
    }
};

static void psx_crf_reset_enter(Object *obj, ResetType type)
{
    PSX_CRF *s = XILINX_PSX_CRF(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void psx_crf_reset_hold(Object *obj)
{
    PSX_CRF *s = XILINX_PSX_CRF(obj);

    crf_update_gpios(s);
}

static void crf_linux_boot_if_init(ARMLinuxBootIf *obj, bool secure_boot)
{
    PSX_CRF *s = XILINX_PSX_CRF(obj);
    int i;

    s->linux_direct_boot = true;
    for (i = 0; i < ARRAY_SIZE(s->rst_acpu); i++) {
        qemu_set_irq(s->rst_acpu[i], false);
    }
}

static const MemoryRegionOps psx_crf_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void psx_crf_realize(DeviceState *dev, Error **errp)
{
    /* Delete this if you don't need it */
}

static void psx_crf_init(Object *obj)
{
    PSX_CRF *s = XILINX_PSX_CRF(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_PSX_CRF, PSX_CRF_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), psx_crf_regs_info,
                              ARRAY_SIZE(psx_crf_regs_info),
                              s->regs_info, s->regs,
                              &psx_crf_ops,
                              XILINX_PSX_CRF_ERR_DEBUG,
                              PSX_CRF_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_ir);

    qdev_init_gpio_out_named(DEVICE(obj), s->rst_acpu, "rst-acpu", 16);

    qdev_init_gpio_out_named(DEVICE(obj), &s->rst_acpu_gic, "rst-acpu-gic", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->rst_dbg_fpd, "rst-dbg-fpd", 1);
    qdev_init_gpio_out_named(DEVICE(obj), s->rst_fpd_swdt, "rst-fpd-swdt", 4);
    qdev_init_gpio_out_named(DEVICE(obj), &s->rst_sysmon_cfg,
                             "rst-sysmon-cfg", 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->rst_sysmon_seq,
                             "rst-sysmon-seq", 1);
}

static const VMStateDescription vmstate_psx_crf = {
    .name = TYPE_XILINX_PSX_CRF,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PSX_CRF, PSX_CRF_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const FDTGenericGPIOSet crf_gpios[] = {
    {
      .names = &fdt_generic_gpio_name_set_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "rst-acpu", .fdt_index = 0, .range = 16 },
        /* Keep these compatible with versal-crf.  */
        { .name = "rst-acpu-gic", .fdt_index = 16, .range = 1 },
        { .name = "rst-dbg-fpd", .fdt_index = 26, .range = 1 },
        /* 27 Reserved.  */
        { .name = "rst-sysmon-cfg", .fdt_index = 28, .range = 1 },
        { .name = "rst-sysmon-seq", .fdt_index = 29, .range = 1 },
        { .name = "rst-fpd-swdt", .fdt_index = 32, .range = 4 },
        { },
      },
    },
    { },
};

static Property psx_crf_properties[] = {
    DEFINE_PROP_UINT32("cores-per-cluster", PSX_CRF, cfg.cores_per_cluster, 4),
    DEFINE_PROP_END_OF_LIST(),
};

static void psx_crf_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);
    ARMLinuxBootIfClass *albifc = ARM_LINUX_BOOT_IF_CLASS(klass);

    dc->realize = psx_crf_realize;
    dc->vmsd = &vmstate_psx_crf;
    device_class_set_props(dc, psx_crf_properties);

    rc->phases.enter = psx_crf_reset_enter;
    rc->phases.hold = psx_crf_reset_hold;

    fggc->controller_gpios = crf_gpios;
    albifc->arm_linux_init = crf_linux_boot_if_init;
}

static const TypeInfo psx_crf_info = {
    .name          = TYPE_XILINX_PSX_CRF,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PSX_CRF),
    .class_init    = psx_crf_class_init,
    .instance_init = psx_crf_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { TYPE_ARM_LINUX_BOOT_IF },
        { }
    },
};

static void psx_crf_register_types(void)
{
    type_register_static(&psx_crf_info);
}

type_init(psx_crf_register_types)
