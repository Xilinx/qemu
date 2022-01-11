/*
 * QEMU model of the random number generation (RNG) in Xilinx
 * Public Key Infrastructure subsystem.
 *
 * WARNING: The model for each RNG is cryptographically very weak,
 * so as not to drain the entropy pool of the host.
 *
 * Copyright (c) 2022 Xilinx Inc.
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
#include "hw/misc/xlnx-versal-psx-pki-rng.h"
#include "hw/qdev-properties.h"

#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "crypto/hash.h"

#ifndef XLNX_PSX_PKI_RNG_ERR_DEBUG
#define XLNX_PSX_PKI_RNG_ERR_DEBUG 0
#endif

REG32(GENRL_CTRL, 0x0)
    FIELD(GENRL_CTRL, AXPROT1, 8, 1)
    FIELD(GENRL_CTRL, XRESP, 4, 2)
    FIELD(GENRL_CTRL, PSLVERR, 0, 1)
REG32(NRN_AVAIL, 0x800)
    FIELD(NRN_AVAIL, NUM, 0, 6)
REG32(NRN_THRESH, 0x804)
    FIELD(NRN_THRESH, VAL, 0, 6)
REG32(INTR_STS, 0xe00)
    FIELD(INTR_STS, NRNAVAIL, 24, 1)
    FIELD(INTR_STS, TRNG7AC, 15, 1)
    FIELD(INTR_STS, TRNG6AC, 14, 1)
    FIELD(INTR_STS, TRNG5AC, 13, 1)
    FIELD(INTR_STS, TRNG4AC, 12, 1)
    FIELD(INTR_STS, TRNG3AC, 11, 1)
    FIELD(INTR_STS, TRNG2AC, 10, 1)
    FIELD(INTR_STS, TRNG1AC, 9, 1)
    FIELD(INTR_STS, TRNG0AC, 8, 1)
    FIELD(INTR_STS, TRNG7INT, 7, 1)
    FIELD(INTR_STS, TRNG6INT, 6, 1)
    FIELD(INTR_STS, TRNG5INT, 5, 1)
    FIELD(INTR_STS, TRNG4INT, 4, 1)
    FIELD(INTR_STS, TRNG3INT, 3, 1)
    FIELD(INTR_STS, TRNG2INT, 2, 1)
    FIELD(INTR_STS, TRNG1INT, 1, 1)
    FIELD(INTR_STS, TRNG0INT, 0, 1)
REG32(INTR_EN, 0xe04)
    FIELD(INTR_EN, NRNAVAIL, 24, 1)
    FIELD(INTR_EN, TRNG7AC, 15, 1)
    FIELD(INTR_EN, TRNG6AC, 14, 1)
    FIELD(INTR_EN, TRNG5AC, 13, 1)
    FIELD(INTR_EN, TRNG4AC, 12, 1)
    FIELD(INTR_EN, TRNG3AC, 11, 1)
    FIELD(INTR_EN, TRNG2AC, 10, 1)
    FIELD(INTR_EN, TRNG1AC, 9, 1)
    FIELD(INTR_EN, TRNG0AC, 8, 1)
    FIELD(INTR_EN, TRNG7INT, 7, 1)
    FIELD(INTR_EN, TRNG6INT, 6, 1)
    FIELD(INTR_EN, TRNG5INT, 5, 1)
    FIELD(INTR_EN, TRNG4INT, 4, 1)
    FIELD(INTR_EN, TRNG3INT, 3, 1)
    FIELD(INTR_EN, TRNG2INT, 2, 1)
    FIELD(INTR_EN, TRNG1INT, 1, 1)
    FIELD(INTR_EN, TRNG0INT, 0, 1)
REG32(INTR_DIS, 0xe08)
    FIELD(INTR_DIS, NRNAVAIL, 24, 1)
    FIELD(INTR_DIS, TRNG7AC, 15, 1)
    FIELD(INTR_DIS, TRNG6AC, 14, 1)
    FIELD(INTR_DIS, TRNG5AC, 13, 1)
    FIELD(INTR_DIS, TRNG4AC, 12, 1)
    FIELD(INTR_DIS, TRNG3AC, 11, 1)
    FIELD(INTR_DIS, TRNG2AC, 10, 1)
    FIELD(INTR_DIS, TRNG1AC, 9, 1)
    FIELD(INTR_DIS, TRNG0AC, 8, 1)
    FIELD(INTR_DIS, TRNG7INT, 7, 1)
    FIELD(INTR_DIS, TRNG6INT, 6, 1)
    FIELD(INTR_DIS, TRNG5INT, 5, 1)
    FIELD(INTR_DIS, TRNG4INT, 4, 1)
    FIELD(INTR_DIS, TRNG3INT, 3, 1)
    FIELD(INTR_DIS, TRNG2INT, 2, 1)
    FIELD(INTR_DIS, TRNG1INT, 1, 1)
    FIELD(INTR_DIS, TRNG0INT, 0, 1)
REG32(INTR_MASK, 0xe0c)
    FIELD(INTR_MASK, NRNAVAIL, 24, 1)
    FIELD(INTR_MASK, TRNG7AC, 15, 1)
    FIELD(INTR_MASK, TRNG6AC, 14, 1)
    FIELD(INTR_MASK, TRNG5AC, 13, 1)
    FIELD(INTR_MASK, TRNG4AC, 12, 1)
    FIELD(INTR_MASK, TRNG3AC, 11, 1)
    FIELD(INTR_MASK, TRNG2AC, 10, 1)
    FIELD(INTR_MASK, TRNG1AC, 9, 1)
    FIELD(INTR_MASK, TRNG0AC, 8, 1)
    FIELD(INTR_MASK, TRNG7INT, 7, 1)
    FIELD(INTR_MASK, TRNG6INT, 6, 1)
    FIELD(INTR_MASK, TRNG5INT, 5, 1)
    FIELD(INTR_MASK, TRNG4INT, 4, 1)
    FIELD(INTR_MASK, TRNG3INT, 3, 1)
    FIELD(INTR_MASK, TRNG2INT, 2, 1)
    FIELD(INTR_MASK, TRNG1INT, 1, 1)
    FIELD(INTR_MASK, TRNG0INT, 0, 1)
REG32(INTR_TRIG, 0xe10)
    FIELD(INTR_TRIG, NRNAVAIL, 24, 1)
    FIELD(INTR_TRIG, TRNG7AC, 15, 1)
    FIELD(INTR_TRIG, TRNG6AC, 14, 1)
    FIELD(INTR_TRIG, TRNG5AC, 13, 1)
    FIELD(INTR_TRIG, TRNG4AC, 12, 1)
    FIELD(INTR_TRIG, TRNG3AC, 11, 1)
    FIELD(INTR_TRIG, TRNG2AC, 10, 1)
    FIELD(INTR_TRIG, TRNG1AC, 9, 1)
    FIELD(INTR_TRIG, TRNG0AC, 8, 1)
    FIELD(INTR_TRIG, TRNG7INT, 7, 1)
    FIELD(INTR_TRIG, TRNG6INT, 6, 1)
    FIELD(INTR_TRIG, TRNG5INT, 5, 1)
    FIELD(INTR_TRIG, TRNG4INT, 4, 1)
    FIELD(INTR_TRIG, TRNG3INT, 3, 1)
    FIELD(INTR_TRIG, TRNG2INT, 2, 1)
    FIELD(INTR_TRIG, TRNG1INT, 1, 1)
    FIELD(INTR_TRIG, TRNG0INT, 0, 1)
REG32(SAFETY_CHK, 0xf0c)
REG32(TRNG0_INTR, 0x1000)
    FIELD(TRNG0_INTR, ICCERTF, 5, 1)
    FIELD(TRNG0_INTR, ICDTF, 4, 1)
    FIELD(TRNG0_INTR, ICDONE, 3, 1)
    FIELD(TRNG0_INTR, IECERTF, 2, 1)
    FIELD(TRNG0_INTR, IEDTF, 1, 1)
    FIELD(TRNG0_INTR, IEDONE, 0, 1)
REG32(TRNG0_STAT, 0x1004)
    FIELD(TRNG0_STAT, QCNT, 9, 3)
    FIELD(TRNG0_STAT, CERTF, 3, 1)
    FIELD(TRNG0_STAT, DTF, 1, 1)
    FIELD(TRNG0_STAT, DONE, 0, 1)
REG32(TRNG0_CTRL, 0x1008)
    FIELD(TRNG0_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG0_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG0_CTRL, EUMODE, 8, 1)
    FIELD(TRNG0_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG0_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG0_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG0_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG0_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG0_CTRL, PRNGSRST, 0, 1)
REG32(TRNG0_CONF0, 0x100c)
    FIELD(TRNG0_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG0_CONF0, DIT, 0, 5)
REG32(TRNG0_CONF1, 0x1010)
    FIELD(TRNG0_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG0_CONF1, DLEN, 0, 8)
REG32(TRNG0_TEST, 0x1014)
    FIELD(TRNG0_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG0_XSEED00, 0x1040)
REG32(TRNG0_XSEED01, 0x1044)
REG32(TRNG0_XSEED02, 0x1048)
REG32(TRNG0_XSEED03, 0x104c)
REG32(TRNG0_XSEED04, 0x1050)
REG32(TRNG0_XSEED05, 0x1054)
REG32(TRNG0_XSEED06, 0x1058)
REG32(TRNG0_XSEED07, 0x105c)
REG32(TRNG0_XSEED08, 0x1060)
REG32(TRNG0_XSEED09, 0x1064)
REG32(TRNG0_XSEED10, 0x1068)
REG32(TRNG0_XSEED11, 0x106c)
REG32(TRNG0_PSTR00, 0x1080)
REG32(TRNG0_PSTR01, 0x1084)
REG32(TRNG0_PSTR02, 0x1088)
REG32(TRNG0_PSTR03, 0x108c)
REG32(TRNG0_PSTR04, 0x1090)
REG32(TRNG0_PSTR05, 0x1094)
REG32(TRNG0_PSTR06, 0x1098)
REG32(TRNG0_PSTR07, 0x109c)
REG32(TRNG0_PSTR08, 0x10a0)
REG32(TRNG0_PSTR09, 0x10a4)
REG32(TRNG0_PSTR10, 0x10a8)
REG32(TRNG0_PSTR11, 0x10ac)
REG32(TRNG0_RND, 0x10c0)
REG32(TRNG0_RESET, 0x10d0)
    FIELD(TRNG0_RESET, ASSERT, 0, 1)
REG32(TRNG0_RO_EN, 0x10d4)
    FIELD(TRNG0_RO_EN, ACTIVE, 0, 1)
REG32(TRNG0_AUTOPROC, 0x1100)
    FIELD(TRNG0_AUTOPROC, CODE, 0, 1)
REG32(TRNG0_NRNPS, 0x1108)
    FIELD(TRNG0_NRNPS, NUM, 0, 10)
REG32(TRNG1_INTR, 0x1200)
    FIELD(TRNG1_INTR, ICCERTF, 5, 1)
    FIELD(TRNG1_INTR, ICDTF, 4, 1)
    FIELD(TRNG1_INTR, ICDONE, 3, 1)
    FIELD(TRNG1_INTR, IECERTF, 2, 1)
    FIELD(TRNG1_INTR, IEDTF, 1, 1)
    FIELD(TRNG1_INTR, IEDONE, 0, 1)
REG32(TRNG1_STAT, 0x1204)
    FIELD(TRNG1_STAT, QCNT, 9, 3)
    FIELD(TRNG1_STAT, CERTF, 3, 1)
    FIELD(TRNG1_STAT, DTF, 1, 1)
    FIELD(TRNG1_STAT, DONE, 0, 1)
REG32(TRNG1_CTRL, 0x1208)
    FIELD(TRNG1_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG1_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG1_CTRL, EUMODE, 8, 1)
    FIELD(TRNG1_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG1_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG1_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG1_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG1_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG1_CTRL, PRNGSRST, 0, 1)
REG32(TRNG1_CONF0, 0x120c)
    FIELD(TRNG1_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG1_CONF0, DIT, 0, 5)
REG32(TRNG1_CONF1, 0x1210)
    FIELD(TRNG1_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG1_CONF1, DLEN, 0, 8)
REG32(TRNG1_TEST, 0x1214)
    FIELD(TRNG1_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG1_XSEED00, 0x1240)
REG32(TRNG1_XSEED01, 0x1244)
REG32(TRNG1_XSEED02, 0x1248)
REG32(TRNG1_XSEED03, 0x124c)
REG32(TRNG1_XSEED04, 0x1250)
REG32(TRNG1_XSEED05, 0x1254)
REG32(TRNG1_XSEED06, 0x1258)
REG32(TRNG1_XSEED07, 0x125c)
REG32(TRNG1_XSEED08, 0x1260)
REG32(TRNG1_XSEED09, 0x1264)
REG32(TRNG1_XSEED10, 0x1268)
REG32(TRNG1_XSEED11, 0x126c)
REG32(TRNG1_PSTR00, 0x1280)
REG32(TRNG1_PSTR01, 0x1284)
REG32(TRNG1_PSTR02, 0x1288)
REG32(TRNG1_PSTR03, 0x128c)
REG32(TRNG1_PSTR04, 0x1290)
REG32(TRNG1_PSTR05, 0x1294)
REG32(TRNG1_PSTR06, 0x1298)
REG32(TRNG1_PSTR07, 0x129c)
REG32(TRNG1_PSTR08, 0x12a0)
REG32(TRNG1_PSTR09, 0x12a4)
REG32(TRNG1_PSTR10, 0x12a8)
REG32(TRNG1_PSTR11, 0x12ac)
REG32(TRNG1_RND, 0x12c0)
REG32(TRNG1_RESET, 0x12d0)
    FIELD(TRNG1_RESET, ASSERT, 0, 1)
REG32(TRNG1_RO_EN, 0x12d4)
    FIELD(TRNG1_RO_EN, ACTIVE, 0, 1)
REG32(TRNG1_AUTOPROC, 0x1300)
    FIELD(TRNG1_AUTOPROC, CODE, 0, 1)
REG32(TRNG1_NRNPS, 0x1308)
    FIELD(TRNG1_NRNPS, NUM, 0, 10)
REG32(TRNG2_INTR, 0x1400)
    FIELD(TRNG2_INTR, ICCERTF, 5, 1)
    FIELD(TRNG2_INTR, ICDTF, 4, 1)
    FIELD(TRNG2_INTR, ICDONE, 3, 1)
    FIELD(TRNG2_INTR, IECERTF, 2, 1)
    FIELD(TRNG2_INTR, IEDTF, 1, 1)
    FIELD(TRNG2_INTR, IEDONE, 0, 1)
REG32(TRNG2_STAT, 0x1404)
    FIELD(TRNG2_STAT, QCNT, 9, 3)
    FIELD(TRNG2_STAT, CERTF, 3, 1)
    FIELD(TRNG2_STAT, DTF, 1, 1)
    FIELD(TRNG2_STAT, DONE, 0, 1)
REG32(TRNG2_CTRL, 0x1408)
    FIELD(TRNG2_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG2_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG2_CTRL, EUMODE, 8, 1)
    FIELD(TRNG2_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG2_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG2_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG2_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG2_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG2_CTRL, PRNGSRST, 0, 1)
REG32(TRNG2_CONF0, 0x140c)
    FIELD(TRNG2_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG2_CONF0, DIT, 0, 5)
REG32(TRNG2_CONF1, 0x1410)
    FIELD(TRNG2_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG2_CONF1, DLEN, 0, 8)
REG32(TRNG2_TEST, 0x1414)
    FIELD(TRNG2_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG2_XSEED00, 0x1440)
REG32(TRNG2_XSEED01, 0x1444)
REG32(TRNG2_XSEED02, 0x1448)
REG32(TRNG2_XSEED03, 0x144c)
REG32(TRNG2_XSEED04, 0x1450)
REG32(TRNG2_XSEED05, 0x1454)
REG32(TRNG2_XSEED06, 0x1458)
REG32(TRNG2_XSEED07, 0x145c)
REG32(TRNG2_XSEED08, 0x1460)
REG32(TRNG2_XSEED09, 0x1464)
REG32(TRNG2_XSEED10, 0x1468)
REG32(TRNG2_XSEED11, 0x146c)
REG32(TRNG2_PSTR00, 0x1480)
REG32(TRNG2_PSTR01, 0x1484)
REG32(TRNG2_PSTR02, 0x1488)
REG32(TRNG2_PSTR03, 0x148c)
REG32(TRNG2_PSTR04, 0x1490)
REG32(TRNG2_PSTR05, 0x1494)
REG32(TRNG2_PSTR06, 0x1498)
REG32(TRNG2_PSTR07, 0x149c)
REG32(TRNG2_PSTR08, 0x14a0)
REG32(TRNG2_PSTR09, 0x14a4)
REG32(TRNG2_PSTR10, 0x14a8)
REG32(TRNG2_PSTR11, 0x14ac)
REG32(TRNG2_RND, 0x14c0)
REG32(TRNG2_RESET, 0x14d0)
    FIELD(TRNG2_RESET, ASSERT, 0, 1)
REG32(TRNG2_RO_EN, 0x14d4)
    FIELD(TRNG2_RO_EN, ACTIVE, 0, 1)
REG32(TRNG2_AUTOPROC, 0x1500)
    FIELD(TRNG2_AUTOPROC, CODE, 0, 1)
REG32(TRNG2_NRNPS, 0x1508)
    FIELD(TRNG2_NRNPS, NUM, 0, 10)
REG32(TRNG3_INTR, 0x1600)
    FIELD(TRNG3_INTR, ICCERTF, 5, 1)
    FIELD(TRNG3_INTR, ICDTF, 4, 1)
    FIELD(TRNG3_INTR, ICDONE, 3, 1)
    FIELD(TRNG3_INTR, IECERTF, 2, 1)
    FIELD(TRNG3_INTR, IEDTF, 1, 1)
    FIELD(TRNG3_INTR, IEDONE, 0, 1)
REG32(TRNG3_STAT, 0x1604)
    FIELD(TRNG3_STAT, QCNT, 9, 3)
    FIELD(TRNG3_STAT, CERTF, 3, 1)
    FIELD(TRNG3_STAT, DTF, 1, 1)
    FIELD(TRNG3_STAT, DONE, 0, 1)
REG32(TRNG3_CTRL, 0x1608)
    FIELD(TRNG3_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG3_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG3_CTRL, EUMODE, 8, 1)
    FIELD(TRNG3_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG3_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG3_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG3_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG3_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG3_CTRL, PRNGSRST, 0, 1)
REG32(TRNG3_CONF0, 0x160c)
    FIELD(TRNG3_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG3_CONF0, DIT, 0, 5)
REG32(TRNG3_CONF1, 0x1610)
    FIELD(TRNG3_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG3_CONF1, DLEN, 0, 8)
REG32(TRNG3_TEST, 0x1614)
    FIELD(TRNG3_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG3_XSEED00, 0x1640)
REG32(TRNG3_XSEED01, 0x1644)
REG32(TRNG3_XSEED02, 0x1648)
REG32(TRNG3_XSEED03, 0x164c)
REG32(TRNG3_XSEED04, 0x1650)
REG32(TRNG3_XSEED05, 0x1654)
REG32(TRNG3_XSEED06, 0x1658)
REG32(TRNG3_XSEED07, 0x165c)
REG32(TRNG3_XSEED08, 0x1660)
REG32(TRNG3_XSEED09, 0x1664)
REG32(TRNG3_XSEED10, 0x1668)
REG32(TRNG3_XSEED11, 0x166c)
REG32(TRNG3_PSTR00, 0x1680)
REG32(TRNG3_PSTR01, 0x1684)
REG32(TRNG3_PSTR02, 0x1688)
REG32(TRNG3_PSTR03, 0x168c)
REG32(TRNG3_PSTR04, 0x1690)
REG32(TRNG3_PSTR05, 0x1694)
REG32(TRNG3_PSTR06, 0x1698)
REG32(TRNG3_PSTR07, 0x169c)
REG32(TRNG3_PSTR08, 0x16a0)
REG32(TRNG3_PSTR09, 0x16a4)
REG32(TRNG3_PSTR10, 0x16a8)
REG32(TRNG3_PSTR11, 0x16ac)
REG32(TRNG3_RND, 0x16c0)
REG32(TRNG3_RESET, 0x16d0)
    FIELD(TRNG3_RESET, ASSERT, 0, 1)
REG32(TRNG3_RO_EN, 0x16d4)
    FIELD(TRNG3_RO_EN, ACTIVE, 0, 1)
REG32(TRNG3_AUTOPROC, 0x1700)
    FIELD(TRNG3_AUTOPROC, CODE, 0, 1)
REG32(TRNG3_NRNPS, 0x1708)
    FIELD(TRNG3_NRNPS, NUM, 0, 10)
REG32(TRNG4_INTR, 0x1800)
    FIELD(TRNG4_INTR, ICCERTF, 5, 1)
    FIELD(TRNG4_INTR, ICDTF, 4, 1)
    FIELD(TRNG4_INTR, ICDONE, 3, 1)
    FIELD(TRNG4_INTR, IECERTF, 2, 1)
    FIELD(TRNG4_INTR, IEDTF, 1, 1)
    FIELD(TRNG4_INTR, IEDONE, 0, 1)
REG32(TRNG4_STAT, 0x1804)
    FIELD(TRNG4_STAT, QCNT, 9, 3)
    FIELD(TRNG4_STAT, CERTF, 3, 1)
    FIELD(TRNG4_STAT, DTF, 1, 1)
    FIELD(TRNG4_STAT, DONE, 0, 1)
REG32(TRNG4_CTRL, 0x1808)
    FIELD(TRNG4_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG4_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG4_CTRL, EUMODE, 8, 1)
    FIELD(TRNG4_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG4_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG4_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG4_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG4_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG4_CTRL, PRNGSRST, 0, 1)
REG32(TRNG4_CONF0, 0x180c)
    FIELD(TRNG4_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG4_CONF0, DIT, 0, 5)
REG32(TRNG4_CONF1, 0x1810)
    FIELD(TRNG4_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG4_CONF1, DLEN, 0, 8)
REG32(TRNG4_TEST, 0x1814)
    FIELD(TRNG4_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG4_XSEED00, 0x1840)
REG32(TRNG4_XSEED01, 0x1844)
REG32(TRNG4_XSEED02, 0x1848)
REG32(TRNG4_XSEED03, 0x184c)
REG32(TRNG4_XSEED04, 0x1850)
REG32(TRNG4_XSEED05, 0x1854)
REG32(TRNG4_XSEED06, 0x1858)
REG32(TRNG4_XSEED07, 0x185c)
REG32(TRNG4_XSEED08, 0x1860)
REG32(TRNG4_XSEED09, 0x1864)
REG32(TRNG4_XSEED10, 0x1868)
REG32(TRNG4_XSEED11, 0x186c)
REG32(TRNG4_PSTR00, 0x1880)
REG32(TRNG4_PSTR01, 0x1884)
REG32(TRNG4_PSTR02, 0x1888)
REG32(TRNG4_PSTR03, 0x188c)
REG32(TRNG4_PSTR04, 0x1890)
REG32(TRNG4_PSTR05, 0x1894)
REG32(TRNG4_PSTR06, 0x1898)
REG32(TRNG4_PSTR07, 0x189c)
REG32(TRNG4_PSTR08, 0x18a0)
REG32(TRNG4_PSTR09, 0x18a4)
REG32(TRNG4_PSTR10, 0x18a8)
REG32(TRNG4_PSTR11, 0x18ac)
REG32(TRNG4_RND, 0x18c0)
REG32(TRNG4_RESET, 0x18d0)
    FIELD(TRNG4_RESET, ASSERT, 0, 1)
REG32(TRNG4_RO_EN, 0x18d4)
    FIELD(TRNG4_RO_EN, ACTIVE, 0, 1)
REG32(TRNG4_AUTOPROC, 0x1900)
    FIELD(TRNG4_AUTOPROC, CODE, 0, 1)
REG32(TRNG4_NRNPS, 0x1908)
    FIELD(TRNG4_NRNPS, NUM, 0, 10)
REG32(TRNG5_INTR, 0x1a00)
    FIELD(TRNG5_INTR, ICCERTF, 5, 1)
    FIELD(TRNG5_INTR, ICDTF, 4, 1)
    FIELD(TRNG5_INTR, ICDONE, 3, 1)
    FIELD(TRNG5_INTR, IECERTF, 2, 1)
    FIELD(TRNG5_INTR, IEDTF, 1, 1)
    FIELD(TRNG5_INTR, IEDONE, 0, 1)
REG32(TRNG5_STAT, 0x1a04)
    FIELD(TRNG5_STAT, QCNT, 9, 3)
    FIELD(TRNG5_STAT, CERTF, 3, 1)
    FIELD(TRNG5_STAT, DTF, 1, 1)
    FIELD(TRNG5_STAT, DONE, 0, 1)
REG32(TRNG5_CTRL, 0x1a08)
    FIELD(TRNG5_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG5_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG5_CTRL, EUMODE, 8, 1)
    FIELD(TRNG5_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG5_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG5_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG5_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG5_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG5_CTRL, PRNGSRST, 0, 1)
REG32(TRNG5_CONF0, 0x1a0c)
    FIELD(TRNG5_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG5_CONF0, DIT, 0, 5)
REG32(TRNG5_CONF1, 0x1a10)
    FIELD(TRNG5_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG5_CONF1, DLEN, 0, 8)
REG32(TRNG5_TEST, 0x1a14)
    FIELD(TRNG5_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG5_XSEED00, 0x1a40)
REG32(TRNG5_XSEED01, 0x1a44)
REG32(TRNG5_XSEED02, 0x1a48)
REG32(TRNG5_XSEED03, 0x1a4c)
REG32(TRNG5_XSEED04, 0x1a50)
REG32(TRNG5_XSEED05, 0x1a54)
REG32(TRNG5_XSEED06, 0x1a58)
REG32(TRNG5_XSEED07, 0x1a5c)
REG32(TRNG5_XSEED08, 0x1a60)
REG32(TRNG5_XSEED09, 0x1a64)
REG32(TRNG5_XSEED10, 0x1a68)
REG32(TRNG5_XSEED11, 0x1a6c)
REG32(TRNG5_PSTR00, 0x1a80)
REG32(TRNG5_PSTR01, 0x1a84)
REG32(TRNG5_PSTR02, 0x1a88)
REG32(TRNG5_PSTR03, 0x1a8c)
REG32(TRNG5_PSTR04, 0x1a90)
REG32(TRNG5_PSTR05, 0x1a94)
REG32(TRNG5_PSTR06, 0x1a98)
REG32(TRNG5_PSTR07, 0x1a9c)
REG32(TRNG5_PSTR08, 0x1aa0)
REG32(TRNG5_PSTR09, 0x1aa4)
REG32(TRNG5_PSTR10, 0x1aa8)
REG32(TRNG5_PSTR11, 0x1aac)
REG32(TRNG5_RND, 0x1ac0)
REG32(TRNG5_RESET, 0x1ad0)
    FIELD(TRNG5_RESET, ASSERT, 0, 1)
REG32(TRNG5_RO_EN, 0x1ad4)
    FIELD(TRNG5_RO_EN, ACTIVE, 0, 1)
REG32(TRNG5_AUTOPROC, 0x1b00)
    FIELD(TRNG5_AUTOPROC, CODE, 0, 1)
REG32(TRNG5_NRNPS, 0x1b08)
    FIELD(TRNG5_NRNPS, NUM, 0, 10)
REG32(TRNG6_INTR, 0x1c00)
    FIELD(TRNG6_INTR, ICCERTF, 5, 1)
    FIELD(TRNG6_INTR, ICDTF, 4, 1)
    FIELD(TRNG6_INTR, ICDONE, 3, 1)
    FIELD(TRNG6_INTR, IECERTF, 2, 1)
    FIELD(TRNG6_INTR, IEDTF, 1, 1)
    FIELD(TRNG6_INTR, IEDONE, 0, 1)
REG32(TRNG6_STAT, 0x1c04)
    FIELD(TRNG6_STAT, QCNT, 9, 3)
    FIELD(TRNG6_STAT, CERTF, 3, 1)
    FIELD(TRNG6_STAT, DTF, 1, 1)
    FIELD(TRNG6_STAT, DONE, 0, 1)
REG32(TRNG6_CTRL, 0x1c08)
    FIELD(TRNG6_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG6_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG6_CTRL, EUMODE, 8, 1)
    FIELD(TRNG6_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG6_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG6_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG6_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG6_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG6_CTRL, PRNGSRST, 0, 1)
REG32(TRNG6_CONF0, 0x1c0c)
    FIELD(TRNG6_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG6_CONF0, DIT, 0, 5)
REG32(TRNG6_CONF1, 0x1c10)
    FIELD(TRNG6_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG6_CONF1, DLEN, 0, 8)
REG32(TRNG6_TEST, 0x1c14)
    FIELD(TRNG6_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG6_XSEED00, 0x1c40)
REG32(TRNG6_XSEED01, 0x1c44)
REG32(TRNG6_XSEED02, 0x1c48)
REG32(TRNG6_XSEED03, 0x1c4c)
REG32(TRNG6_XSEED04, 0x1c50)
REG32(TRNG6_XSEED05, 0x1c54)
REG32(TRNG6_XSEED06, 0x1c58)
REG32(TRNG6_XSEED07, 0x1c5c)
REG32(TRNG6_XSEED08, 0x1c60)
REG32(TRNG6_XSEED09, 0x1c64)
REG32(TRNG6_XSEED10, 0x1c68)
REG32(TRNG6_XSEED11, 0x1c6c)
REG32(TRNG6_PSTR00, 0x1c80)
REG32(TRNG6_PSTR01, 0x1c84)
REG32(TRNG6_PSTR02, 0x1c88)
REG32(TRNG6_PSTR03, 0x1c8c)
REG32(TRNG6_PSTR04, 0x1c90)
REG32(TRNG6_PSTR05, 0x1c94)
REG32(TRNG6_PSTR06, 0x1c98)
REG32(TRNG6_PSTR07, 0x1c9c)
REG32(TRNG6_PSTR08, 0x1ca0)
REG32(TRNG6_PSTR09, 0x1ca4)
REG32(TRNG6_PSTR10, 0x1ca8)
REG32(TRNG6_PSTR11, 0x1cac)
REG32(TRNG6_RND, 0x1cc0)
REG32(TRNG6_RESET, 0x1cd0)
    FIELD(TRNG6_RESET, ASSERT, 0, 1)
REG32(TRNG6_RO_EN, 0x1cd4)
    FIELD(TRNG6_RO_EN, ACTIVE, 0, 1)
REG32(TRNG6_AUTOPROC, 0x1d00)
    FIELD(TRNG6_AUTOPROC, CODE, 0, 1)
REG32(TRNG6_NRNPS, 0x1d08)
    FIELD(TRNG6_NRNPS, NUM, 0, 10)
REG32(TRNG7_INTR, 0x1e00)
    FIELD(TRNG7_INTR, ICCERTF, 5, 1)
    FIELD(TRNG7_INTR, ICDTF, 4, 1)
    FIELD(TRNG7_INTR, ICDONE, 3, 1)
    FIELD(TRNG7_INTR, IECERTF, 2, 1)
    FIELD(TRNG7_INTR, IEDTF, 1, 1)
    FIELD(TRNG7_INTR, IEDONE, 0, 1)
REG32(TRNG7_STAT, 0x1e04)
    FIELD(TRNG7_STAT, QCNT, 9, 3)
    FIELD(TRNG7_STAT, CERTF, 3, 1)
    FIELD(TRNG7_STAT, DTF, 1, 1)
    FIELD(TRNG7_STAT, DONE, 0, 1)
REG32(TRNG7_CTRL, 0x1e08)
    FIELD(TRNG7_CTRL, PERSODISABLE, 10, 1)
    FIELD(TRNG7_CTRL, SINGLEGENMODE, 9, 1)
    FIELD(TRNG7_CTRL, EUMODE, 8, 1)
    FIELD(TRNG7_CTRL, PRNGMODE, 7, 1)
    FIELD(TRNG7_CTRL, TSTMODE, 6, 1)
    FIELD(TRNG7_CTRL, PRNGSTART, 5, 1)
    FIELD(TRNG7_CTRL, PRNGXS, 3, 1)
    FIELD(TRNG7_CTRL, TRSSEN, 2, 1)
    FIELD(TRNG7_CTRL, PRNGSRST, 0, 1)
REG32(TRNG7_CONF0, 0x1e0c)
    FIELD(TRNG7_CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(TRNG7_CONF0, DIT, 0, 5)
REG32(TRNG7_CONF1, 0x1e10)
    FIELD(TRNG7_CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(TRNG7_CONF1, DLEN, 0, 8)
REG32(TRNG7_TEST, 0x1e14)
    FIELD(TRNG7_TEST, SINGLEBITRAW, 0, 1)
REG32(TRNG7_XSEED00, 0x1e40)
REG32(TRNG7_XSEED01, 0x1e44)
REG32(TRNG7_XSEED02, 0x1e48)
REG32(TRNG7_XSEED03, 0x1e4c)
REG32(TRNG7_XSEED04, 0x1e50)
REG32(TRNG7_XSEED05, 0x1e54)
REG32(TRNG7_XSEED06, 0x1e58)
REG32(TRNG7_XSEED07, 0x1e5c)
REG32(TRNG7_XSEED08, 0x1e60)
REG32(TRNG7_XSEED09, 0x1e64)
REG32(TRNG7_XSEED10, 0x1e68)
REG32(TRNG7_XSEED11, 0x1e6c)
REG32(TRNG7_PSTR00, 0x1e80)
REG32(TRNG7_PSTR01, 0x1e84)
REG32(TRNG7_PSTR02, 0x1e88)
REG32(TRNG7_PSTR03, 0x1e8c)
REG32(TRNG7_PSTR04, 0x1e90)
REG32(TRNG7_PSTR05, 0x1e94)
REG32(TRNG7_PSTR06, 0x1e98)
REG32(TRNG7_PSTR07, 0x1e9c)
REG32(TRNG7_PSTR08, 0x1ea0)
REG32(TRNG7_PSTR09, 0x1ea4)
REG32(TRNG7_PSTR10, 0x1ea8)
REG32(TRNG7_PSTR11, 0x1eac)
REG32(TRNG7_RND, 0x1ec0)
REG32(TRNG7_RESET, 0x1ed0)
    FIELD(TRNG7_RESET, ASSERT, 0, 1)
REG32(TRNG7_RO_EN, 0x1ed4)
    FIELD(TRNG7_RO_EN, ACTIVE, 0, 1)
REG32(TRNG7_AUTOPROC, 0x1f00)
    FIELD(TRNG7_AUTOPROC, CODE, 0, 1)
REG32(TRNG7_NRNPS, 0x1f08)
    FIELD(TRNG7_NRNPS, NUM, 0, 10)

/*
 * Instance-relative REG32 for common handling for 8 TRNG instances.
 *
 * For bit-field access, use TRNG_FIELD_DP32/TRNG_FIELD_EX32 or TRNG0.
 */
