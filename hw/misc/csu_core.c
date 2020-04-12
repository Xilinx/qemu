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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"
#include "hw/irq.h"

#ifndef XLNX_CSU_CORE_ERR_DEBUG
#define XLNX_CSU_CORE_ERR_DEBUG 0
#endif

#define TYPE_XLNX_CSU_CORE "xlnx.zynqmp-csu-core"

#define XLNX_CSU_CORE(obj) \
     OBJECT_CHECK(CSU, (obj), TYPE_XLNX_CSU_CORE)

#define VERSION_PLATFORM_QEMU   0x3
#define VERSION_PS_VERSION_PROD 0x3
#define QEMU_IDCODE             0x4600093

REG32(CSU_STATUS, 0x0)
    FIELD(CSU_STATUS, BOOT_ENC, 1, 1)
    FIELD(CSU_STATUS, BOOT_AUTH, 0, 1)
REG32(CSU_CTRL, 0x4)
    FIELD(CSU_CTRL, SLVERR_ENABLE, 4, 1)
    FIELD(CSU_CTRL, CSU_CLK_SEL, 0, 1)
REG32(CSU_SSS_CFG, 0x8)
    FIELD(CSU_SSS_CFG, SHA_SSS, 12, 4)
    FIELD(CSU_SSS_CFG, AES_SSS, 8, 4)
    FIELD(CSU_SSS_CFG, DMA_SSS, 4, 4)
    FIELD(CSU_SSS_CFG, PCAP_SSS, 0, 4)
REG32(CSU_DMA_RESET, 0xc)
    FIELD(CSU_DMA_RESET, RESET, 0, 1)
REG32(CSU_MULTI_BOOT, 0x10)
REG32(CSU_TAMPER_TRIG, 0x14)
    FIELD(CSU_TAMPER_TRIG, TAMPER, 0, 1)
REG32(CSU_FT_STATUS, 0x18)
    FIELD(CSU_FT_STATUS, R_UE, 31, 1)
    FIELD(CSU_FT_STATUS, R_VOTER_ERROR, 30, 1)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_23, 29, 1)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_13, 28, 1)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_12, 27, 1)
    FIELD(CSU_FT_STATUS, R_MISMATCH_23_A, 26, 1)
    FIELD(CSU_FT_STATUS, R_MISMATCH_13_A, 25, 1)
    FIELD(CSU_FT_STATUS, R_MISMATCH_12_A, 24, 1)
    FIELD(CSU_FT_STATUS, R_FT_ST_MISMATCH, 23, 1)
    FIELD(CSU_FT_STATUS, R_CPU_ID_MISMATCH, 22, 1)
    FIELD(CSU_FT_STATUS, R_SLEEP_RESET, 19, 1)
    FIELD(CSU_FT_STATUS, R_MISMATCH_23_B, 18, 1)
    FIELD(CSU_FT_STATUS, R_MISMATCH_13_B, 17, 1)
    FIELD(CSU_FT_STATUS, R_MISMATCH_12_B, 16, 1)
    FIELD(CSU_FT_STATUS, N_UE, 15, 1)
    FIELD(CSU_FT_STATUS, N_VOTER_ERROR, 14, 1)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_23, 13, 1)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_13, 12, 1)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_12, 11, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_23_A, 10, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_13_A, 9, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_12_A, 8, 1)
    FIELD(CSU_FT_STATUS, N_FT_ST_MISMATCH, 7, 1)
    FIELD(CSU_FT_STATUS, N_CPU_ID_MISMATCH, 6, 1)
    FIELD(CSU_FT_STATUS, N_SLEEP_RESET, 3, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_23_B, 2, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_13_B, 1, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_12_B, 0, 1)
