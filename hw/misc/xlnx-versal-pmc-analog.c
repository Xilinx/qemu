/*
 * QEMU model of the PMC_ANLG PMC Analog
 *
 * Copyright (c) 2018 Xilinx Inc.
 *
 * Autogenerated by xregqemu.py 2018-08-29.
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
#include "hw/irq.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "xlnx-versal-ams.h"

#ifndef PMC_ANALOG_ERR_DEBUG
#define PMC_ANALOG_ERR_DEBUG 0
#endif

#define TYPE_PMC_ANALOG "xlnx.pmc-analog"

#define PMC_ANALOG(obj) \
     OBJECT_CHECK(PmcAnalog, (obj), TYPE_PMC_ANALOG)

REG32(GD_CTRL, 0x0)
    FIELD(GD_CTRL, GD1_RST_STATUS_REG, 25, 1)
    FIELD(GD_CTRL, GD1_FABRIC_GL_EN, 24, 1)
    FIELD(GD_CTRL, GD1_TEST_GLITCH_SEL, 19, 5)
    FIELD(GD_CTRL, GD1_TEST_GLITCH_GEN, 18, 1)
    FIELD(GD_CTRL, GD1_GL_DET_TEST_MODE, 17, 1)
    FIELD(GD_CTRL, GD1_EN_GLITCH_DET_B, 16, 1)
    FIELD(GD_CTRL, GD0_RST_STATUS_REG, 9, 1)
    FIELD(GD_CTRL, GD0_FABRIC_GL_EN, 8, 1)
    FIELD(GD_CTRL, GD0_TEST_GLITCH_SEL, 3, 5)
    FIELD(GD_CTRL, GD0_TEST_GLITCH_GEN, 2, 1)
    FIELD(GD_CTRL, GD0_GL_DET_TEST_MODE, 1, 1)
    FIELD(GD_CTRL, GD0_EN_GLITCH_DET_B, 0, 1)
REG32(GLITCH_DET_STATUS, 0x4)
    FIELD(GLITCH_DET_STATUS, VCCINT_PMC_1, 1, 1)
    FIELD(GLITCH_DET_STATUS, VCCINT_PMC_0, 0, 1)
REG32(POR_CTRL, 0x8)
    FIELD(POR_CTRL, IRO_SLEEP, 1, 1)
    FIELD(POR_CTRL, CFG_MCLK_OFF, 0, 1)
REG32(VGG_CTRL, 0xc)
    FIELD(VGG_CTRL, TEST_VGG_SEL, 14, 5)
    FIELD(VGG_CTRL, TEST_REF_SEL, 9, 5)
    FIELD(VGG_CTRL, TEST_VGG_VDD_SEL, 7, 2)
    FIELD(VGG_CTRL, TEST_VGG_VDD_EN, 6, 1)
    FIELD(VGG_CTRL, TEST_VGG_EN, 5, 1)
    FIELD(VGG_CTRL, TEST_NEG_SLOPE_VGG, 4, 1)
    FIELD(VGG_CTRL, SW_DIS_VGG_REG, 1, 1)
    FIELD(VGG_CTRL, EN_VGG_CLAMP, 0, 1)
REG32(CFRM_PROBE, 0x10)
    FIELD(CFRM_PROBE, MUX_SELECT, 0, 20)
REG32(PMC_SYSMON, 0x14)
    FIELD(PMC_SYSMON, AMS_I2C_SEL, 0, 1)
REG32(GD_FUSE_CTRL_0, 0x20)
    FIELD(GD_FUSE_CTRL_0, SPARE_SEL, 16, 4)
    FIELD(GD_FUSE_CTRL_0, VCCINT_PMC_VAL_SEL, 12, 3)
    FIELD(GD_FUSE_CTRL_0, DEL_SEL, 8, 4)
    FIELD(GD_FUSE_CTRL_0, COMP_SEL, 4, 2)
    FIELD(GD_FUSE_CTRL_0, USE_REG, 0, 1)
REG32(GD_FUSE_CTRL_1, 0x24)
    FIELD(GD_FUSE_CTRL_1, SPARE_SEL, 16, 4)
    FIELD(GD_FUSE_CTRL_1, VCCINT_PMC_VAL_SEL, 12, 3)
    FIELD(GD_FUSE_CTRL_1, DEL_SEL, 8, 4)
    FIELD(GD_FUSE_CTRL_1, COMP_SEL, 4, 2)
    FIELD(GD_FUSE_CTRL_1, USE_REG, 0, 1)
REG32(CFG_POR_CNT_SKIP, 0x30)
    FIELD(CFG_POR_CNT_SKIP, VAL, 0, 1)
REG32(PMC_ANLG_ISR, 0x40)
    FIELD(PMC_ANLG_ISR, SLVERR, 0, 1)
REG32(PMC_ANLG_IMR, 0x44)
    FIELD(PMC_ANLG_IMR, SLVERR, 0, 1)
REG32(PMC_ANLG_IER, 0x48)
    FIELD(PMC_ANLG_IER, SLVERR, 0, 1)
REG32(PMC_ANLG_IDR, 0x4c)
    FIELD(PMC_ANLG_IDR, SLVERR, 0, 1)
REG32(SLVERR_CTRL, 0x50)
    FIELD(SLVERR_CTRL, ENABLE, 0, 1)
REG32(PMC_ANLG_ECO_0, 0x100)
REG32(PMC_ANLG_ECO_1, 0x104)
REG32(TEST_FPD_ISO_LATCH, 0x200)
    FIELD(TEST_FPD_ISO_LATCH, ENABLE, 0, 1)
REG32(TEST_IOU_MODE_IS_DFT, 0x204)
    FIELD(TEST_IOU_MODE_IS_DFT, DISABLE, 0, 1)
REG32(BNK3_EN_RX, 0x10000)
    FIELD(BNK3_EN_RX, BNK3_EN_RX, 0, 13)
REG32(BNK3_SEL_RX, 0x10004)
    FIELD(BNK3_SEL_RX, BNK3_SEL_RX, 0, 26)
REG32(BNK3_EN_RX_SCHMITT_HYST, 0x10008)
    FIELD(BNK3_EN_RX_SCHMITT_HYST, BNK3_EN_RX_SCHMITT_HYST, 0, 13)
REG32(BNK3_EN_WK_PD, 0x1000c)
    FIELD(BNK3_EN_WK_PD, BNK3_EN_WK_PD, 0, 13)
REG32(BNK3_EN_WK_PU, 0x10010)
    FIELD(BNK3_EN_WK_PU, BNK3_EN_WK_PU, 0, 13)
REG32(BNK3_SEL_DRV, 0x10014)
    FIELD(BNK3_SEL_DRV, BNK3_SEL_DRV, 0, 26)
REG32(BNK3_SEL_SLEW, 0x10018)
    FIELD(BNK3_SEL_SLEW, BNK3_SEL_SLEW, 0, 13)
REG32(BNK3_EN_DFT_OPT_INV, 0x1001c)
    FIELD(BNK3_EN_DFT_OPT_INV, BNK3_EN_DFT_OPT_INV, 0, 13)
REG32(BNK3_EN_PAD2PAD_LOOPBACK, 0x10020)
    FIELD(BNK3_EN_PAD2PAD_LOOPBACK, BNK3_EN_PAD2PAD_LOOPBACK, 0, 13)
REG32(BNK3_RX_SPARE, 0x10024)
    FIELD(BNK3_RX_SPARE, BNK3_RX_SPARE, 0, 26)
REG32(BNK3_TX_SPARE, 0x10028)
    FIELD(BNK3_TX_SPARE, BNK3_TX_SPARE, 0, 26)
REG32(BNK3_SEL_EN1P8, 0x1002c)
    FIELD(BNK3_SEL_EN1P8, BNK3_SEL_EN1P8, 0, 1)
REG32(BNK3_EN_B_POR_DETECT, 0x10030)
    FIELD(BNK3_EN_B_POR_DETECT, BNK3_EN_B_POR_DETECT, 0, 1)
REG32(BNK3_LPF_BYP_POR_DETECT, 0x10034)
    FIELD(BNK3_LPF_BYP_POR_DETECT, BNK3_LPF_BYP_POR_DETECT, 0, 1)
REG32(BNK3_EN_LATCH, 0x10038)
    FIELD(BNK3_EN_LATCH, BNK3_EN_LATCH, 0, 1)
REG32(BNK3_VBG_LPF_BYP_B, 0x1003c)
    FIELD(BNK3_VBG_LPF_BYP_B, BNK3_VBG_LPF_BYP_B, 0, 1)
REG32(BNK3_EN_AMP_B, 0x10040)
    FIELD(BNK3_EN_AMP_B, BNK3_EN_AMP_B, 0, 2)
REG32(BNK3_SPARE_BIAS, 0x10044)
    FIELD(BNK3_SPARE_BIAS, BNK3_SPARE_BIAS, 0, 4)
REG32(BNK3_DRIVER_BIAS, 0x10048)
    FIELD(BNK3_DRIVER_BIAS, BNK3_DRIVER_BIAS, 0, 15)
REG32(BNK3_VMODE, 0x1004c)
    FIELD(BNK3_VMODE, BNK3_VMODE, 0, 1)
REG32(BNK3_SEL_AUX_IO_RX, 0x10050)
    FIELD(BNK3_SEL_AUX_IO_RX, BNK3_SEL_AUX_IO_RX, 0, 13)
REG32(BNK3_EN_TX_HS_MODE, 0x10054)
    FIELD(BNK3_EN_TX_HS_MODE, BNK3_EN_TX_HS_MODE, 0, 13)
REG32(XPD_PRE_LOAD, 0x10200)
REG32(XPD_EXPECTED, 0x10204)
REG32(XPD_CTRL0, 0x10208)
    FIELD(XPD_CTRL0, DELAY_SPARE, 25, 5)
    FIELD(XPD_CTRL0, CMP_SEL, 24, 1)
    FIELD(XPD_CTRL0, DELAY_CELL_TYPE, 19, 5)
    FIELD(XPD_CTRL0, DELAY_VT_TYPE, 17, 2)
    FIELD(XPD_CTRL0, DELAY_VALUE, 6, 11)
    FIELD(XPD_CTRL0, PATH_SEL, 0, 6)
REG32(XPD_CTRL1, 0x1020c)
    FIELD(XPD_CTRL1, CLK_SPARE, 12, 4)
    FIELD(XPD_CTRL1, CLK_PHASE_SEL, 10, 2)
    FIELD(XPD_CTRL1, CLK_VT_TYPE, 8, 2)
    FIELD(XPD_CTRL1, CLK_CELL_TYPE, 6, 2)
    FIELD(XPD_CTRL1, CLK_INSERT_DLY, 2, 4)
    FIELD(XPD_CTRL1, CLK_SEL, 0, 2)
REG32(XPD_CTRL2, 0x10210)
    FIELD(XPD_CTRL2, CTRL_SPARE, 1, 2)
    FIELD(XPD_CTRL2, ENABLE, 0, 1)
REG32(XPD_CTRL3, 0x10214)
    FIELD(XPD_CTRL3, DCYCLE_CNT_VALUE, 3, 12)
    FIELD(XPD_CTRL3, DCYCLE_HIGH_LOW, 2, 1)
    FIELD(XPD_CTRL3, DCYCLE_CNT_CLR, 1, 1)
    FIELD(XPD_CTRL3, DCYCLE_START, 0, 1)
REG32(XPD_SOFT_RST, 0x10218)
    FIELD(XPD_SOFT_RST, CLK0, 0, 1)
REG32(XPD_STAT, 0x1021c)
    FIELD(XPD_STAT, CMP_RESULT, 1, 1)
    FIELD(XPD_STAT, CMP_DONE, 0, 1)
REG32(PMV_CTRL0, 0x10300)
    FIELD(PMV_CTRL0, FLOP_SEL_INTIP, 9, 4)
    FIELD(PMV_CTRL0, LATCH_SEL_INTIP, 5, 4)
    FIELD(PMV_CTRL0, OUTPUT_SEL_INTIP, 1, 4)
    FIELD(PMV_CTRL0, TOGGLE_SEL_INTIP, 0, 1)
REG32(BISR_CACHE_CTRL_0, 0x20000)
    FIELD(BISR_CACHE_CTRL_0, CLR, 4, 1)
    FIELD(BISR_CACHE_CTRL_0, TRIGGER, 0, 1)
REG32(BISR_CACHE_CTRL_1, 0x20004)
    FIELD(BISR_CACHE_CTRL_1, PGEN_0, 0, 1)
REG32(BISR_CACHE_STATUS, 0x20008)
    FIELD(BISR_CACHE_STATUS, PASS, 1, 1)
    FIELD(BISR_CACHE_STATUS, DONE, 0, 1)
REG32(BISR_CACHE_DATA_0, 0x20010)
REG32(BISR_CACHE_DATA_1, 0x20014)
REG32(BISR_TEST_DATA_0, 0x20020)
REG32(BISR_TEST_DATA_1, 0x20024)
REG32(OD_MBIST_RST, 0x20100)
    FIELD(OD_MBIST_RST, LPD_IOU, 6, 1)
    FIELD(OD_MBIST_RST, LPD_RPU, 5, 1)
    FIELD(OD_MBIST_RST, LPD, 4, 1)
    FIELD(OD_MBIST_RST, PMC_IOU, 1, 1)
    FIELD(OD_MBIST_RST, PMC, 0, 1)
REG32(OD_MBIST_PG_EN, 0x20104)
    FIELD(OD_MBIST_PG_EN, LPD_IOU, 6, 1)
    FIELD(OD_MBIST_PG_EN, LPD_RPU, 5, 1)
    FIELD(OD_MBIST_PG_EN, LPD, 4, 1)
    FIELD(OD_MBIST_PG_EN, PMC_IOU, 1, 1)
    FIELD(OD_MBIST_PG_EN, PMC, 0, 1)
REG32(OD_MBIST_SETUP, 0x20108)
    FIELD(OD_MBIST_SETUP, LPD_IOU, 6, 1)
    FIELD(OD_MBIST_SETUP, LPD_RPU, 5, 1)
    FIELD(OD_MBIST_SETUP, LPD, 4, 1)
    FIELD(OD_MBIST_SETUP, PMC_IOU, 1, 1)
    FIELD(OD_MBIST_SETUP, PMC, 0, 1)
REG32(MBIST_MODE, 0x2010c)
    FIELD(MBIST_MODE, PMC_IOU, 1, 1)
REG32(OD_MBIST_DONE, 0x20110)
    FIELD(OD_MBIST_DONE, LPD_IOU, 6, 1)
    FIELD(OD_MBIST_DONE, LPD_RPU, 5, 1)
    FIELD(OD_MBIST_DONE, LPD, 4, 1)
    FIELD(OD_MBIST_DONE, PMC_IOU, 1, 1)
    FIELD(OD_MBIST_DONE, PMC, 0, 1)
REG32(OD_MBIST_GOOD, 0x20114)
    FIELD(OD_MBIST_GOOD, LPD_IOU, 6, 1)
    FIELD(OD_MBIST_GOOD, LPD_RPU, 5, 1)
    FIELD(OD_MBIST_GOOD, LPD, 4, 1)
    FIELD(OD_MBIST_GOOD, PMC_IOU, 1, 1)
    FIELD(OD_MBIST_GOOD, PMC, 0, 1)
REG32(SCAN_CLEAR_TRIGGER, 0x20120)
    FIELD(SCAN_CLEAR_TRIGGER, NOC, 8, 1)
    FIELD(SCAN_CLEAR_TRIGGER, LPD_IOU, 6, 1)
    FIELD(SCAN_CLEAR_TRIGGER, LPD_RPU, 5, 1)
    FIELD(SCAN_CLEAR_TRIGGER, LPD, 4, 1)
REG32(SCAN_CLEAR_LOCK, 0x20124)
    FIELD(SCAN_CLEAR_LOCK, LOCK, 0, 1)
REG32(SCAN_CLEAR_DONE, 0x20128)
    FIELD(SCAN_CLEAR_DONE, LPD_IOU, 6, 1)
    FIELD(SCAN_CLEAR_DONE, LPD_RPU, 5, 1)
    FIELD(SCAN_CLEAR_DONE, LPD, 4, 1)
    FIELD(SCAN_CLEAR_DONE, PMC, 0, 1)
REG32(SCAN_CLEAR_PASS, 0x2012c)
    FIELD(SCAN_CLEAR_PASS, LPD_IOU, 6, 1)
    FIELD(SCAN_CLEAR_PASS, LPD_RPU, 5, 1)
    FIELD(SCAN_CLEAR_PASS, LPD, 4, 1)
    FIELD(SCAN_CLEAR_PASS, PMC, 0, 1)
REG32(LBIST_ENABLE, 0x20200)
    FIELD(LBIST_ENABLE, LPD_RPU, 1, 1)
    FIELD(LBIST_ENABLE, LPD, 0, 1)
REG32(LBIST_RST_N, 0x20204)
    FIELD(LBIST_RST_N, LPD_RPU, 1, 1)
    FIELD(LBIST_RST_N, LPD, 0, 1)
REG32(LBIST_ISOLATION_EN, 0x20208)
    FIELD(LBIST_ISOLATION_EN, LPD_RPU, 1, 1)
    FIELD(LBIST_ISOLATION_EN, LPD, 0, 1)
REG32(LBIST_LOCK, 0x2020c)
    FIELD(LBIST_LOCK, LOCK, 0, 1)
REG32(LBIST_DONE, 0x20210)
    FIELD(LBIST_DONE, LPD_RPU, 1, 1)
    FIELD(LBIST_DONE, LPD, 0, 1)
REG32(LBIST_LPD_MISR_0, 0x20214)
REG32(LBIST_LPD_MISR_1, 0x20218)
REG32(LBIST_LPD_MISR_2, 0x2021c)
REG32(LBIST_LPD_MISR_3, 0x20220)
REG32(LBIST_LPD_MISR_4, 0x20224)
REG32(LBIST_LPD_MISR_5, 0x20228)
REG32(LBIST_LPD_MISR_6, 0x2022c)
REG32(LBIST_LPD_MISR_7, 0x20230)
REG32(LBIST_LPD_MISR_8, 0x20234)
REG32(LBIST_LPD_MISR_9, 0x20238)
REG32(LBIST_LPD_MISR_10, 0x2023c)
REG32(LBIST_LPD_MISR_11, 0x20240)
REG32(LBIST_LPD_MISR_12, 0x20244)
REG32(LBIST_LPD_MISR_13, 0x20248)
REG32(LBIST_LPD_RPU_MISR_0, 0x20250)
REG32(LBIST_LPD_RPU_MISR_1, 0x20254)
REG32(LBIST_LPD_RPU_MISR_2, 0x20258)

#define PMC_ANLG_R_MAX (R_LBIST_LPD_RPU_MISR_2 + 1)

typedef struct PmcAnalog {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_pmc_anlg_imr;
    qemu_irq irq_glitch_detected;

    Object *tamper_sink;

    uint32_t regs[PMC_ANLG_R_MAX];
    RegisterInfo regs_info[PMC_ANLG_R_MAX];
} PmcAnalog;

static void pmc_anlg_imr_update_irq(PmcAnalog *s)
{
    bool pending = s->regs[R_PMC_ANLG_ISR] & ~s->regs[R_PMC_ANLG_IMR];
    qemu_set_irq(s->irq_pmc_anlg_imr, pending);
}

static void pmc_anlg_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    PmcAnalog *s = PMC_ANALOG(reg->opaque);
    pmc_anlg_imr_update_irq(s);
}

static uint64_t pmc_anlg_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    PmcAnalog *s = PMC_ANALOG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_PMC_ANLG_IMR] &= ~val;
    pmc_anlg_imr_update_irq(s);
    return 0;
}

static uint64_t pmc_anlg_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    PmcAnalog *s = PMC_ANALOG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_PMC_ANLG_IMR] |= val;
    pmc_anlg_imr_update_irq(s);
    return 0;
}

#define SCAN_CLEAR_TRIG(dev) \
    if (FIELD_EX32(val, SCAN_CLEAR_TRIGGER, LPD_IOU) &&          \
        !FIELD_EX32(curr_regval, SCAN_CLEAR_TRIGGER, LPD_IOU)) { \
        ARRAY_FIELD_DP32(s->regs, SCAN_CLEAR_DONE, LPD_IOU, 1);  \
        ARRAY_FIELD_DP32(s->regs, SCAN_CLEAR_PASS, LPD_IOU, 1);  \
    }

static uint64_t pmc_anlg_scan_clear_trigger_prew(RegisterInfo *reg,
                                                 uint64_t val64)
{
    PmcAnalog *s = PMC_ANALOG(reg->opaque);
    uint32_t val = val64;
    uint32_t curr_regval;

    if (ARRAY_FIELD_EX32(s->regs, SCAN_CLEAR_LOCK, LOCK)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Attempted to trigger scan clear when "
                      "register is locked.\n");
        return 0;
    }

    /*
     * We're not locked, check to see if the user is setting a
     * scan clear trigger. Scan clears always pass.
     */
    curr_regval = s->regs[R_SCAN_CLEAR_TRIGGER];

    if (FIELD_EX32(val, SCAN_CLEAR_TRIGGER, NOC) &&
        !FIELD_EX32(curr_regval, SCAN_CLEAR_TRIGGER, NOC)) {
        ARRAY_FIELD_DP32(s->regs, SCAN_CLEAR_DONE, PMC, 1);
        ARRAY_FIELD_DP32(s->regs, SCAN_CLEAR_PASS, PMC, 1);
    }

    SCAN_CLEAR_TRIG(LPD);
    SCAN_CLEAR_TRIG(LPD_RPU);
    SCAN_CLEAR_TRIG(LPD_IOU);

    return val;
}