#define TRNG_OFFSET(N) (A_TRNG0_ ## N - A_TRNG0_INTR)

REG32(TRNG_INTR, TRNG_OFFSET(INTR))
REG32(TRNG_STAT, TRNG_OFFSET(STAT))
REG32(TRNG_CTRL, TRNG_OFFSET(CTRL))
REG32(TRNG_CONF0, TRNG_OFFSET(CONF0))
REG32(TRNG_CONF1, TRNG_OFFSET(CONF1))
REG32(TRNG_TEST, TRNG_OFFSET(TEST))
REG32(TRNG_XSEED00, TRNG_OFFSET(XSEED00))
REG32(TRNG_XSEED01, TRNG_OFFSET(XSEED01))
REG32(TRNG_XSEED02, TRNG_OFFSET(XSEED02))
REG32(TRNG_XSEED03, TRNG_OFFSET(XSEED03))
REG32(TRNG_XSEED04, TRNG_OFFSET(XSEED04))
REG32(TRNG_XSEED05, TRNG_OFFSET(XSEED05))
REG32(TRNG_XSEED06, TRNG_OFFSET(XSEED06))
REG32(TRNG_XSEED07, TRNG_OFFSET(XSEED07))
REG32(TRNG_XSEED08, TRNG_OFFSET(XSEED08))
REG32(TRNG_XSEED09, TRNG_OFFSET(XSEED09))
REG32(TRNG_XSEED10, TRNG_OFFSET(XSEED10))
REG32(TRNG_XSEED11, TRNG_OFFSET(XSEED11))
REG32(TRNG_PSTR00, TRNG_OFFSET(PSTR00))
REG32(TRNG_PSTR01, TRNG_OFFSET(PSTR01))
REG32(TRNG_PSTR02, TRNG_OFFSET(PSTR02))
REG32(TRNG_PSTR03, TRNG_OFFSET(PSTR03))
REG32(TRNG_PSTR04, TRNG_OFFSET(PSTR04))
REG32(TRNG_PSTR05, TRNG_OFFSET(PSTR05))
REG32(TRNG_PSTR06, TRNG_OFFSET(PSTR06))
REG32(TRNG_PSTR07, TRNG_OFFSET(PSTR07))
REG32(TRNG_PSTR08, TRNG_OFFSET(PSTR08))
REG32(TRNG_PSTR09, TRNG_OFFSET(PSTR09))
REG32(TRNG_PSTR10, TRNG_OFFSET(PSTR10))
REG32(TRNG_PSTR11, TRNG_OFFSET(PSTR11))
REG32(TRNG_RND, TRNG_OFFSET(RND))
REG32(TRNG_RESET, TRNG_OFFSET(RESET))
REG32(TRNG_RO_EN, TRNG_OFFSET(RO_EN))
REG32(TRNG_AUTOPROC, TRNG_OFFSET(AUTOPROC))
REG32(TRNG_NRNPS, TRNG_OFFSET(NRNPS))

