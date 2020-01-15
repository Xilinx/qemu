/*
 * QEMU model of Xilinx PUF Syndrome Bits
 *
 * Copyright (c) 2020 Xilinx Inc.
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
#ifndef XLNX_ZYNQMP_PUFSYN_H
#define XLNX_ZYNQMP_PUFSYN_H

#include "qemu/osdep.h"
#include "hw/block/xlnx-efuse.h"
#include "hw/zynqmp_aes_key.h"

/*
 * API Definition for XilSKey's XSK_ZYNQMP_CSU_PUF_ list of mailbox registers.
 *
 * See also:
 *  https://github.com/Xilinx/embeddedsw/blob/d70f3cd/lib/sw_services/xilskey/src/include/xilskey_eps_zynqmp_puf.h
 */
enum Zynqmp_PUF_Ops {
    /* Commands */
    PUF_CMD_REGISTRATION = 1,
    PUF_CMD_REGENERATION = 4,
    PUF_CMD_DEBUG_2 = 5,        /* XilSKey_Puf_Debug2() */
    PUF_CMD_RESET = 6,          /* XAPP-1333, puf_user_data.h */

    /* Status bits */
    PUF_STATUS_WRD_RDY = 0x01,
    PUF_STATUS_KEY_RDY = 0x08,
    PUF_STATUS_AUX_SHIFT = 4,

    /* Supported configs */
    PUF_CFG0_VALUE = 2,
    PUF_CFG1_4K_MODE = 0x0c230090,
};

typedef struct Zynqmp_PUFRegen {
    enum {
        Zynqmp_PUFRegen_BUFFR,      /* e.g., filled from BOOT.BIN file */
        Zynqmp_PUFRegen_EFUSE,
    } source;

    union {
        struct {
            XLNXEFuse *dev;
            uint32_t  base_row;
        } efuse;
        struct {
            const uint8_t *base;
            uint32_t u8_cnt;
        } buffr;
    };
} Zynqmp_PUFRegen;

typedef struct Zynqmp_PUFHD Zynqmp_PUFHD; /* Opaque */

Zynqmp_PUFHD *zynqmp_pufhd_new(ZynqMPAESKeySink *puf_keysink);
void zynqmp_pufhd_next(Zynqmp_PUFHD *s, uint32_t *word, uint32_t *status);
bool zynqmp_pufhd_regen(const Zynqmp_PUFRegen *data,
                        ZynqMPAESKeySink *keysink, uint32_t *c_hash);

#endif