REG32(CSU_ISR, 0x20)
    FIELD(CSU_ISR, CSU_PL_ISO, 15, 1)
    FIELD(CSU_ISR, CSU_RAM_ECC_ERROR, 14, 1)
    FIELD(CSU_ISR, TAMPER, 13, 1)
    FIELD(CSU_ISR, PUF_ACC_ERROR, 12, 1)
    FIELD(CSU_ISR, APB_SLVERR, 11, 1)
    FIELD(CSU_ISR, TMR_FATAL, 10, 1)
    FIELD(CSU_ISR, PL_SEU_ERROR, 9, 1)
    FIELD(CSU_ISR, AES_ERROR, 8, 1)
    FIELD(CSU_ISR, PCAP_WR_OVERFLOW, 7, 1)
    FIELD(CSU_ISR, PCAP_RD_OVERFLOW, 6, 1)
    FIELD(CSU_ISR, PL_POR_B, 5, 1)
    FIELD(CSU_ISR, PL_INIT, 4, 1)
    FIELD(CSU_ISR, PL_DONE, 3, 1)
    FIELD(CSU_ISR, SHA_DONE, 2, 1)
    FIELD(CSU_ISR, RSA_DONE, 1, 1)
    FIELD(CSU_ISR, AES_DONE, 0, 1)
REG32(CSU_IMR, 0x24)
    FIELD(CSU_IMR, CSU_PL_ISO, 15, 1)
    FIELD(CSU_IMR, CSU_RAM_ECC_ERROR, 14, 1)
    FIELD(CSU_IMR, TAMPER, 13, 1)
    FIELD(CSU_IMR, PUF_ACC_ERROR, 12, 1)
    FIELD(CSU_IMR, APB_SLVERR, 11, 1)
    FIELD(CSU_IMR, TMR_FATAL, 10, 1)
    FIELD(CSU_IMR, PL_SEU_ERROR, 9, 1)
    FIELD(CSU_IMR, AES_ERROR, 8, 1)
    FIELD(CSU_IMR, PCAP_WR_OVERFLOW, 7, 1)
    FIELD(CSU_IMR, PCAP_RD_OVERFLOW, 6, 1)
    FIELD(CSU_IMR, PL_POR_B, 5, 1)
    FIELD(CSU_IMR, PL_INIT, 4, 1)
    FIELD(CSU_IMR, PL_DONE, 3, 1)
    FIELD(CSU_IMR, SHA_DONE, 2, 1)
    FIELD(CSU_IMR, RSA_DONE, 1, 1)
    FIELD(CSU_IMR, AES_DONE, 0, 1)
REG32(CSU_IER, 0x28)
    FIELD(CSU_IER, CSU_PL_ISO, 15, 1)
    FIELD(CSU_IER, CSU_RAM_ECC_ERROR, 14, 1)
    FIELD(CSU_IER, TAMPER, 13, 1)
    FIELD(CSU_IER, PUF_ACC_ERROR, 12, 1)
    FIELD(CSU_IER, APB_SLVERR, 11, 1)
    FIELD(CSU_IER, TMR_FATAL, 10, 1)
    FIELD(CSU_IER, PL_SEU_ERROR, 9, 1)
    FIELD(CSU_IER, AES_ERROR, 8, 1)
    FIELD(CSU_IER, PCAP_WR_OVERFLOW, 7, 1)
    FIELD(CSU_IER, PCAP_RD_OVERFLOW, 6, 1)
    FIELD(CSU_IER, PL_POR_B, 5, 1)
    FIELD(CSU_IER, PL_INIT, 4, 1)
    FIELD(CSU_IER, PL_DONE, 3, 1)
    FIELD(CSU_IER, SHA_DONE, 2, 1)
    FIELD(CSU_IER, RSA_DONE, 1, 1)
    FIELD(CSU_IER, AES_DONE, 0, 1)