#define TRNG_R_MAX (R_TRNG_NRNPS + 1)

#define TRNG_R_BASE(N, s) (&s->regs[R_TRNG ## N ## _INTR])

#define TRNG_A_BASE(N)    (A_TRNG ## N ## _INTR)
#define TRNG_A_LAST(N)    (TRNG_A_BASE(N) + TRNG_R_MAX * 4 - 1)
#define TRNG_A_SIZE       (TRNG_A_BASE(1) - TRNG_A_BASE(0))

#define TRNG_FIELD_EX32(regs, reg, field) \
    FIELD_EX32((regs)[R_TRNG_ ## reg], TRNG0_ ## reg, field)

#define TRNG_FIELD_DP32(regs, reg, field, val) \
    (regs)[R_TRNG_ ## reg] = FIELD_DP32((regs)[R_TRNG_ ## reg],         \
                                        TRNG0_ ## reg, field, val)

enum {
    PSX_PKI_RNG_FIFO_DEPTH = 4,
    PSX_PKI_RNG_FIFO_READ_SIZE  = 256 / 8,
    PSX_PKI_RNG_CTRLSTAT_OFFSET = 0x10000,
    PSX_PKI_RNG_CTRLSTAT_R_MAX  = R_TRNG7_NRNPS + 1,
    PSX_PKI_RNG_IOMEM_MAX       = PSX_PKI_RNG_CTRLSTAT_OFFSET
                                  + 4 * PSX_PKI_RNG_CTRLSTAT_R_MAX,
    PSX_PKI_RNG_DRNG_TOTAL      = ARRAY_SIZE(((XlnxPsxPkiRng *)0)->drng),
};

QEMU_BUILD_BUG_ON(PSX_PKI_RNG_CTRLSTAT_R_MAX
                  != ARRAY_SIZE(((XlnxPsxPkiRng *)0)->regs_info));

static void intr_update_irq(XlnxPsxPkiRng *s)
{
    bool pending = s->regs[R_INTR_STS] & ~s->regs[R_INTR_MASK];
    qemu_set_irq(s->irq_intr, pending);
}

static void intr_sts_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg->opaque);
    intr_update_irq(s);
}

static uint64_t intr_en_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_INTR_MASK] &= ~val;
    intr_update_irq(s);
    return 0;
}

