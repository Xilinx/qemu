/*
 * QEMU model of Xilinx CSU Core Functionality
 *
 * For the most part, a dummy device model.
 *
 * Copyright (c) 2013 Peter Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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

#include "hw/sysbus.h"
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"

#ifndef XILINX_CSU_CORE_ERR_DEBUG
#define XILINX_CSU_CORE_ERR_DEBUG 0
#endif

#define TYPE_XILINX_CSU_CORE "xlnx.zynqmp-csu-core"

#define XILINX_CSU_CORE(obj) \
     OBJECT_CHECK(CSU, (obj), TYPE_XILINX_CSU_CORE)

#define R_IXR_APB_SLVERR    (1 << 31)
#define R_IXR_TMR_FATAL     (1 << 0)
#define R_IXR_RSVD          (0x7ffffffe)

#define XFSBL_PLATFORM_QEMU  0X00003000U
#define QEMU_IDCODE          0x4600093

REG32(CSU_STATUS, 0x0)
    FIELD(CSU_STATUS, BOOT_ENC, 1, 1)
    FIELD(CSU_STATUS, BOOT_AUTH, 1, 0)
REG32(CSU_CTRL, 0x4)
    FIELD(CSU_CTRL, SLVERR_ENABLE, 1, 4)
    FIELD(CSU_CTRL, CSU_CLK_SEL, 1, 0)
REG32(CSU_SSS_CFG, 0x8)
    FIELD(CSU_SSS_CFG, PSTP_SSS, 4, 16)
    FIELD(CSU_SSS_CFG, SHA_SSS, 4, 12)
    FIELD(CSU_SSS_CFG, AES_SSS, 4, 8)
    FIELD(CSU_SSS_CFG, DMA_SSS, 4, 4)
    FIELD(CSU_SSS_CFG, PCAP_SSS, 4, 0)
REG32(CSU_DMA_RESET, 0xc)
    FIELD(CSU_DMA_RESET, RESET, 1, 0)
REG32(CSU_MULTI_BOOT, 0x10)
REG32(CSU_TAMPER_TRIG, 0x14)
    FIELD(CSU_TAMPER_TRIG, TAMPER, 1, 0)
REG32(CSU_FT_STATUS, 0x18)
    FIELD(CSU_FT_STATUS, R_UE, 1, 31)
    FIELD(CSU_FT_STATUS, R_VOTER_ERROR, 1, 30)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_23, 1, 29)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_13, 1, 28)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_12, 1, 27)
    FIELD(CSU_FT_STATUS, R_MISMATCH_23_A, 1, 26)
    FIELD(CSU_FT_STATUS, R_MISMATCH_13_A, 1, 25)
    FIELD(CSU_FT_STATUS, R_MISMATCH_12_A, 1, 24)
    FIELD(CSU_FT_STATUS, R_FT_ST_MISMATCH, 1, 23)
    FIELD(CSU_FT_STATUS, R_CPU_ID_MISMATCH, 1, 22)
    FIELD(CSU_FT_STATUS, R_SLEEP_RESET, 1, 19)
    FIELD(CSU_FT_STATUS, R_MISMATCH_23_B, 1, 18)
    FIELD(CSU_FT_STATUS, R_MISMATCH_13_B, 1, 17)
    FIELD(CSU_FT_STATUS, R_MISMATCH_12_B, 1, 16)
    FIELD(CSU_FT_STATUS, N_UE, 1, 15)
    FIELD(CSU_FT_STATUS, N_VOTER_ERROR, 1, 14)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_23, 1, 13)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_13, 1, 12)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_12, 1, 11)
    FIELD(CSU_FT_STATUS, N_MISMATCH_23_A, 1, 10)
    FIELD(CSU_FT_STATUS, N_MISMATCH_13_A, 1, 9)
    FIELD(CSU_FT_STATUS, N_MISMATCH_12_A, 1, 8)
    FIELD(CSU_FT_STATUS, N_FT_ST_MISMATCH, 1, 7)
    FIELD(CSU_FT_STATUS, N_CPU_ID_MISMATCH, 1, 6)
    FIELD(CSU_FT_STATUS, N_SLEEP_RESET, 1, 3)
    FIELD(CSU_FT_STATUS, N_MISMATCH_23_B, 1, 2)
    FIELD(CSU_FT_STATUS, N_MISMATCH_13_B, 1, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_12_B, 1, 0)
REG32(CSU_ISR, 0x20)
    FIELD(CSU_ISR, CSU_PL_ISO, 1, 15)
    FIELD(CSU_ISR, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_ISR, TAMPER, 1, 13)
    FIELD(CSU_ISR, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_ISR, APB_SLVERR, 1, 11)
    FIELD(CSU_ISR, TMR_FATAL, 1, 10)
    FIELD(CSU_ISR, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_ISR, AES_ERROR, 1, 8)
    FIELD(CSU_ISR, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_ISR, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_ISR, PL_POR_B, 1, 5)
    FIELD(CSU_ISR, PL_INIT, 1, 4)
    FIELD(CSU_ISR, PL_DONE, 1, 3)
    FIELD(CSU_ISR, SHA_DONE, 1, 2)
    FIELD(CSU_ISR, RSA_DONE, 1, 1)
    FIELD(CSU_ISR, AES_DONE, 1, 0)
REG32(CSU_IMR, 0x24)
    FIELD(CSU_IMR, CSU_PL_ISO, 1, 15)
    FIELD(CSU_IMR, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_IMR, TAMPER, 1, 13)
    FIELD(CSU_IMR, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_IMR, APB_SLVERR, 1, 11)
    FIELD(CSU_IMR, TMR_FATAL, 1, 10)
    FIELD(CSU_IMR, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_IMR, AES_ERROR, 1, 8)
    FIELD(CSU_IMR, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_IMR, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_IMR, PL_POR_B, 1, 5)
    FIELD(CSU_IMR, PL_INIT, 1, 4)
    FIELD(CSU_IMR, PL_DONE, 1, 3)
    FIELD(CSU_IMR, SHA_DONE, 1, 2)
    FIELD(CSU_IMR, RSA_DONE, 1, 1)
    FIELD(CSU_IMR, AES_DONE, 1, 0)
REG32(CSU_IER, 0x28)
    FIELD(CSU_IER, CSU_PL_ISO, 1, 15)
    FIELD(CSU_IER, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_IER, TAMPER, 1, 13)
    FIELD(CSU_IER, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_IER, APB_SLVERR, 1, 11)
    FIELD(CSU_IER, TMR_FATAL, 1, 10)
    FIELD(CSU_IER, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_IER, AES_ERROR, 1, 8)
    FIELD(CSU_IER, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_IER, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_IER, PL_POR_B, 1, 5)
    FIELD(CSU_IER, PL_INIT, 1, 4)
    FIELD(CSU_IER, PL_DONE, 1, 3)
    FIELD(CSU_IER, SHA_DONE, 1, 2)
    FIELD(CSU_IER, RSA_DONE, 1, 1)
    FIELD(CSU_IER, AES_DONE, 1, 0)
REG32(CSU_IDR, 0x2c)
    FIELD(CSU_IDR, CSU_PL_ISO, 1, 15)
    FIELD(CSU_IDR, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_IDR, TAMPER, 1, 13)
    FIELD(CSU_IDR, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_IDR, APB_SLVERR, 1, 11)
    FIELD(CSU_IDR, TMR_FATAL, 1, 10)
    FIELD(CSU_IDR, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_IDR, AES_ERROR, 1, 8)
    FIELD(CSU_IDR, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_IDR, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_IDR, PL_POR_B, 1, 5)
    FIELD(CSU_IDR, PL_INIT, 1, 4)
    FIELD(CSU_IDR, PL_DONE, 1, 3)
    FIELD(CSU_IDR, SHA_DONE, 1, 2)
    FIELD(CSU_IDR, RSA_DONE, 1, 1)
    FIELD(CSU_IDR, AES_DONE, 1, 0)
REG32(JTAG_CHAIN_CFG, 0x30)
    FIELD(JTAG_CHAIN_CFG, SSSS_LINK_ARM_DAP, 1, 1)
    FIELD(JTAG_CHAIN_CFG, SSSS_LINK_PL_TAP, 1, 0)
REG32(JTAG_CHAIN_STATUS, 0x34)
    FIELD(JTAG_CHAIN_STATUS, ARM_DAP, 1, 1)
    FIELD(JTAG_CHAIN_STATUS, PL_TAP, 1, 0)
REG32(JTAG_SEC, 0x38)
    FIELD(JTAG_SEC, SSSS_DDRPHY_SEC, 3, 12)
    FIELD(JTAG_SEC, SSSS_PLTAP_EN, 3, 9)
    FIELD(JTAG_SEC, SSSS_PMU_SEC, 3, 6)
    FIELD(JTAG_SEC, SSSS_PLTAP_SEC, 3, 3)
    FIELD(JTAG_SEC, SSSS_DAP_SEC, 3, 0)
REG32(JTAG_DAP_CFG, 0x3c)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_SPNIDEN, 1, 7)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_SPIDEN, 1, 6)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_NIDEN, 1, 5)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_DBGEN, 1, 4)
    FIELD(JTAG_DAP_CFG, SSSS_APU_SPNIDEN, 1, 3)
    FIELD(JTAG_DAP_CFG, SSSS_APU_SPIDEN, 1, 2)
    FIELD(JTAG_DAP_CFG, SSSS_APU_NIDEN, 1, 1)
    FIELD(JTAG_DAP_CFG, SSSS_APU_DBGEN, 1, 0)
REG32(IDCODE, 0x40)
REG32(VERSION, 0x44)
    FIELD(VERSION, PLATFORM_VERSION, 4, 16)
    FIELD(VERSION, PLATFORM, 4, 12)
    FIELD(VERSION, RTL_VERSION, 8, 4)
    FIELD(VERSION, PS_VERSION, 4, 0)
REG32(CSU_ROM_DIGEST_0, 0x50)
REG32(SCRATCH_0, 0x100)
REG32(SCRATCH_1, 0x104)
REG32(SCRATCH_2, 0x108)
REG32(SCRATCH_3, 0x10c)
REG32(SCRATCH_4, 0x110)
REG32(SCRATCH_5, 0x114)
REG32(SCRATCH_6, 0x118)
REG32(SCRATCH_7, 0x11c)
REG32(AES_STATUS, 0x1000)
    FIELD(AES_STATUS, OKR_ZEROED, 1, 11)
    FIELD(AES_STATUS, BOOT_ZEROED, 1, 10)
    FIELD(AES_STATUS, KUP_ZEROED, 1, 9)
    FIELD(AES_STATUS, AES_KEY_ZEROED, 1, 8)
    FIELD(AES_STATUS, BLACK_KEY_DONE, 1, 5)
    FIELD(AES_STATUS, KEY_INIT_DONE, 1, 4)
    FIELD(AES_STATUS, GCM_TAG_PASS, 1, 3)
    FIELD(AES_STATUS, DONE, 1, 2)
    FIELD(AES_STATUS, READY, 1, 1)
    FIELD(AES_STATUS, BUSY, 1, 0)
REG32(AES_KEY_SRC, 0x1004)
    FIELD(AES_KEY_SRC, KEY_SRC, 4, 0)
REG32(AES_KEY_LOAD, 0x1008)
    FIELD(AES_KEY_LOAD, KEY_LOAD, 1, 0)
REG32(AES_START_MSG, 0x100c)
    FIELD(AES_START_MSG, START_MSG, 1, 0)
REG32(AES_RESET, 0x1010)
    FIELD(AES_RESET, RESET, 1, 0)
REG32(AES_KEY_CLEAR, 0x1014)
    FIELD(AES_KEY_CLEAR, AES_OKR_ZERO, 1, 3)
    FIELD(AES_KEY_CLEAR, AES_BOOT_ZERO, 1, 2)
    FIELD(AES_KEY_CLEAR, AES_KUP_ZERO, 1, 1)
    FIELD(AES_KEY_CLEAR, AES_KEY_ZERO, 1, 0)
REG32(AES_CFG, 0x1018)
    FIELD(AES_CFG, ENCRYPT_DECRYPT_N, 1, 0)
REG32(AES_KUP_WR, 0x101c)
    FIELD(AES_KUP_WR, IV_WRITE, 1, 1)
    FIELD(AES_KUP_WR, KUP_WRITE, 1, 0)
REG32(AES_KUP_0, 0x1020)
REG32(AES_KUP_1, 0x1024)
REG32(AES_KUP_2, 0x1028)
REG32(AES_KUP_3, 0x102c)
REG32(AES_KUP_4, 0x1030)
REG32(AES_KUP_5, 0x1034)
REG32(AES_KUP_6, 0x1038)
REG32(AES_KUP_7, 0x103c)
REG32(AES_IV_0, 0x1040)
REG32(AES_IV_1, 0x1044)
REG32(AES_IV_2, 0x1048)
REG32(AES_IV_3, 0x104c)
REG32(SHA_START, 0x2000)
    FIELD(SHA_START, START_MSG, 1, 0)
REG32(SHA_RESET, 0x2004)
    FIELD(SHA_RESET, RESET, 1, 0)
REG32(SHA_DONE, 0x2008)
    FIELD(SHA_DONE, SHA_DONE, 1, 0)
REG32(SHA_DIGEST_0, 0x2010)
REG32(SHA_DIGEST_1, 0x2014)
REG32(SHA_DIGEST_2, 0x2018)
REG32(SHA_DIGEST_3, 0x201c)
REG32(SHA_DIGEST_4, 0x2020)
REG32(SHA_DIGEST_5, 0x2024)
REG32(SHA_DIGEST_6, 0x2028)
REG32(SHA_DIGEST_7, 0x202c)
REG32(SHA_DIGEST_8, 0x2030)
REG32(SHA_DIGEST_9, 0x2034)
REG32(SHA_DIGEST_10, 0x2038)
REG32(SHA_DIGEST_11, 0x203c)
REG32(PCAP_PROG, 0x3000)
    FIELD(PCAP_PROG, PCFG_PROG_B, 1, 0)
REG32(PCAP_RDWR, 0x3004)
    FIELD(PCAP_RDWR, PCAP_RDWR_B, 1, 0)
REG32(PCAP_CTRL, 0x3008)
    FIELD(PCAP_CTRL, PCFG_GSR, 1, 3)
    FIELD(PCAP_CTRL, PCFG_GTS, 1, 2)
    FIELD(PCAP_CTRL, PCFG_POR_CNT_4K, 1, 1)
    FIELD(PCAP_CTRL, PCAP_PR, 1, 0)
REG32(PCAP_RESET, 0x300c)
    FIELD(PCAP_RESET, RESET, 1, 0)
REG32(PCAP_STATUS, 0x3010)
    FIELD(PCAP_STATUS, PCFG_FUSE_PL_DIS, 1, 31)
    FIELD(PCAP_STATUS, PCFG_PL_CFG_USED, 1, 30)
    FIELD(PCAP_STATUS, PCFG_IS_ZYNQ, 1, 29)
    FIELD(PCAP_STATUS, PCFG_GWE, 1, 13)
    FIELD(PCAP_STATUS, PCFG_MCAP_MODE, 1, 12)
    FIELD(PCAP_STATUS, PL_GTS_USR_B, 1, 11)
    FIELD(PCAP_STATUS, PL_GTS_CFG_B, 1, 10)
    FIELD(PCAP_STATUS, PL_GPWRDWN_B, 1, 9)
    FIELD(PCAP_STATUS, PL_GHIGH_B, 1, 8)
    FIELD(PCAP_STATUS, PL_FST_CFG, 1, 7)
    FIELD(PCAP_STATUS, PL_CFG_RESET_B, 1, 6)
    FIELD(PCAP_STATUS, PL_SEU_ERROR, 1, 5)
    FIELD(PCAP_STATUS, PL_EOS, 1, 4)
    FIELD(PCAP_STATUS, PL_DONE, 1, 3)
    FIELD(PCAP_STATUS, PL_INIT, 1, 2)
    FIELD(PCAP_STATUS, PCAP_RD_IDLE, 1, 1)
    FIELD(PCAP_STATUS, PCAP_WR_IDLE, 1, 0)
REG32(PUF_CMD, 0x4000)
    FIELD(PUF_CMD, CMD, 4, 0)
REG32(PUF_CFG0, 0x4004)
REG32(PUF_CFG1, 0x4008)
REG32(PUF_SHUT, 0x400c)
    FIELD(PUF_SHUT, SOSET, 8, 24)
    FIELD(PUF_SHUT, SOPEN, 24, 0)
REG32(PUF_STATUS, 0x4010)
    FIELD(PUF_STATUS, OVERFLOW, 2, 28)
    FIELD(PUF_STATUS, AUX, 24, 4)
    FIELD(PUF_STATUS, KEY_RDY, 1, 3)
    FIELD(PUF_STATUS, KEY_ZERO, 1, 1)
    FIELD(PUF_STATUS, SYN_WRD_RDY, 1, 0)
REG32(PUF_DBG, 0x4014)
REG32(PUF_WORD, 0x4018)
REG32(PUF_TM_STATUS, 0x4804)
    FIELD(PUF_TM_STATUS, DN, 1, 0)
REG32(PUF_TM_UL, 0x4808)
    FIELD(PUF_TM_UL, UL, 18, 0)
REG32(PUF_TM_LL, 0x480c)
    FIELD(PUF_TM_LL, LL, 18, 0)
REG32(PUF_TM_SW, 0x4810)
    FIELD(PUF_TM_SW, OFF, 8, 24)
    FIELD(PUF_TM_SW, SW, 24, 0)
REG32(PUF_TM_TR, 0x4814)
    FIELD(PUF_TM_TR, US, 2, 24)
    FIELD(PUF_TM_TR, ER, 8, 16)
    FIELD(PUF_TM_TR, FRR, 1, 0)
REG32(TAMPER_STATUS, 0x5000)
    FIELD(TAMPER_STATUS, TAMPER_13, 1, 13)
    FIELD(TAMPER_STATUS, TAMPER_12, 1, 12)
    FIELD(TAMPER_STATUS, TAMPER_11, 1, 11)
    FIELD(TAMPER_STATUS, TAMPER_10, 1, 10)
    FIELD(TAMPER_STATUS, TAMPER_9, 1, 9)
    FIELD(TAMPER_STATUS, TAMPER_8, 1, 8)
    FIELD(TAMPER_STATUS, TAMPER_7, 1, 7)
    FIELD(TAMPER_STATUS, TAMPER_6, 1, 6)
    FIELD(TAMPER_STATUS, TAMPER_5, 1, 5)
    FIELD(TAMPER_STATUS, TAMPER_4, 1, 4)
    FIELD(TAMPER_STATUS, TAMPER_3, 1, 3)
    FIELD(TAMPER_STATUS, TAMPER_2, 1, 2)
    FIELD(TAMPER_STATUS, TAMPER_1, 1, 1)
    FIELD(TAMPER_STATUS, TAMPER_0, 1, 0)
REG32(CSU_TAMPER_0, 0x5004)
    FIELD(CSU_TAMPER_0, BBRAM_ERASE, 1, 5)
    FIELD(CSU_TAMPER_0, SEC_LOCKDOWN_1, 1, 3)
    FIELD(CSU_TAMPER_0, SEC_LOCKDOWN_0, 1, 2)
    FIELD(CSU_TAMPER_0, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_0, SYS_INTERRUPT, 1, 0)
REG32(CSU_TAMPER_14, 0x503c)
    FIELD(CSU_TAMPER_14, BBRAM_ERASE, 1, 4)
    FIELD(CSU_TAMPER_14, SEC_LOCKDOWN_1, 1, 3)
    FIELD(CSU_TAMPER_14, SEC_LOCKDOWN_0, 1, 2)
    FIELD(CSU_TAMPER_14, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_14, SYS_INTERRUPT, 1, 0)
REG32(CSU_RAM_CFG, 0xf000)
    FIELD(CSU_RAM_CFG, PCAP_WR_RAM_EMAS, 1, 25)
    FIELD(CSU_RAM_CFG, PCAP_WR_RAM_EMAA, 3, 22)
    FIELD(CSU_RAM_CFG, PCAP_WR_RAM_EMAB, 3, 19)
    FIELD(CSU_RAM_CFG, PCAP_RD_RAM_EMAS, 1, 18)
    FIELD(CSU_RAM_CFG, PCAP_RD_RAM_EMAA, 3, 15)
    FIELD(CSU_RAM_CFG, PCAP_RD_RAM_EMAB, 3, 12)
    FIELD(CSU_RAM_CFG, RSA_RAM_EMAS, 1, 11)
    FIELD(CSU_RAM_CFG, RSA_RAM_EMAW, 2, 9)
    FIELD(CSU_RAM_CFG, RSA_RAM_EMA, 3, 6)
    FIELD(CSU_RAM_CFG, TMR_RAM_EMAS, 1, 5)
    FIELD(CSU_RAM_CFG, TMR_RAM_EMAW, 2, 3)
    FIELD(CSU_RAM_CFG, TMR_RAM_EMA, 3, 0)
REG32(CSU_ROM_CFG, 0xf004)
    FIELD(CSU_ROM_CFG, ROM_KEN, 1, 3)
    FIELD(CSU_ROM_CFG, ROM_EMA, 3, 0)
REG32(TEST_CSU_CTRL, 0xf008)
    FIELD(TEST_CSU_CTRL, SSSS_SYSOSC_SEL, 3, 1)
    FIELD(TEST_CSU_CTRL, PSTP_CLK_SEL, 1, 0)
REG32(CSU_ECO_0, 0xfff4)
REG32(CSU_ECO_1, 0xfff8)
REG32(CSU_ECO_2, 0xfffc)

#define R_MAX (R_CSU_ECO_2 + 1)

typedef struct CSU {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq_csu;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} CSU;


static void csu_update_irq(CSU *s)
{
    bool pending = s->regs[R_CSU_ISR] & ~s->regs[R_CSU_IMR];
    qemu_set_irq(s->irq_csu, pending);
}
/* FIXME: Does the multi Boot register survive reset? */