REG32(CSU_IDR, 0x2c)
    FIELD(CSU_IDR, CSU_PL_ISO, 15, 1)
    FIELD(CSU_IDR, CSU_RAM_ECC_ERROR, 14, 1)
    FIELD(CSU_IDR, TAMPER, 13, 1)
    FIELD(CSU_IDR, PUF_ACC_ERROR, 12, 1)
    FIELD(CSU_IDR, APB_SLVERR, 11, 1)
    FIELD(CSU_IDR, TMR_FATAL, 10, 1)
    FIELD(CSU_IDR, PL_SEU_ERROR, 9, 1)
    FIELD(CSU_IDR, AES_ERROR, 8, 1)
    FIELD(CSU_IDR, PCAP_WR_OVERFLOW, 7, 1)
    FIELD(CSU_IDR, PCAP_RD_OVERFLOW, 6, 1)
    FIELD(CSU_IDR, PL_POR_B, 5, 1)
    FIELD(CSU_IDR, PL_INIT, 4, 1)
    FIELD(CSU_IDR, PL_DONE, 3, 1)
    FIELD(CSU_IDR, SHA_DONE, 2, 1)
    FIELD(CSU_IDR, RSA_DONE, 1, 1)
    FIELD(CSU_IDR, AES_DONE, 0, 1)
REG32(JTAG_CHAIN_STATUS, 0x34)
    FIELD(JTAG_CHAIN_STATUS, ARM_DAP, 1, 1)
    FIELD(JTAG_CHAIN_STATUS, PL_TAP, 0, 1)
REG32(JTAG_SEC, 0x38)
    FIELD(JTAG_SEC, SSSS_PMU_SEC, 6, 3)
    FIELD(JTAG_SEC, SSSS_PLTAP_SEC, 3, 3)
    FIELD(JTAG_SEC, SSSS_DAP_SEC, 0, 3)
REG32(JTAG_DAP_CFG, 0x3c)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_NIDEN, 5, 1)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_DBGEN, 4, 1)
    FIELD(JTAG_DAP_CFG, SSSS_APU_SPNIDEN, 3, 1)
    FIELD(JTAG_DAP_CFG, SSSS_APU_SPIDEN, 2, 1)
    FIELD(JTAG_DAP_CFG, SSSS_APU_NIDEN, 1, 1)
    FIELD(JTAG_DAP_CFG, SSSS_APU_DBGEN, 0, 1)
REG32(IDCODE, 0x40)
REG32(VERSION, 0x44)
    FIELD(VERSION, PLATFORM, 12, 4)
    FIELD(VERSION, PS_VERSION, 0, 4)
REG32(CSU_ROM_DIGEST_0, 0x50)
REG32(CSU_ROM_DIGEST_1, 0x54)
REG32(CSU_ROM_DIGEST_2, 0x58)
REG32(CSU_ROM_DIGEST_3, 0x5C)
REG32(CSU_ROM_DIGEST_4, 0x60)
REG32(CSU_ROM_DIGEST_5, 0x64)
REG32(CSU_ROM_DIGEST_6, 0x68)
REG32(CSU_ROM_DIGEST_7, 0x6C)
REG32(CSU_ROM_DIGEST_8, 0x70)
REG32(CSU_ROM_DIGEST_9, 0x74)
REG32(CSU_ROM_DIGEST_10, 0x78)
REG32(CSU_ROM_DIGEST_11, 0x7C)
REG32(AES_STATUS, 0x1000)
    FIELD(AES_STATUS, OKR_ZEROED, 11, 1)
    FIELD(AES_STATUS, BOOT_ZEROED, 10, 1)
    FIELD(AES_STATUS, KUP_ZEROED, 9, 1)
    FIELD(AES_STATUS, AES_KEY_ZEROED, 8, 1)
    FIELD(AES_STATUS, KEY_INIT_DONE, 4, 1)
    FIELD(AES_STATUS, GCM_TAG_PASS, 3, 1)
    FIELD(AES_STATUS, DONE, 2, 1)
    FIELD(AES_STATUS, READY, 1, 1)
    FIELD(AES_STATUS, BUSY, 0, 1)
REG32(AES_KEY_SRC, 0x1004)
    FIELD(AES_KEY_SRC, KEY_SRC, 0, 4)
REG32(AES_KEY_LOAD, 0x1008)
    FIELD(AES_KEY_LOAD, KEY_LOAD, 0, 1)
REG32(AES_START_MSG, 0x100c)
    FIELD(AES_START_MSG, START_MSG, 0, 1)
REG32(AES_RESET, 0x1010)
    FIELD(AES_RESET, RESET, 0, 1)