static uint64_t intr_dis_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_INTR_MASK] |= val;
    intr_update_irq(s);
    return 0;
}

static uint64_t intr_trig_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg->opaque);
    uint32_t val = val64;

    s->regs[R_INTR_STS] |= val;
    intr_update_irq(s);
    return 0;
}

static void intr_update_nrnavail(XlnxPsxPkiRng *s)
{
    uint32_t avail = ARRAY_FIELD_EX32(s->regs, NRN_AVAIL, NUM);
    uint32_t thresh = ARRAY_FIELD_EX32(s->regs, NRN_THRESH, VAL);

    ARRAY_FIELD_DP32(s->regs, INTR_STS, NRNAVAIL, !!(avail >= thresh));
    intr_update_irq(s);
}

static void nrn_avail_update(XlnxPsxPkiRng *s, int change)
{
    int a_max = FIELD_EX32(UINT32_MAX, NRN_AVAIL, NUM);
    int avail = ARRAY_FIELD_EX32(s->regs, NRN_AVAIL, NUM);

    avail += change;
    if (avail < 0) {
        avail = 0;
    } else if (avail > a_max) {
        avail = a_max;
    }

    ARRAY_FIELD_DP32(s->regs, NRN_AVAIL, NUM, avail);
    intr_update_nrnavail(s);
}