static void csu_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XILINX_CSU_CORE(reg->opaque);
    csu_update_irq(s);
}

static void csu_core_reset(DeviceState *dev)
{
    CSU *s = XILINX_CSU_CORE(dev);
    int i;
 
    for (i = 0; i < R_MAX; ++i) {
        /*FIXME: Exclude bootmode and multiboot and reset reason */
        register_reset(&s->regs_info[i]);
    }

    csu_update_irq(s);
}

static uint64_t int_enable_pre_write(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XILINX_CSU_CORE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CSU_IMR] &= ~val;
    csu_update_irq(s);
    return 0;
}

static uint64_t int_disable_pre_write(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XILINX_CSU_CORE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CSU_IMR] |= val;
    csu_update_irq(s);
    return 0;
}

static const RegisterAccessInfo csu_core_regs_info[] = {
    {   .name = "CSU_STATUS",  .decode.addr = A_CSU_STATUS,
        .ro = 0xffffffff,
    },{ .name = "CSU_CTRL",  .decode.addr = A_CSU_CTRL,
        .rsvd = 0xe,
    },{ .name = "CSU_SSS_CFG",  .decode.addr = A_CSU_SSS_CFG,
    },{ .name = "CSU_DMA_RESET",  .decode.addr = A_CSU_DMA_RESET,
    },{ .name = "CSU_MULTI_BOOT",  .decode.addr = A_CSU_MULTI_BOOT,
    },{ .name = "CSU_TAMPER_TRIG",  .decode.addr = A_CSU_TAMPER_TRIG,
    },{ .name = "CSU_FT_STATUS",  .decode.addr = A_CSU_FT_STATUS,
        .rsvd = 0x300030,
        .ro = 0xffffffff,
    },{ .name = "Interrupt Status",  .decode.addr = A_CSU_ISR,
        .w1c = 0xffffffff,
        .post_write = csu_isr_postw,
    },{ .name = "Interrupt Mask",  .decode.addr = A_CSU_IMR,
        .reset = 0xffffffff,
        .ro = 0xffffffff,
    },{ .name = "Interrupt Enable",  .decode.addr = A_CSU_IER,
        .pre_write = int_enable_pre_write,
    },{ .name = "Interrupt Disable",  .decode.addr = A_CSU_IDR,
        .pre_write = int_disable_pre_write,
    },{ .name = "JTAG_CHAIN_CFG",  .decode.addr = A_JTAG_CHAIN_CFG,
    },{ .name = "JTAG_CHAIN_STATUS",  .decode.addr = A_JTAG_CHAIN_STATUS,
        .ro = 0x3,
    },{ .name = "JTAG_SEC",  .decode.addr = A_JTAG_SEC,
    },{ .name = "JTAG_DAP_CFG",  .decode.addr = A_JTAG_DAP_CFG,
    },{ .name = "IDCODE",  .decode.addr = A_IDCODE,
        .ro = 0xffffffff, .reset = QEMU_IDCODE,
    },{ .name = "VERSION",  .decode.addr = A_VERSION,
        .ro = 0xfffff,
        .reset = XFSBL_PLATFORM_QEMU,
    },
#define P(n) \
    {   .name = "CSU_ROM_DIGEST_" #n, \
        .decode.addr = A_CSU_ROM_DIGEST_0 + n * 4, \
        .reset = 0xffffffff, \
        .ro = 0xffffffff, },
    P(0) P(2) P(3) P(4) P(5) P(6) P(7) P(8) P(9) P(10) P(11)
#undef P
     { .name = "SCRATCH_0",  .decode.addr = A_SCRATCH_0,
    },{ .name = "SCRATCH_1",  .decode.addr = A_SCRATCH_1,
    },{ .name = "SCRATCH_2",  .decode.addr = A_SCRATCH_2,
    },{ .name = "SCRATCH_3",  .decode.addr = A_SCRATCH_3,
    },{ .name = "SCRATCH_4",  .decode.addr = A_SCRATCH_4,
    },{ .name = "SCRATCH_5",  .decode.addr = A_SCRATCH_5,
    },{ .name = "SCRATCH_6",  .decode.addr = A_SCRATCH_6,
    },{ .name = "SCRATCH_7",  .decode.addr = A_SCRATCH_7,
    },{ .name = "AES_STATUS",  .decode.addr = A_AES_STATUS,
        .reset = 0xf00,
        .rsvd = 0xc0,
        .ro = 0xfff,
    },{ .name = "AES_KEY_SRC",  .decode.addr = A_AES_KEY_SRC,
    },{ .name = "AES_KEY_LOAD",  .decode.addr = A_AES_KEY_LOAD,
    },{ .name = "AES_START_MSG",  .decode.addr = A_AES_START_MSG,
    },{ .name = "AES_RESET",  .decode.addr = A_AES_RESET,
    },{ .name = "AES_KEY_CLEAR",  .decode.addr = A_AES_KEY_CLEAR,
    },{ .name = "AES_CFG",  .decode.addr = A_AES_CFG,
    },{ .name = "AES_KUP_WR",  .decode.addr = A_AES_KUP_WR,
    },{ .name = "AES_KUP_0",  .decode.addr = A_AES_KUP_0,
    },{ .name = "AES_KUP_1",  .decode.addr = A_AES_KUP_1,
    },{ .name = "AES_KUP_2",  .decode.addr = A_AES_KUP_2,
    },{ .name = "AES_KUP_3",  .decode.addr = A_AES_KUP_3,
    },{ .name = "AES_KUP_4",  .decode.addr = A_AES_KUP_4,
    },{ .name = "AES_KUP_5",  .decode.addr = A_AES_KUP_5,
    },{ .name = "AES_KUP_6",  .decode.addr = A_AES_KUP_6,
    },{ .name = "AES_KUP_7",  .decode.addr = A_AES_KUP_7,
    },{ .name = "AES_IV_0",  .decode.addr = A_AES_IV_0,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_1",  .decode.addr = A_AES_IV_1,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_2",  .decode.addr = A_AES_IV_2,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_3",  .decode.addr = A_AES_IV_3,
        .ro = 0xffffffff,
    },{ .name = "SHA_START",  .decode.addr = A_SHA_START,
    },{ .name = "SHA_RESET",  .decode.addr = A_SHA_RESET,
    },{ .name = "SHA_DONE",  .decode.addr = A_SHA_DONE,
        .ro = 0x1,
    },{ .name = "SHA_DIGEST_0",  .decode.addr = A_SHA_DIGEST_0,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_1",  .decode.addr = A_SHA_DIGEST_1,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_2",  .decode.addr = A_SHA_DIGEST_2,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_3",  .decode.addr = A_SHA_DIGEST_3,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_4",  .decode.addr = A_SHA_DIGEST_4,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_5",  .decode.addr = A_SHA_DIGEST_5,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_6",  .decode.addr = A_SHA_DIGEST_6,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_7",  .decode.addr = A_SHA_DIGEST_7,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_8",  .decode.addr = A_SHA_DIGEST_8,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_9",  .decode.addr = A_SHA_DIGEST_9,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_10",  .decode.addr = A_SHA_DIGEST_10,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_11",  .decode.addr = A_SHA_DIGEST_11,
        .ro = 0xffffffff,
    },{ .name = "PCAP_PROG",  .decode.addr = A_PCAP_PROG,
    },{ .name = "PCAP_RDWR",  .decode.addr = A_PCAP_RDWR,
    },{ .name = "PCAP_CTRL",  .decode.addr = A_PCAP_CTRL,
        .reset = 0x1,
    },{ .name = "PCAP_RESET",  .decode.addr = A_PCAP_RESET,
    },{ .name = "PCAP_STATUS",  .decode.addr = A_PCAP_STATUS,
        .reset = 0x3,
        .rsvd = 0x1fffc000,
        .ro = 0xffffffff,
    },{ .name = "PUF_CMD",  .decode.addr = A_PUF_CMD,
    },{ .name = "PUF_CFG0",  .decode.addr = A_PUF_CFG0,
        .reset = 0x2,
    },{ .name = "PUF_CFG1",  .decode.addr = A_PUF_CFG1,
        .reset = 0x80080,
    },{ .name = "PUF_SHUT",  .decode.addr = A_PUF_SHUT,
        .reset = 0x1000020,
    },{ .name = "PUF_STATUS",  .decode.addr = A_PUF_STATUS,
        .rsvd = 0x4,
        .ro = 0x3fffffff,
    },{ .name = "PUF_DBG",  .decode.addr = A_PUF_DBG,
    },{ .name = "PUF_WORD",  .decode.addr = A_PUF_WORD,
        .ro = 0xffffffff,
    },{ .name = "PUF_TM_STATUS",  .decode.addr = A_PUF_TM_STATUS,
        .ro = 0x1,
    },{ .name = "PUF_TM_UL",  .decode.addr = A_PUF_TM_UL,
    },{ .name = "PUF_TM_LL",  .decode.addr = A_PUF_TM_LL,
    },{ .name = "PUF_TM_SW",  .decode.addr = A_PUF_TM_SW,
        .reset = 0x1000020,
    },{ .name = "PUF_TM_TR",  .decode.addr = A_PUF_TM_TR,
        .rsvd = 0xfffe,
    },{ .name = "TAMPER_STATUS",  .decode.addr = A_TAMPER_STATUS,
        .w1c = 0x3fff,
    },
#define P(n) \
    {   .name = "CSU_TAMPER_" #n, .decode.addr = A_CSU_TAMPER_0 + n * 4 \
    },
    P(0) P(2) P(3) P(4) P(5) P(6) P(7) P(8) P(9) P(10) P(11) P(12) P(13)
#undef P
     { .name = "CSU_TAMPER_14",  .decode.addr = A_CSU_TAMPER_14,
        .reset = 0xc,
    },{ .name = "CSU_RAM_CFG",  .decode.addr = A_CSU_RAM_CFG,
        .reset = 0x2ddb2cb,
    },{ .name = "CSU_ROM_CFG",  .decode.addr = A_CSU_ROM_CFG,
        .reset = 0x3,
    },{ .name = "TEST_CSU_CTRL",  .decode.addr = A_TEST_CSU_CTRL,
    },{ .name = "CSU_ECO_0",  .decode.addr = A_CSU_ECO_0,
        .ro = 0xffffffff,
    },{ .name = "CSU_ECO_1",  .decode.addr = A_CSU_ECO_1,
    },{ .name = "CSU_ECO_2",  .decode.addr = A_CSU_ECO_2,
        .ro = 0xffffffff,
    }
};