REG32(AES_KEY_CLEAR, 0x1014)
    FIELD(AES_KEY_CLEAR, AES_KUP_ZERO, 1, 1)
    FIELD(AES_KEY_CLEAR, AES_KEY_ZERO, 0, 1)
REG32(AES_KUP_WR, 0x101c)
    FIELD(AES_KUP_WR, IV_WRITE, 1, 1)
    FIELD(AES_KUP_WR, KUP_WRITE, 0, 1)
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
    FIELD(SHA_START, START_MSG, 0, 1)
REG32(SHA_RESET, 0x2004)
    FIELD(SHA_RESET, RESET, 0, 1)
REG32(SHA_DONE, 0x2008)
    FIELD(SHA_DONE, SHA_DONE, 0, 1)
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
    FIELD(PCAP_PROG, PCFG_PROG_B, 0, 1)
REG32(PCAP_RDWR, 0x3004)
    FIELD(PCAP_RDWR, PCAP_RDWR_B, 0, 1)
REG32(PCAP_CTRL, 0x3008)
    FIELD(PCAP_CTRL, PCFG_GSR, 3, 1)
    FIELD(PCAP_CTRL, PCFG_GTS, 2, 1)
    FIELD(PCAP_CTRL, PCFG_POR_CNT_4K, 1, 1)
    FIELD(PCAP_CTRL, PCAP_PR, 0, 1)
REG32(PCAP_RESET, 0x300c)
    FIELD(PCAP_RESET, RESET, 0, 1)
REG32(PCAP_STATUS, 0x3010)
    FIELD(PCAP_STATUS, PCFG_GWE, 13, 1)
    FIELD(PCAP_STATUS, PCFG_MCAP_MODE, 12, 1)
    FIELD(PCAP_STATUS, PL_GTS_USR_B, 11, 1)
    FIELD(PCAP_STATUS, PL_GTS_CFG_B, 10, 1)
    FIELD(PCAP_STATUS, PL_GPWRDWN_B, 9, 1)
    FIELD(PCAP_STATUS, PL_GHIGH_B, 8, 1)
    FIELD(PCAP_STATUS, PL_FST_CFG, 7, 1)
    FIELD(PCAP_STATUS, PL_CFG_RESET_B, 6, 1)
    FIELD(PCAP_STATUS, PL_SEU_ERROR, 5, 1)
    FIELD(PCAP_STATUS, PL_EOS, 4, 1)
    FIELD(PCAP_STATUS, PL_DONE, 3, 1)
    FIELD(PCAP_STATUS, PL_INIT, 2, 1)
    FIELD(PCAP_STATUS, PCAP_RD_IDLE, 1, 1)
    FIELD(PCAP_STATUS, PCAP_WR_IDLE, 0, 1)
REG32(TAMPER_STATUS, 0x5000)
    FIELD(TAMPER_STATUS, TAMPER_13, 13, 1)
    FIELD(TAMPER_STATUS, TAMPER_12, 12, 1)
    FIELD(TAMPER_STATUS, TAMPER_11, 11, 1)
    FIELD(TAMPER_STATUS, TAMPER_10, 10, 1)
    FIELD(TAMPER_STATUS, TAMPER_9, 9, 1)
    FIELD(TAMPER_STATUS, TAMPER_8, 8, 1)
    FIELD(TAMPER_STATUS, TAMPER_7, 7, 1)
    FIELD(TAMPER_STATUS, TAMPER_6, 6, 1)
    FIELD(TAMPER_STATUS, TAMPER_5, 5, 1)
    FIELD(TAMPER_STATUS, TAMPER_4, 4, 1)
    FIELD(TAMPER_STATUS, TAMPER_3, 3, 1)
    FIELD(TAMPER_STATUS, TAMPER_2, 2, 1)
    FIELD(TAMPER_STATUS, TAMPER_1, 1, 1)
    FIELD(TAMPER_STATUS, TAMPER_0, 0, 1)