static void nrn_thresh_postw(RegisterInfo *reg, uint64_t val)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg->opaque);

    intr_update_nrnavail(s);
}

static bool pki_drng_is_auto_drng(XlnxPsxPkiDrng *rng)
{
    return rng->id == PSX_PKI_RNG_DRNG_TOTAL;
}

static XlnxPsxPkiRng *pki_drng_container(XlnxPsxPkiDrng *rng)
{
    if (pki_drng_is_auto_drng(rng)) {
        return container_of(rng, XlnxPsxPkiRng, auto_drng);
    }

    assert(rng->id < PSX_PKI_RNG_DRNG_TOTAL);
    rng -= rng->id;

    return container_of(rng, XlnxPsxPkiRng, drng[0]);
}

static unsigned pki_drng_iregs(unsigned rng_id)
{
    assert(rng_id < PSX_PKI_RNG_DRNG_TOTAL);
    return R_TRNG0_INTR + rng_id * (TRNG_A_SIZE / 4);
}

static uint32_t *pki_drng_regs(XlnxPsxPkiDrng *rng)
{
    XlnxPsxPkiRng *s;

    s = pki_drng_container(rng);
    return &s->regs[pki_drng_iregs(rng->id)];
}

static RegisterInfo *pki_drng_regs_info(XlnxPsxPkiDrng *rng)
{
    XlnxPsxPkiRng *s;

    s = pki_drng_container(rng);
    return &s->regs_info[pki_drng_iregs(rng->id)];
}

static void pki_drng_irq_set(XlnxPsxPkiDrng *rng, uint32_t e)
{
    XlnxPsxPkiRng *s = pki_drng_container(rng);
    uint32_t intr_mask = e << rng->id;

    s->regs[R_INTR_STS] |= intr_mask;
    intr_update_irq(s);
}

static void pki_drng_int_notify(XlnxPsxPkiDrng *rng)
{
    pki_drng_irq_set(rng, R_INTR_STS_TRNG0INT_MASK);
}

static void pki_drng_ac_notify(XlnxPsxPkiDrng *rng)
{
    pki_drng_irq_set(rng, R_INTR_STS_TRNG0AC_MASK);
}