#define MBIST_TRIG(dev)                                         \
    if (FIELD_EX32(val, OD_MBIST_PG_EN, dev) &&                 \
        !FIELD_EX32(curr_regval, OD_MBIST_PG_EN, dev)) {        \
        setup = ARRAY_FIELD_EX32(s->regs, OD_MBIST_SETUP, dev); \
        rst = !ARRAY_FIELD_EX32(s->regs, OD_MBIST_RST, dev);    \
        if (setup && !rst) {                                    \
            ARRAY_FIELD_DP32(s->regs, OD_MBIST_DONE, dev, 1);   \
            ARRAY_FIELD_DP32(s->regs, OD_MBIST_GOOD, dev, 1);   \
        }                                                       \
    }

static uint64_t pmc_anlg_od_mbist_pg_en_prew(RegisterInfo *reg, uint64_t val64)
{
    PmcAnalog *s = PMC_ANALOG(reg->opaque);
    uint32_t val = val64;
    uint32_t curr_regval = s->regs[R_OD_MBIST_PG_EN];
    bool rst;
    bool setup;

    /* Trigger MBIST if we're going from 0 -> 1 */
    MBIST_TRIG(LPD_IOU);
    MBIST_TRIG(LPD_RPU);
    MBIST_TRIG(LPD);
    MBIST_TRIG(PMC_IOU);
    MBIST_TRIG(PMC);

    return val;
}