REG32(CSU_TAMPER_0, 0x5004)
    FIELD(CSU_TAMPER_0, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_0, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_0, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_0, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_0, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_1, 0x5008)
    FIELD(CSU_TAMPER_1, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_1, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_1, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_1, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_1, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_2, 0x500C)
    FIELD(CSU_TAMPER_2, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_2, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_2, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_2, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_2, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_3, 0x5010)
    FIELD(CSU_TAMPER_3, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_3, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_3, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_3, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_3, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_4, 0x5014)
    FIELD(CSU_TAMPER_4, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_4, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_4, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_4, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_4, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_5, 0x5018)
    FIELD(CSU_TAMPER_5, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_5, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_5, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_5, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_5, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_6, 0x501C)
    FIELD(CSU_TAMPER_6, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_6, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_6, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_6, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_6, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_7, 0x5020)
    FIELD(CSU_TAMPER_7, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_7, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_7, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_7, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_7, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_8, 0x5024)
    FIELD(CSU_TAMPER_8, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_8, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_8, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_8, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_8, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_9, 0x5028)
    FIELD(CSU_TAMPER_9, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_9, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_9, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_9, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_9, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_10, 0x502C)
    FIELD(CSU_TAMPER_10, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_10, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_10, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_10, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_10, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_11, 0x5030)
    FIELD(CSU_TAMPER_11, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_11, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_11, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_11, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_11, SYS_INTERRUPT, 0, 1)
REG32(CSU_TAMPER_12, 0x5034)
    FIELD(CSU_TAMPER_12, BBRAM_ERASE, 5, 1)
    FIELD(CSU_TAMPER_12, SEC_LOCKDOWN_1, 3, 1)
    FIELD(CSU_TAMPER_12, SEC_LOCKDOWN_0, 2, 1)
    FIELD(CSU_TAMPER_12, SYS_RESET, 1, 1)
    FIELD(CSU_TAMPER_12, SYS_INTERRUPT, 0, 1)

#define XLNX_CSU_CORE_R_MAX (R_CSU_TAMPER_12 + 1)

typedef struct CSU {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    struct {
        uint32_t idcode;
        struct {
            uint8_t platform;
            uint8_t ps_version;
        } version;
    } cfg;

    qemu_irq irq_csu;

    uint32_t regs[XLNX_CSU_CORE_R_MAX];
    RegisterInfo regs_info[XLNX_CSU_CORE_R_MAX];
} CSU;

static void csu_update_irq(CSU *s)
{
    bool pending = s->regs[R_CSU_ISR] & ~s->regs[R_CSU_IMR];
    qemu_set_irq(s->irq_csu, pending);
}

static void csu_isr_set_puf_acc_error(void *opaque, int n, int level)
{
    CSU *s = XLNX_CSU_CORE(opaque);

    /* This error is only a positive-edge latch */
    if (!level || ARRAY_FIELD_EX32(s->regs, CSU_ISR, PUF_ACC_ERROR)) {
        return;
    }

    ARRAY_FIELD_DP32(s->regs, CSU_ISR, PUF_ACC_ERROR, 1);
    csu_update_irq(s);
}

static void csu_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XLNX_CSU_CORE(reg->opaque);
    csu_update_irq(s);
}

static uint64_t int_enable_pre_write(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XLNX_CSU_CORE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CSU_IMR] &= ~val;
    csu_update_irq(s);
    return 0;
}

static uint64_t int_disable_pre_write(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XLNX_CSU_CORE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CSU_IMR] |= val;
    csu_update_irq(s);
    return 0;
}