static const MemoryRegionOps csu_core_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void csu_core_realize(DeviceState *dev, Error **errp)
{
    CSU *s = XILINX_CSU_CORE(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < ARRAY_SIZE(csu_core_regs_info); ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    csu_core_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &csu_core_regs_info[i],
            .debug = XILINX_CSU_CORE_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(dev), &csu_core_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, r->access->decode.addr, &r->mem);
    }
    return;
}

static void csu_core_init(Object *obj)
{
    CSU *s = XILINX_CSU_CORE(obj);

    memory_region_init(&s->iomem, obj, TYPE_XILINX_CSU_CORE, R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_csu);
}

/* FIXME: With no regs we are actually stateless. Although post load we need
 * to call notify() to start up the fire-hose of zeros again.
 */

static const VMStateDescription vmstate_csu_core = {
    .name = TYPE_XILINX_CSU_CORE,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CSU, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void csu_core_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = csu_core_reset;
    dc->realize = csu_core_realize;
    dc->vmsd = &vmstate_csu_core;
}

static const TypeInfo csu_core_info = {
    .name          = TYPE_XILINX_CSU_CORE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CSU),
    .class_init    = csu_core_class_init,
    .instance_init = csu_core_init,
};

static void csu_core_register_types(void)
{
    type_register_static(&csu_core_info);
}

type_init(csu_core_register_types)