static void pmc_anlg_clear_gd_status(PmcAnalog *s)
{
    if (ARRAY_FIELD_EX32(s->regs, GD_CTRL, GD1_RST_STATUS_REG)) {
        ARRAY_FIELD_DP32(s->regs, GLITCH_DET_STATUS, VCCINT_PMC_1, 0);
    }
    if (ARRAY_FIELD_EX32(s->regs, GD_CTRL, GD0_RST_STATUS_REG)) {
        ARRAY_FIELD_DP32(s->regs, GLITCH_DET_STATUS, VCCINT_PMC_0, 0);
    }
}

static void pmc_anlg_tamper_out(PmcAnalog *s, uint32_t events)
{
    const char *name = XLNX_AMS_TAMPER_PROP;
    g_autofree Error *err = NULL;

    if (!events || !s->tamper_sink) {
        return;
    }

    object_property_set_uint(s->tamper_sink, name, events, &err);
    if (err) {
        g_autofree char *p_dev = object_get_canonical_path(OBJECT(s));
        g_autofree char *p_qom = object_get_canonical_path(s->tamper_sink);

        warn_report("%s: qom-set %s %s 0x%02x failed: %s", p_dev, p_qom,
                    name, events, error_get_pretty(err));
    }
}

static void pmc_anlg_set_gd_status(PmcAnalog *s, unsigned bits)
{
    uint32_t ctrl = s->regs[R_GD_CTRL];
    uint32_t tamper;
    unsigned n;

    static const struct gd_mask {
        uint32_t status;
        uint32_t dis;
        uint32_t rst;
        uint32_t tamper;
    } gd_mask[] = {
        [0] = { R_GLITCH_DET_STATUS_VCCINT_PMC_0_MASK,
                R_GD_CTRL_GD0_EN_GLITCH_DET_B_MASK,
                R_GD_CTRL_GD0_RST_STATUS_REG_MASK,
                XLNX_AMS_VCCINT_0_GLITCH_MASK },

        [1] = { R_GLITCH_DET_STATUS_VCCINT_PMC_1_MASK,
                R_GD_CTRL_GD1_EN_GLITCH_DET_B_MASK,
                R_GD_CTRL_GD1_RST_STATUS_REG_MASK,
                XLNX_AMS_VCCINT_1_GLITCH_MASK },
    };

    for (tamper = 0, n = 0; n < ARRAY_SIZE(gd_mask); n++) {
        if (!(bits & gd_mask[n].status)) {
            continue;   /* no glitch */
        }
        if (ctrl & gd_mask[n].rst) {
            continue;   /* detector in reset */
        }
        if (ctrl & gd_mask[n].dis) {
            continue;   /* detector disabled */
        }

        s->regs[R_GLITCH_DET_STATUS] |= gd_mask[n].status;
        tamper |= gd_mask[n].tamper;
    }

    if (tamper) {
        /* Both outputs are non-maskable */
        pmc_anlg_tamper_out(s, tamper);
        qemu_irq_pulse(s->irq_glitch_detected);
    }
}