static const RegisterAccessInfo csu_core_regs_info[] = {
    {   .name = "CSU_STATUS",  .addr = A_CSU_STATUS,
        .ro = 0xffffffff,
    },{ .name = "CSU_CTRL",  .addr = A_CSU_CTRL,
        .rsvd = 0xe,
    },{ .name = "CSU_SSS_CFG",  .addr = A_CSU_SSS_CFG,
    },{ .name = "CSU_DMA_RESET",  .addr = A_CSU_DMA_RESET,
    },{ .name = "CSU_MULTI_BOOT",  .addr = A_CSU_MULTI_BOOT,
    },{ .name = "CSU_TAMPER_TRIG",  .addr = A_CSU_TAMPER_TRIG,
    },{ .name = "CSU_FT_STATUS",  .addr = A_CSU_FT_STATUS,
        .rsvd = 0x300030,
        .ro = 0xffffffff,
    },{ .name = "Interrupt Status",  .addr = A_CSU_ISR,
        .w1c = 0xffffffff,
        .post_write = csu_isr_postw,
    },{ .name = "Interrupt Mask",  .addr = A_CSU_IMR,
        .reset = 0xffffffff,
        .ro = 0xffffffff,
    },{ .name = "Interrupt Enable",  .addr = A_CSU_IER,
        .pre_write = int_enable_pre_write,
    },{ .name = "Interrupt Disable",  .addr = A_CSU_IDR,
        .pre_write = int_disable_pre_write,
    },{ .name = "JTAG_CHAIN_STATUS",  .addr = A_JTAG_CHAIN_STATUS,
        .ro = 0x3,
    },{ .name = "JTAG_SEC",  .addr = A_JTAG_SEC,
    },{ .name = "JTAG_DAP_CFG",  .addr = A_JTAG_DAP_CFG,
    },{ .name = "IDCODE",  .addr = A_IDCODE,
        .ro = 0xffffffff,
    },{ .name = "VERSION",  .addr = A_VERSION,
        .ro = 0xfffff,
    },
#define P(n) \
    {   .name = "CSU_ROM_DIGEST_" #n, \
        .addr = A_CSU_ROM_DIGEST_0 + n * 4, \
        .reset = 0xffffffff, \
        .ro = 0xffffffff, },
    P(0) P(1) P(2) P(3) P(4) P(5) P(6) P(7) P(8) P(9) P(10) P(11)
#undef P
    { .name = "AES_STATUS",  .addr = A_AES_STATUS,
        .reset = 0xf00,
        .rsvd = 0xc0,
        .ro = 0xfff,
    },{ .name = "AES_KEY_SRC",  .addr = A_AES_KEY_SRC,
    },{ .name = "AES_KEY_LOAD",  .addr = A_AES_KEY_LOAD,
    },{ .name = "AES_START_MSG",  .addr = A_AES_START_MSG,
    },{ .name = "AES_RESET",  .addr = A_AES_RESET,
    },{ .name = "AES_KEY_CLEAR",  .addr = A_AES_KEY_CLEAR,
    },{ .name = "AES_KUP_WR",  .addr = A_AES_KUP_WR,
    },{ .name = "AES_KUP_0",  .addr = A_AES_KUP_0,
    },{ .name = "AES_KUP_1",  .addr = A_AES_KUP_1,
    },{ .name = "AES_KUP_2",  .addr = A_AES_KUP_2,
    },{ .name = "AES_KUP_3",  .addr = A_AES_KUP_3,
    },{ .name = "AES_KUP_4",  .addr = A_AES_KUP_4,
    },{ .name = "AES_KUP_5",  .addr = A_AES_KUP_5,
    },{ .name = "AES_KUP_6",  .addr = A_AES_KUP_6,
    },{ .name = "AES_KUP_7",  .addr = A_AES_KUP_7,
    },{ .name = "AES_IV_0",  .addr = A_AES_IV_0,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_1",  .addr = A_AES_IV_1,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_2",  .addr = A_AES_IV_2,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_3",  .addr = A_AES_IV_3,
        .ro = 0xffffffff,
    },{ .name = "SHA_START",  .addr = A_SHA_START,
    },{ .name = "SHA_RESET",  .addr = A_SHA_RESET,
    },{ .name = "SHA_DONE",  .addr = A_SHA_DONE,
        .ro = 0x1,
    },{ .name = "SHA_DIGEST_0",  .addr = A_SHA_DIGEST_0,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_1",  .addr = A_SHA_DIGEST_1,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_2",  .addr = A_SHA_DIGEST_2,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_3",  .addr = A_SHA_DIGEST_3,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_4",  .addr = A_SHA_DIGEST_4,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_5",  .addr = A_SHA_DIGEST_5,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_6",  .addr = A_SHA_DIGEST_6,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_7",  .addr = A_SHA_DIGEST_7,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_8",  .addr = A_SHA_DIGEST_8,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_9",  .addr = A_SHA_DIGEST_9,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_10",  .addr = A_SHA_DIGEST_10,
        .ro = 0xffffffff,
    },{ .name = "SHA_DIGEST_11",  .addr = A_SHA_DIGEST_11,
        .ro = 0xffffffff,
    },{ .name = "PCAP_PROG",  .addr = A_PCAP_PROG,
    },{ .name = "PCAP_RDWR",  .addr = A_PCAP_RDWR,
    },{ .name = "PCAP_CTRL",  .addr = A_PCAP_CTRL,
        .reset = 0x1,
    },{ .name = "PCAP_RESET",  .addr = A_PCAP_RESET,
    },{ .name = "PCAP_STATUS",  .addr = A_PCAP_STATUS,
        .reset = 0x3,
        .rsvd = 0x1fffc000,
        .ro = 0xffffffff,
    },{ .name = "TAMPER_STATUS",  .addr = A_TAMPER_STATUS,
        .w1c = 0x3fff,
    },
#define P(n) \
    {   .name = "CSU_TAMPER_" #n, .addr = A_CSU_TAMPER_0 + n * 4 \
    },
    P(0) P(1) P(2) P(3) P(4) P(5) P(6) P(7) P(8) P(9) P(10) P(11) P(12)
#undef P
};

