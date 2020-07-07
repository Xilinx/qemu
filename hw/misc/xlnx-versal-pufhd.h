/*
 * QEMU model of Xilinx PUF Syndrome Bits for Versal
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
#ifndef XLNX_VERSAL_PUFHD_H
#define XLNX_VERSAL_PUFHD_H

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "hw/block/xlnx-efuse.h"
#include "hw/zynqmp_aes_key.h"

typedef struct Versal_PUFHD Versal_PUFHD; /* Opaque */

typedef struct {                    /* all fields in cpu endian */
    uint32_t aux;
    uint32_t c_hash;
    uint32_t puf_id[8];
    bool     id_only;
} Versal_PUFExtra;

typedef struct {
    enum {
        Versal_PUFRegen_EFUSE,      /* helper-data from efuse */
        Versal_PUFRegen_MEM,        /* helper-data from guest memory */
    } source;

    union {
        struct {
            XLNXEFuse *dev;
            uint32_t  base_row;
        } efuse;
        struct {
            AddressSpace *as;
            MemTxAttrs attr;
            hwaddr addr;
        } mem;
    };

    Versal_PUFExtra info;
} Versal_PUFRegen;

Versal_PUFHD *versal_pufhd_new(ZynqMPAESKeySink *puf_keysink, bool is_12k);
bool versal_pufhd_next(Versal_PUFHD *s, uint32_t *word, Versal_PUFExtra *info);
bool versal_pufhd_regen(Versal_PUFRegen *data, ZynqMPAESKeySink *keysink);

#endif