static void pmc_anlg_inject_glitches(PmcAnalog *s)
{
    uint32_t ctrl = s->regs[R_GD_CTRL];
    uint32_t gd;
    unsigned n;

    static const struct test_mask {
        uint32_t gd;
        uint32_t mode;
        uint32_t sel;
        uint32_t gen;
    } test_mask[] = {
        [0] = { R_GLITCH_DET_STATUS_VCCINT_PMC_0_MASK,
                R_GD_CTRL_GD0_TEST_GLITCH_SEL_MASK,
                R_GD_CTRL_GD0_TEST_GLITCH_GEN_MASK,
                R_GD_CTRL_GD0_GL_DET_TEST_MODE_MASK },

        [1] = { R_GLITCH_DET_STATUS_VCCINT_PMC_1_MASK,
                R_GD_CTRL_GD1_TEST_GLITCH_SEL_MASK,
                R_GD_CTRL_GD1_TEST_GLITCH_GEN_MASK,
                R_GD_CTRL_GD1_GL_DET_TEST_MODE_MASK },
    };

    for (gd = 0, n = 0; n < ARRAY_SIZE(test_mask); n++) {
        if (!(ctrl & test_mask[n].gen)) {
            continue;   /* not injecting */
        }
        if (!(ctrl & test_mask[n].mode)) {
            continue;   /* not in test mode */
        }
        if (!(ctrl & test_mask[n].sel)) {
            continue;   /* test config selected */
        }

        gd |= test_mask[n].gd;
    }

    pmc_anlg_set_gd_status(s, gd);
}