static void pki_drng_stat_done(XlnxPsxPkiDrng *rng)
{
    uint32_t *regs = pki_drng_regs(rng);

    TRNG_FIELD_DP32(regs, STAT, DONE, 1);

    if (TRNG_FIELD_EX32(regs, INTR, IEDONE)) {
        pki_drng_int_notify(rng);
    }
}

static void pki_drng_stat_qcnt(XlnxPsxPkiDrng *rng, unsigned qcnt)
{
    uint32_t *regs = pki_drng_regs(rng);
    uint32_t v_old = TRNG_FIELD_EX32(regs, STAT, QCNT);

    if (qcnt > 4) {
        qcnt -= 4;
    }

    TRNG_FIELD_DP32(regs, STAT, QCNT, qcnt);

    if (qcnt != 4) {
        TRNG_FIELD_DP32(regs, STAT, DONE, 0);
    } else if (v_old != 4) {
        TRNG_FIELD_DP32(regs, STAT, DONE, 1);
        pki_drng_stat_done(rng);
    }
}

static void pki_drng_iseed_384(XlnxPsxPkiDrng *rng, uint8_t *s384)
{
    XlnxPsxPkiRng *s = pki_drng_container(rng);
    struct {
        uint8_t id;
        uint64_t counter;
        uint64_t nonce[2];
    } *hdr = (void *)s384;

    memset(s384, 0, (384 / 8));

    /*
     * Simulate internal entropy source with counter-mode PRNG.
     * Use host-indepedent endian for consistent guest sequence.
     *
     * Add instance id to be unique, and non-zero leading byte.
     * Add counter to ensure unique seeding in each round.
     */
    rng->iseed_counter++;
    hdr->id = rng->id + 1;
    hdr->counter = cpu_to_be64(rng->iseed_counter);

    /* non-zero 'iseed-nonce' prop selects reproducible PRNG sequence */
    if (s->iseed_nonce) {
        hdr->nonce[0] = cpu_to_be64(s->iseed_nonce);
    } else {
        hdr->nonce[0] = cpu_to_be64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        hdr->nonce[1] = cpu_to_be64(getpid());
    }

    /* Non-zero last bit */
    s384[(384 / 8) - 1] = 0x1;
}

static void pki_drng_count(XlnxPsxPkiDrng *rng)
{
    uint64_t *c = &rng->state.counter;
    uint64_t v = be64_to_cpu(*c) + 1;

    *c = cpu_to_be64(v);
}

static void pki_drng_regen(XlnxPsxPkiDrng *rng)
{
    uint8_t *rnd = (uint8_t *)(rng->random);
    size_t rnd_len = sizeof(rng->random);

    rng->rnd_get = 0;
    qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA256,
                       (char *)&rng->state, sizeof(rng->state),
                       &rnd, &rnd_len, &error_abort);
    pki_drng_count(rng);
}

static void pki_drng_reseed(XlnxPsxPkiDrng *rng)
{
    bool prng;
    uint8_t *seed;
    size_t seed_len;
    size_t nr = 0;
    uint32_t *salt = NULL;
    union {
        uint32_t u32[(384 / 32) * 2];
        uint8_t  u8[1];
        char     ch[1];
    } data;

    if (pki_drng_is_auto_drng(rng)) {
        prng = false;
    } else {
        uint32_t *regs = pki_drng_regs(rng);

        prng = !!TRNG_FIELD_EX32(regs, CTRL, PRNGXS);

        if (prng) {
            salt = &regs[R_TRNG_XSEED00];
        } else if (!TRNG_FIELD_EX32(regs, CTRL, PERSODISABLE)) {
            salt = &regs[R_TRNG_PSTR00];
        }
    }

    if (salt) {
        /* host-independent endian for consistent guest sequence */
        for (nr = 0; nr < 12; nr++) {
            data.u32[nr] = cpu_to_be32(salt[nr]);
        }
        nr *= 4;
    }

    if (!prng) {
        pki_drng_iseed_384(rng, &data.u8[nr]);
        nr += 384 / 8;
    }

    /*
     * Create initial state for counter-mode CSPRNG.
     * Non-zero counter indicating seeding completed.
     */
    rng->state.counter = 0;
    pki_drng_count(rng);

    seed = (uint8_t *)rng->state.seed;
    seed_len = sizeof(rng->state.seed);
    qcrypto_hash_bytes(QCRYPTO_HASH_ALG_SHA384,
                       data.ch, nr, &seed, &seed_len, &error_abort);

    /* Invalidate generation buffer */
    rng->rnd_get = ARRAY_SIZE(rng->random);
}

static void pki_drng_generate(XlnxPsxPkiDrng *rng)
{
    /* Generate next number and raise DONE intr to indicate ok to read RND */
    pki_drng_regen(rng);
    pki_drng_stat_qcnt(rng, ARRAY_SIZE(rng->random));
}

static unsigned pki_drng_avail(XlnxPsxPkiDrng *rng)
{
    int n = (ssize_t)ARRAY_SIZE(rng->random) - (ssize_t)rng->rnd_get;

    return n > 0 ? n : 0;
}

static void pki_drng_autoproc_reset(XlnxPsxPkiRng *s)
{
    s->regs[R_NRN_AVAIL] = 0;
    s->auto_members = 0;

    memset(&s->auto_drng, 0, sizeof(s->auto_drng));
    s->auto_drng.id = PSX_PKI_RNG_DRNG_TOTAL;
    pki_drng_reseed(&s->auto_drng);
}

static void pki_drng_autoproc_enter(XlnxPsxPkiDrng *rng)
{
    XlnxPsxPkiRng *s = pki_drng_container(rng);
    uint32_t mask = 1 << rng->id;

    /* FIFO depth is simulated as always full */
    if (!(s->auto_members & mask)) {
        nrn_avail_update(s, PSX_PKI_RNG_FIFO_DEPTH);
    }

    s->auto_members |= mask;
}

static void pki_drng_autoproc_leave(XlnxPsxPkiDrng *rng)
{
    XlnxPsxPkiRng *s = pki_drng_container(rng);
    uint32_t mask = 1 << rng->id;

    if (s->auto_members & mask) {
        nrn_avail_update(s, -PSX_PKI_RNG_FIFO_DEPTH);
    }

    s->auto_members &= ~mask;
}

static void pki_drng_reset(XlnxPsxPkiDrng *rng)
{
    RegisterInfo *regs_info = pki_drng_regs_info(rng);
    size_t k = offsetof(XlnxPsxPkiDrng, rnd_get);
    unsigned i;

    pki_drng_autoproc_leave(rng);
    memset((char *)rng + k, 0, (sizeof(rng->random) - k));

    for (i = 0; i < TRNG_R_MAX; i++) {
        register_reset(&regs_info[i]);
    }
}

static void pki_drng_ctrl_on_set(XlnxPsxPkiDrng *rng, uint32_t mask)
{
    uint32_t *regs = pki_drng_regs(rng);

    if (FIELD_EX32(mask, TRNG0_CTRL, PRNGSRST)) {
        pki_drng_reset(rng);
        return;
    }

    if (FIELD_EX32(mask, TRNG0_CTRL, PRNGSTART)) {
        if (!FIELD_EX32(regs[R_TRNG_CTRL], TRNG0_CTRL, PRNGMODE)) {
            pki_drng_reseed(rng);
            pki_drng_stat_done(rng);
        } else {
            pki_drng_generate(rng);
        }
    }
}

static void pki_drng_ctrl_on_clear(XlnxPsxPkiDrng *rng, uint32_t mask)
{
    if (FIELD_EX32(mask, TRNG0_CTRL, PRNGSTART)) {
        pki_drng_stat_qcnt(rng, 0);
        rng->rnd_get = ARRAY_SIZE(rng->random);
    }
}

static int pki_drng_id(hwaddr addr, bool *auto_check)
{
    unsigned id;

    if (addr < TRNG_A_BASE(0) || addr > TRNG_A_LAST(7)) {
        if (auto_check) {
            *auto_check = false;
        }
        return -1;
    }

    addr -= TRNG_A_BASE(0);

    if (auto_check) {
        unsigned offset = addr % TRNG_A_SIZE;

        /* True if register is subject to auto-proc access restriction */
        *auto_check = offset < (A_TRNG_RND + 4);
    }

    id = addr / TRNG_A_SIZE;
    assert(id < PSX_PKI_RNG_DRNG_TOTAL);

    return (int)id;
}

static void pki_drng_init_regs_info(XlnxPsxPkiRng *s)
{
    unsigned nr;

    for (nr = 0; nr < PSX_PKI_RNG_DRNG_TOTAL; nr++) {
        s->drng[nr].id = nr;
    }

    /*
     * Point TRNG reg context to DRNG owner instead of container
     * for pki_drng_dev_of() to pick up.
     */
    for (nr = 0; nr < ARRAY_SIZE(s->regs_info); nr++) {
        RegisterInfo *reg = &s->regs_info[nr];
        int id;

        if (!reg->access) {
            continue;
        }

        id = pki_drng_id(reg->access->addr, NULL);
        if (id < 0) {
            continue;
        }

        reg->opaque = &s->drng[id];
    }
}