static const MemoryRegionOps csu_core_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void csu_core_reset(DeviceState *dev)
{
    CSU *s = XLNX_CSU_CORE(dev);
    int i;

    for (i = 0; i < XLNX_CSU_CORE_R_MAX; ++i) {
        if (i != R_CSU_MULTI_BOOT) {
            register_reset(&s->regs_info[i]);
        }
    }
    s->regs[R_IDCODE] = s->cfg.idcode;
    /* Indicates the PL is powered up. */
    ARRAY_FIELD_DP32(s->regs, CSU_ISR, PL_POR_B, 1);
    ARRAY_FIELD_DP32(s->regs, VERSION, PLATFORM, s->cfg.version.platform);
    ARRAY_FIELD_DP32(s->regs, VERSION, PS_VERSION, s->cfg.version.ps_version);
    csu_update_irq(s);
}


static void csu_core_realize(DeviceState *dev, Error **errp)
{

}

static void csu_core_init(Object *obj)
{
    CSU *s = XLNX_CSU_CORE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_CSU_CORE,
                       XLNX_CSU_CORE_R_MAX * 4);
    reg_array = register_init_block32(DEVICE(obj), csu_core_regs_info,
                                      ARRAY_SIZE(csu_core_regs_info),
                                      s->regs_info, s->regs,
                                      &csu_core_ops,
                                      XLNX_CSU_CORE_ERR_DEBUG,
                                      XLNX_CSU_CORE_R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x00, &reg_array->mem);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_csu);

    qdev_init_gpio_in_named(DEVICE(obj), csu_isr_set_puf_acc_error,
                            "puf-acc-error", 1);
}

static const VMStateDescription vmstate_csu_core = {
    .name = TYPE_XLNX_CSU_CORE,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CSU, XLNX_CSU_CORE_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property csu_core_properties[] = {
    DEFINE_PROP_UINT8("version-platform", CSU, cfg.version.platform,
                      VERSION_PLATFORM_QEMU),
    DEFINE_PROP_UINT8("version-ps-version", CSU, cfg.version.ps_version,
                      VERSION_PS_VERSION_PROD),
    DEFINE_PROP_UINT32("idcode", CSU, cfg.idcode, QEMU_IDCODE),
    DEFINE_PROP_END_OF_LIST(),
};

static void csu_core_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = csu_core_reset;
    dc->realize = csu_core_realize;
    device_class_set_props(dc, csu_core_properties);
    dc->vmsd = &vmstate_csu_core;
}

static const TypeInfo csu_core_info = {
    .name          = TYPE_XLNX_CSU_CORE,
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