static void pmc_anlg_gd_ctrl_postw(RegisterInfo *reg, uint64_t val64)
{
    PmcAnalog *s = PMC_ANALOG(reg->opaque);

    pmc_anlg_clear_gd_status(s);
    pmc_anlg_inject_glitches(s);
}

static const RegisterAccessInfo pmc_anlg_regs_info[] = {
    {   .name = "GD_CTRL",  .addr = A_GD_CTRL,
        .rsvd = 0xfc00fc00,
        .post_write = pmc_anlg_gd_ctrl_postw,
    },{ .name = "GLITCH_DET_STATUS",  .addr = A_GLITCH_DET_STATUS,
        .rsvd = 0xfffffffc,
        .ro = 0x3,
    },{ .name = "POR_CTRL",  .addr = A_POR_CTRL,
        .rsvd = 0xfffffffc,
    },{ .name = "VGG_CTRL",  .addr = A_VGG_CTRL,
        .rsvd = 0xfff8000c,
    },{ .name = "CFRM_PROBE",  .addr = A_CFRM_PROBE,
        .rsvd = 0xfff00000,
    },{ .name = "PMC_SYSMON",  .addr = A_PMC_SYSMON,
        .rsvd = 0xfffffffe,
    },{ .name = "GD_FUSE_CTRL_0",  .addr = A_GD_FUSE_CTRL_0,
        .rsvd = 0xfff080ce,
    },{ .name = "GD_FUSE_CTRL_1",  .addr = A_GD_FUSE_CTRL_1,
        .rsvd = 0xfff080ce,
    },{ .name = "CFG_POR_CNT_SKIP",  .addr = A_CFG_POR_CNT_SKIP,
    },{ .name = "PMC_ANLG_ISR",  .addr = A_PMC_ANLG_ISR,
        .w1c = 0x1,
        .post_write = pmc_anlg_isr_postw,
    },{ .name = "PMC_ANLG_IMR",  .addr = A_PMC_ANLG_IMR,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "PMC_ANLG_IER",  .addr = A_PMC_ANLG_IER,
        .pre_write = pmc_anlg_ier_prew,
    },{ .name = "PMC_ANLG_IDR",  .addr = A_PMC_ANLG_IDR,
        .pre_write = pmc_anlg_idr_prew,
    },{ .name = "SLVERR_CTRL",  .addr = A_SLVERR_CTRL,
    },{ .name = "PMC_ANLG_ECO_0",  .addr = A_PMC_ANLG_ECO_0,
    },{ .name = "PMC_ANLG_ECO_1",  .addr = A_PMC_ANLG_ECO_1,
    },{ .name = "TEST_FPD_ISO_LATCH",  .addr = A_TEST_FPD_ISO_LATCH,
    },{ .name = "TEST_IOU_MODE_IS_DFT",  .addr = A_TEST_IOU_MODE_IS_DFT,
    },{ .name = "BNK3_EN_RX",  .addr = A_BNK3_EN_RX,
        .reset = 0x1fff,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_SEL_RX",  .addr = A_BNK3_SEL_RX,
        .reset = 0x3ffffff,
        .rsvd = 0xfc000000,
    },{ .name = "BNK3_EN_RX_SCHMITT_HYST",  .addr = A_BNK3_EN_RX_SCHMITT_HYST,
        .reset = 0x1fff,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_EN_WK_PD",  .addr = A_BNK3_EN_WK_PD,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_EN_WK_PU",  .addr = A_BNK3_EN_WK_PU,
        .reset = 0x1fff,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_SEL_DRV",  .addr = A_BNK3_SEL_DRV,
        .reset = 0x3ffffff,
        .rsvd = 0xfc000000,
    },{ .name = "BNK3_SEL_SLEW",  .addr = A_BNK3_SEL_SLEW,
        .reset = 0x1fff,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_EN_DFT_OPT_INV",  .addr = A_BNK3_EN_DFT_OPT_INV,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_EN_PAD2PAD_LOOPBACK",  .addr = A_BNK3_EN_PAD2PAD_LOOPBACK,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_RX_SPARE",  .addr = A_BNK3_RX_SPARE,
        .rsvd = 0xfc000000,
    },{ .name = "BNK3_TX_SPARE",  .addr = A_BNK3_TX_SPARE,
        .rsvd = 0xfc000000,
    },{ .name = "BNK3_SEL_EN1P8",  .addr = A_BNK3_SEL_EN1P8,
        .rsvd = 0xfffffffe,
    },{ .name = "BNK3_EN_B_POR_DETECT",  .addr = A_BNK3_EN_B_POR_DETECT,
        .rsvd = 0xfffffffe,
    },{ .name = "BNK3_LPF_BYP_POR_DETECT",  .addr = A_BNK3_LPF_BYP_POR_DETECT,
        .reset = 0x1,
        .rsvd = 0xfffffffe,
    },{ .name = "BNK3_EN_LATCH",  .addr = A_BNK3_EN_LATCH,
        .rsvd = 0xfffffffe,
    },{ .name = "BNK3_VBG_LPF_BYP_B",  .addr = A_BNK3_VBG_LPF_BYP_B,
        .reset = 0x1,
        .rsvd = 0xfffffffe,
    },{ .name = "BNK3_EN_AMP_B",  .addr = A_BNK3_EN_AMP_B,
        .rsvd = 0xfffffffc,
    },{ .name = "BNK3_SPARE_BIAS",  .addr = A_BNK3_SPARE_BIAS,
        .rsvd = 0xfffffff0,
    },{ .name = "BNK3_DRIVER_BIAS",  .addr = A_BNK3_DRIVER_BIAS,
        .reset = 0x11,
        .rsvd = 0xffff8000,
    },{ .name = "BNK3_VMODE",  .addr = A_BNK3_VMODE,
        .rsvd = 0xfffffffe,
        .ro = 0x1,
    },{ .name = "BNK3_SEL_AUX_IO_RX",  .addr = A_BNK3_SEL_AUX_IO_RX,
        .rsvd = 0xffffe000,
    },{ .name = "BNK3_EN_TX_HS_MODE",  .addr = A_BNK3_EN_TX_HS_MODE,
        .rsvd = 0xffffe000,
    },{ .name = "XPD_PRE_LOAD",  .addr = A_XPD_PRE_LOAD,
    },{ .name = "XPD_EXPECTED",  .addr = A_XPD_EXPECTED,
    },{ .name = "XPD_CTRL0",  .addr = A_XPD_CTRL0,
        .rsvd = 0xc0000000,
    },{ .name = "XPD_CTRL1",  .addr = A_XPD_CTRL1,
        .rsvd = 0xffff0000,
    },{ .name = "XPD_CTRL2",  .addr = A_XPD_CTRL2,
        .rsvd = 0xfffffff8,
    },{ .name = "XPD_CTRL3",  .addr = A_XPD_CTRL3,
        .rsvd = 0xffff8000,
        .ro = 0x7ff8,
    },{ .name = "XPD_SOFT_RST",  .addr = A_XPD_SOFT_RST,
        .rsvd = 0xfffffffe,
    },{ .name = "XPD_STAT",  .addr = A_XPD_STAT,
        .reset = R_XPD_STAT_CMP_DONE_MASK | R_XPD_STAT_CMP_RESULT_MASK,
        .rsvd = 0xfffffffc,
        .ro = 0x3,
    },{ .name = "PMV_CTRL0",  .addr = A_PMV_CTRL0,
        .rsvd = 0xffffe000,
    },{ .name = "BISR_CACHE_CTRL_0",  .addr = A_BISR_CACHE_CTRL_0,
        .rsvd = 0xe,
    },{ .name = "BISR_CACHE_CTRL_1",  .addr = A_BISR_CACHE_CTRL_1,
    },{ .name = "BISR_CACHE_STATUS",  .addr = A_BISR_CACHE_STATUS,
        .reset = R_BISR_CACHE_STATUS_DONE_MASK
                 | R_BISR_CACHE_STATUS_PASS_MASK,
        .ro = 0x3,
    },{ .name = "BISR_CACHE_DATA_0",  .addr = A_BISR_CACHE_DATA_0,
    },{ .name = "BISR_CACHE_DATA_1",  .addr = A_BISR_CACHE_DATA_1,
    },{ .name = "BISR_TEST_DATA_0",  .addr = A_BISR_TEST_DATA_0,
        .ro = 0xffffffff,
    },{ .name = "BISR_TEST_DATA_1",  .addr = A_BISR_TEST_DATA_1,
        .ro = 0xffffffff,
    },{ .name = "OD_MBIST_RST",  .addr = A_OD_MBIST_RST,
        .rsvd = 0xc,
    },{ .name = "OD_MBIST_PG_EN",  .addr = A_OD_MBIST_PG_EN,
        .rsvd = 0xc,
        .pre_write = pmc_anlg_od_mbist_pg_en_prew,
    },{ .name = "OD_MBIST_SETUP",  .addr = A_OD_MBIST_SETUP,
        .rsvd = 0xc,
    },{ .name = "MBIST_MODE",  .addr = A_MBIST_MODE,
        .rsvd = 0xfffffffd,
    },{ .name = "OD_MBIST_DONE",  .addr = A_OD_MBIST_DONE,
        .rsvd = 0xc,
        .ro = 0x7f,
    },{ .name = "OD_MBIST_GOOD",  .addr = A_OD_MBIST_GOOD,
        .rsvd = 0xc,
        .ro = 0x7f,
    },{ .name = "SCAN_CLEAR_TRIGGER",  .addr = A_SCAN_CLEAR_TRIGGER,
        .rsvd = 0x8f,
        .pre_write = pmc_anlg_scan_clear_trigger_prew,
    },{ .name = "SCAN_CLEAR_LOCK",  .addr = A_SCAN_CLEAR_LOCK,
    },{ .name = "SCAN_CLEAR_DONE",  .addr = A_SCAN_CLEAR_DONE,
        .reset = R_SCAN_CLEAR_DONE_LPD_IOU_MASK
                 | R_SCAN_CLEAR_DONE_LPD_RPU_MASK \
                 | R_SCAN_CLEAR_DONE_LPD_MASK \
                 | R_SCAN_CLEAR_DONE_PMC_MASK,
        .rsvd = 0x8e,
        .ro = 0xff,
    },{ .name = "SCAN_CLEAR_PASS",  .addr = A_SCAN_CLEAR_PASS,
        .reset = R_SCAN_CLEAR_PASS_LPD_IOU_MASK
                 | R_SCAN_CLEAR_PASS_LPD_RPU_MASK \
                 | R_SCAN_CLEAR_PASS_LPD_MASK \
                 | R_SCAN_CLEAR_PASS_PMC_MASK,
        .rsvd = 0x8e,
        .ro = 0xff,
    },{ .name = "LBIST_ENABLE",  .addr = A_LBIST_ENABLE,
    },{ .name = "LBIST_RST_N",  .addr = A_LBIST_RST_N,
    },{ .name = "LBIST_ISOLATION_EN",  .addr = A_LBIST_ISOLATION_EN,
    },{ .name = "LBIST_LOCK",  .addr = A_LBIST_LOCK,
    },{ .name = "LBIST_DONE",  .addr = A_LBIST_DONE,
        .reset = R_LBIST_DONE_LPD_RPU_MASK \
                 | R_LBIST_DONE_LPD_MASK,
        .ro = 0x3,
    },{ .name = "LBIST_LPD_MISR_0",  .addr = A_LBIST_LPD_MISR_0,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_1",  .addr = A_LBIST_LPD_MISR_1,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_2",  .addr = A_LBIST_LPD_MISR_2,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_3",  .addr = A_LBIST_LPD_MISR_3,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_4",  .addr = A_LBIST_LPD_MISR_4,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_5",  .addr = A_LBIST_LPD_MISR_5,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_6",  .addr = A_LBIST_LPD_MISR_6,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_7",  .addr = A_LBIST_LPD_MISR_7,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_8",  .addr = A_LBIST_LPD_MISR_8,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_9",  .addr = A_LBIST_LPD_MISR_9,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_10",  .addr = A_LBIST_LPD_MISR_10,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_11",  .addr = A_LBIST_LPD_MISR_11,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_12",  .addr = A_LBIST_LPD_MISR_12,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_MISR_13",  .addr = A_LBIST_LPD_MISR_13,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_RPU_MISR_0",  .addr = A_LBIST_LPD_RPU_MISR_0,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_RPU_MISR_1",  .addr = A_LBIST_LPD_RPU_MISR_1,
        .ro = 0xffffffff,
    },{ .name = "LBIST_LPD_RPU_MISR_2",  .addr = A_LBIST_LPD_RPU_MISR_2,
        .ro = 0xffffffff,
    }
};