static XlnxPsxPkiDrng *pki_drng_dev_of(RegisterInfo *reg)
{
    XlnxPsxPkiDrng *rng = reg->opaque;

    assert(rng);
    assert(reg->access);
    assert(rng->id == pki_drng_id(reg->access->addr, NULL));

    return rng;
}

static uint32_t *pki_drng_base_of(RegisterInfo *reg)
{
    XlnxPsxPkiDrng *rng = pki_drng_dev_of(reg);

    return pki_drng_regs(rng);
}

static uint32_t pki_drng_val_of(RegisterInfo *reg)
{
    return *(uint32_t *)reg->data;
}

static uint64_t pki_drng_wo_reg_postr(RegisterInfo *reg, uint64_t val)
{
    return 0;
}

static uint64_t pki_drng_intr_prew(RegisterInfo *reg, uint64_t val64)
{
    uint32_t *regs = pki_drng_base_of(reg);
    uint32_t r_sta = regs[R_TRNG_STAT];
    uint32_t v_new = val64;

    if (FIELD_EX32(v_new, TRNG0_INTR, ICCERTF)) {
        v_new = FIELD_DP32(v_new, TRNG0_INTR, ICCERTF, 0);
        r_sta = FIELD_DP32(r_sta, TRNG0_STAT, CERTF, 0);
    }
    if (FIELD_EX32(v_new, TRNG0_INTR, ICDTF)) {
        v_new = FIELD_DP32(v_new, TRNG0_INTR, ICDTF, 0);
        r_sta = FIELD_DP32(r_sta, TRNG0_STAT, DTF, 0);
    }
    if (FIELD_EX32(v_new, TRNG0_INTR, ICDONE)) {
        v_new = FIELD_DP32(v_new, TRNG0_INTR, ICDONE, 0);
        r_sta = FIELD_DP32(r_sta, TRNG0_STAT, DONE, 0);
    }

    regs[R_TRNG_STAT] = r_sta;

    return v_new;
}

static uint64_t pki_drng_stat_post_read(RegisterInfo *reg, uint64_t val)
{
    uint32_t *regs = pki_drng_base_of(reg);

    /* Reads targeted at multiple addresses are dispatched here */
    return regs[R_TRNG_STAT];
}

static uint64_t pki_drng_ctrl_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiDrng *rng = pki_drng_dev_of(reg);
    uint32_t *regs = pki_drng_regs(rng);
    uint32_t v_new = val64;
    uint32_t v_old, s_mask, c_mask;

    /* Update reg to simplify implementing ctrl actions */
    v_old = regs[R_TRNG_CTRL];
    regs[R_TRNG_CTRL] = v_new;

    /* Act on 0->1 transition */
    s_mask = ~v_old & v_new;
    if (s_mask) {
        pki_drng_ctrl_on_set(rng, s_mask);
    }

    /* Act on 1->0 transition */
    c_mask = v_old & ~v_new;
    if (c_mask) {
        pki_drng_ctrl_on_clear(rng, c_mask);
    }

    return regs[R_TRNG_CTRL];
}

static uint64_t pki_drng_rnd_post_read(RegisterInfo *reg, uint64_t val)
{
    XlnxPsxPkiDrng *rng = pki_drng_dev_of(reg);
    uint32_t rnd, qcnt;

    if (!rng->state.counter) {
        return 0;   /* Seeding was not done */
    }

    qcnt = pki_drng_avail(rng);
    if (!qcnt) {
        return 0;
    }

    rnd = rng->random[rng->rnd_get++];
    qcnt--;

    if (qcnt) {
        pki_drng_stat_qcnt(rng, qcnt);
    } else {
        uint32_t *regs = pki_drng_regs(rng);

        if (TRNG_FIELD_EX32(regs, CTRL, SINGLEGENMODE)) {
            TRNG_FIELD_DP32(regs, CTRL, PRNGSTART, 0);
            pki_drng_ctrl_on_clear(rng, R_TRNG0_CTRL_PRNGSTART_MASK);
        } else {
            pki_drng_generate(rng);
        }
    }

    return rnd;
}

static uint64_t pki_drng_reset_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiDrng *rng = pki_drng_dev_of(reg);
    uint32_t v_old = FIELD_EX32(pki_drng_val_of(reg), TRNG0_RESET, ASSERT);
    uint32_t v_new = FIELD_EX32(val64, TRNG0_RESET, ASSERT);

    if (!v_old && v_new) {
        pki_drng_reset(rng);
    }

    return v_new;
}

static uint64_t pki_drng_autoproc_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPsxPkiDrng *rng = pki_drng_dev_of(reg);
    uint32_t v_old = FIELD_EX32(pki_drng_val_of(reg), TRNG0_AUTOPROC, CODE);
    uint32_t v_new = FIELD_EX32(val64, TRNG0_AUTOPROC, CODE);

    if (!v_old && v_new) {
        pki_drng_autoproc_enter(rng);
    }
    if (v_old && !v_new) {
        uint32_t *regs = pki_drng_regs(rng);

        regs[R_TRNG_CTRL] = 0;
        pki_drng_ctrl_on_clear(rng, R_TRNG0_CTRL_PRNGSTART_MASK);

        pki_drng_autoproc_leave(rng);
        pki_drng_ac_notify(rng);
    }

    return val64;
}

/*
 * RegisterAccessInfo template for one instance of TRNG.
 */
#define TRNG_REG_INFO_NAME_ADDR(N, L) \
    .name = "TRNG" #N #L, .addr = A_TRNG ## N ## L

#define TRNG_REG_INFO_SEED(N, L, M)        \
    {  TRNG_REG_INFO_NAME_ADDR(N, L ## M), \
       .post_read = pki_drng_stat_post_read,   \
    }

#define TRNG_REG_INFO_XSEED(N, M) \
    TRNG_REG_INFO_SEED(N, _XSEED, M)

#define TRNG_REG_INFO_PSTR(N, M) \
    TRNG_REG_INFO_SEED(N, _PSTR, M)

#define TRNG_REG_INFO(N) \
    {   TRNG_REG_INFO_NAME_ADDR(N, _INTR), \
        .pre_write = pki_drng_intr_prew, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _STAT), \
        .ro = 0xffffffff, \
        .post_read = pki_drng_stat_post_read, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _CTRL), \
        .pre_write = pki_drng_ctrl_prew, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _CONF0), \
        .reset = 0x210c, \
        .post_read = pki_drng_wo_reg_postr, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _CONF1), \
        .reset = 0x26409, \
        .post_read = pki_drng_wo_reg_postr, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _TEST), \
        .post_read = pki_drng_wo_reg_postr, \
    }, \
    TRNG_REG_INFO_XSEED(N, 00), \
    TRNG_REG_INFO_XSEED(N, 01), \
    TRNG_REG_INFO_XSEED(N, 02), \
    TRNG_REG_INFO_XSEED(N, 03), \
    TRNG_REG_INFO_XSEED(N, 04), \
    TRNG_REG_INFO_XSEED(N, 05), \
    TRNG_REG_INFO_XSEED(N, 06), \
    TRNG_REG_INFO_XSEED(N, 07), \
    TRNG_REG_INFO_XSEED(N, 08), \
    TRNG_REG_INFO_XSEED(N, 09), \
    TRNG_REG_INFO_XSEED(N, 10), \
    TRNG_REG_INFO_XSEED(N, 11), \
    TRNG_REG_INFO_PSTR(N, 00), \
    TRNG_REG_INFO_PSTR(N, 01), \
    TRNG_REG_INFO_PSTR(N, 02), \
    TRNG_REG_INFO_PSTR(N, 03), \
    TRNG_REG_INFO_PSTR(N, 04), \
    TRNG_REG_INFO_PSTR(N, 05), \
    TRNG_REG_INFO_PSTR(N, 06), \
    TRNG_REG_INFO_PSTR(N, 07), \
    TRNG_REG_INFO_PSTR(N, 08), \
    TRNG_REG_INFO_PSTR(N, 09), \
    TRNG_REG_INFO_PSTR(N, 10), \
    TRNG_REG_INFO_PSTR(N, 11), \
    {   TRNG_REG_INFO_NAME_ADDR(N, _RND), \
        .ro = 0xffffffff, \
        .post_read = pki_drng_rnd_post_read, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _RESET), \
        .reset = 0x1, \
        .pre_write = pki_drng_reset_prew, \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _RO_EN), \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _AUTOPROC), \
        .pre_write = pki_drng_autoproc_prew,  \
    },{ TRNG_REG_INFO_NAME_ADDR(N, _NRNPS), \
    }

static const RegisterAccessInfo psx_pki_rng_regs_info[] = {
    {   .name = "GENRL_CTRL",  .addr = A_GENRL_CTRL,
        .reset = 0x121,
    },{ .name = "NRN_AVAIL",  .addr = A_NRN_AVAIL,
        .ro = 0xffffffff,
    },{ .name = "NRN_THRESH",  .addr = A_NRN_THRESH,
        .ro = ~R_NRN_THRESH_VAL_MASK,
        .reset = 0x10,
        .post_write = nrn_thresh_postw,
    },{ .name = "INTR_STS",  .addr = A_INTR_STS,
        .rsvd = 0xfeff0000,
        .w1c = 0x100ffff,
        .post_write = intr_sts_postw,
    },{ .name = "INTR_EN",  .addr = A_INTR_EN,
        .pre_write = intr_en_prew,
    },{ .name = "INTR_DIS",  .addr = A_INTR_DIS,
        .pre_write = intr_dis_prew,
    },{ .name = "INTR_MASK",  .addr = A_INTR_MASK,
        .reset = 0x100ffff,
        .ro = 0x100ffff,
    },{ .name = "INTR_TRIG",  .addr = A_INTR_TRIG,
        .pre_write = intr_trig_prew,
    },{ .name = "SAFETY_CHK",  .addr = A_SAFETY_CHK,
    },

    TRNG_REG_INFO(0),
    TRNG_REG_INFO(1),
    TRNG_REG_INFO(2),
    TRNG_REG_INFO(3),
    TRNG_REG_INFO(4),
    TRNG_REG_INFO(5),
    TRNG_REG_INFO(6),
    TRNG_REG_INFO(7),
};

static void psx_pki_rng_reset_enter(Object *obj, ResetType type)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(obj);
    unsigned int i;

    if (!s->dirty) {
        return;  /* avoid slow-down from repeated resets */
    }

    pki_drng_autoproc_reset(s);

    for (i = 0; psx_pki_rng_regs_info[i].addr < A_TRNG0_INTR; ++i) {
        register_reset(&s->regs_info[i]);
    }

    for (i = 0; i < ARRAY_SIZE(s->drng); ++i) {
        pki_drng_reset(&s->drng[i]);
    }

    s->dirty = false;
}

static void psx_pki_rng_reset_hold(Object *obj)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(obj);

    intr_update_irq(s);
}

static MemTxResult psx_pki_rng_fifo_read(void *opaque, hwaddr addr,
                                         uint64_t *data, unsigned size,
                                         MemTxAttrs attrs)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(opaque);
    void *rnd;

    assert(size <= PSX_PKI_RNG_FIFO_READ_SIZE);
    s->dirty = true;

    if (addr >= PSX_PKI_RNG_CTRLSTAT_OFFSET
        || size > (PSX_PKI_RNG_CTRLSTAT_OFFSET - addr)) {
        memset(data, 0, size);
        return MEMTX_DECODE_ERROR;
    }

    /* Return all 0 if no DRNG instance is in auto-proc mode */
    if (!s->auto_members) {
        goto ret_null;
    }

    /*
     * 'byte-fifo' prop turns on QEMU-specific debug for fifo to
     * be read on any size, so long as addr is aligned on size.
     *
     * If off, return all 0 on unaligned address or unsupported size,
     * as in real-hardware.
     */
    if (s->byte_fifo) {
        if (!QEMU_IS_ALIGNED(addr, size)) {
            goto ret_null;
        }
        rnd = (uint8_t *)s->auto_drng.random
              + (addr % PSX_PKI_RNG_FIFO_READ_SIZE);

        if (QEMU_IS_ALIGNED(addr, PSX_PKI_RNG_FIFO_READ_SIZE)) {
            pki_drng_regen(&s->auto_drng);
        }
    } else {
        if (size != PSX_PKI_RNG_FIFO_READ_SIZE
            || !QEMU_IS_ALIGNED(addr, PSX_PKI_RNG_FIFO_READ_SIZE)) {
            goto ret_null;
        }
        rnd = s->auto_drng.random;
        pki_drng_regen(&s->auto_drng);
    }

    /*
     * Because real hardware's seeding are truly random in auto-proc mode,
     * the auto-proc mode only FIFO model needs not be deterministic.  Thus,
     * simplify the model by using a single DRNG state. Reseeding period is
     * ignored, given no need for the model to be cryptographically strong.
     */
    memcpy(data, rnd, size);

    return MEMTX_OK;

ret_null:
    memset(data, 0, size);
    return MEMTX_OK;
}

static MemTxResult psx_pki_rng_fifo_write(void *opaque, hwaddr addr,
                                          uint64_t data, unsigned size,
                                          MemTxAttrs attrs)
{
    if (addr >= PSX_PKI_RNG_CTRLSTAT_OFFSET) {
        return MEMTX_DECODE_ERROR;
    }

    /* Writes are silently ignored */
    return MEMTX_OK;
}

static bool psx_pki_rng_auto_is_on(XlnxPsxPkiRng *s, hwaddr addr)
{
    bool auto_check;
    int drng_id;

    drng_id = pki_drng_id(addr, &auto_check);
    if (auto_check) {
        uint32_t mask = 1 << drng_id;

        return !!(s->auto_members & mask);
    }

    return false;
}

static uint64_t psx_pki_rng_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg_array->r[0]->opaque);

    if (psx_pki_rng_auto_is_on(s, addr)) {
        /* As in real hardware, auto-proc mode DRNG silently ignores reads */
        return 0;
    }

    s->dirty = true;
    return register_read_memory(opaque, addr, size);
}

static void psx_pki_rng_regs_write(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(reg_array->r[0]->opaque);

    if (psx_pki_rng_auto_is_on(s, addr)) {
        /* As in real hardware, auto-proc mode DRNG silently ignores writes */
        return;
    }

    s->dirty = true;
    register_write_memory(opaque, addr, value, size);
}

static const MemoryRegionOps psx_pki_rng_fifo_ops = {
    .read_with_attrs = psx_pki_rng_fifo_read,
    .write_with_attrs = psx_pki_rng_fifo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = PSX_PKI_RNG_FIFO_READ_SIZE,
    },
};

static const MemoryRegionOps psx_pki_rng_regs_ops = {
    .read = psx_pki_rng_regs_read,
    .write = psx_pki_rng_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void psx_pki_rng_realize(DeviceState *dev, Error **errp)
{
    if (!qcrypto_hash_supports(QCRYPTO_HASH_ALG_SHA384)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(dev));

        error_setg(errp, "%s: Need QCRYPTO_HASH_ALG_SHA384 support", path);
        return;
    }
    if (!qcrypto_hash_supports(QCRYPTO_HASH_ALG_SHA256)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(dev));

        error_setg(errp, "%s: Need QCRYPTO_HASH_ALG_SHA256 support", path);
        return;
    }
}

static void psx_pki_rng_init(Object *obj)
{
    XlnxPsxPkiRng *s = XLNX_PSX_PKI_RNG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    s->dirty = true;

    reg_array =
        register_init_block32(DEVICE(obj), psx_pki_rng_regs_info,
                              ARRAY_SIZE(psx_pki_rng_regs_info),
                              s->regs_info, s->regs,
                              &psx_pki_rng_regs_ops,
                              XLNX_PSX_PKI_RNG_ERR_DEBUG,
                              PSX_PKI_RNG_CTRLSTAT_R_MAX * 4);
    pki_drng_init_regs_info(s);

    memory_region_init_io(&s->iomem, obj, &psx_pki_rng_fifo_ops,
                          s, TYPE_XLNX_PSX_PKI_RNG,
                          PSX_PKI_RNG_IOMEM_MAX);
    memory_region_add_subregion(&s->iomem, PSX_PKI_RNG_CTRLSTAT_OFFSET,
                                &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_intr);
}

static const VMStateDescription vmstate_psx_pki_rng = {
    .name = TYPE_XLNX_PSX_PKI_RNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxPsxPkiRng, PSX_PKI_RNG_CTRLSTAT_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property psx_pki_rng_props[] = {
    DEFINE_PROP_UINT64("iseed-nonce", XlnxPsxPkiRng,
                       iseed_nonce, 0xcafebeef1badf00dULL),
    DEFINE_PROP_BOOL("byte-fifo", XlnxPsxPkiRng, byte_fifo, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void psx_pki_rng_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_psx_pki_rng;
    dc->realize = psx_pki_rng_realize;
    rc->phases.enter = psx_pki_rng_reset_enter;
    rc->phases.hold = psx_pki_rng_reset_hold;

    device_class_set_props(dc, psx_pki_rng_props);
}

static const TypeInfo psx_pki_rng_info = {
    .name          = TYPE_XLNX_PSX_PKI_RNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxPsxPkiRng),
    .class_init    = psx_pki_rng_class_init,
    .instance_init = psx_pki_rng_init,
};

static void psx_pki_rng_register_types(void)
{
    type_register_static(&psx_pki_rng_info);
}

type_init(psx_pki_rng_register_types)