static void pmc_anlg_reset(DeviceState *dev)
{
    PmcAnalog *s = PMC_ANALOG(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    pmc_anlg_imr_update_irq(s);
}

static const MemoryRegionOps pmc_anlg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pmc_anlg_realize(DeviceState *dev, Error **errp)
{
    /* Delete this if you don't need it */
}

static void pmc_anlg_init(Object *obj)
{
    PmcAnalog *s = PMC_ANALOG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_PMC_ANALOG, PMC_ANLG_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), pmc_anlg_regs_info,
                              ARRAY_SIZE(pmc_anlg_regs_info),
                              s->regs_info, s->regs,
                              &pmc_anlg_ops,
                              PMC_ANALOG_ERR_DEBUG,
                              PMC_ANLG_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_out(DEVICE(obj), &s->irq_glitch_detected, 1);
    qdev_init_gpio_out(DEVICE(obj), &s->irq_pmc_anlg_imr, 1);
}

static Property pmc_anlg_properties[] = {
    DEFINE_PROP_LINK("tamper-sink", PmcAnalog, tamper_sink,
                     TYPE_OBJECT, Object *),

    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_pmc_anlg = {
    .name = TYPE_PMC_ANALOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, PmcAnalog, PMC_ANLG_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void pmc_anlg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = pmc_anlg_reset;
    dc->realize = pmc_anlg_realize;
    dc->vmsd = &vmstate_pmc_anlg;

    device_class_set_props(dc, pmc_anlg_properties);
}

static const TypeInfo pmc_anlg_info = {
    .name          = TYPE_PMC_ANALOG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PmcAnalog),
    .class_init    = pmc_anlg_class_init,
    .instance_init = pmc_anlg_init,
};

static void pmc_anlg_register_types(void)
{
    type_register_static(&pmc_anlg_info);
}

type_init(pmc_anlg_register_types)
