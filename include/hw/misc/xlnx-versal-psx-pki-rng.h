/*
 * QEMU model of the random number generation (RNG) in Xilinx
 * Public Key Infrastructure subsystem.
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
#ifndef XLNX_PSX_PKI_RNG_H
#define XLNX_PSX_PKI_RNG_H

#include "hw/qdev-core.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#define RMAX_XLNX_PSX_PKI_RNG ((0x1F08 / 4) + 1)

#define TYPE_XLNX_PSX_PKI_RNG "xlnx,psx-pki-rng"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxPsxPkiRng, XLNX_PSX_PKI_RNG)

typedef struct XlnxPsxPkiDrng {
    uint64_t iseed_counter;
    uint8_t  id;
    /* The following zeros on soft-reset */
    uint8_t  rnd_get;
    uint32_t random[256 / 32];
    struct {
        uint32_t seed[384 / 32];
        uint64_t counter;
    } state;
} XlnxPsxPkiDrng;

typedef struct XlnxPsxPkiRng {
    SysBusDevice parent_obj;
    qemu_irq irq_intr;

    uint64_t iseed_nonce;
    XlnxPsxPkiDrng drng[8];
    XlnxPsxPkiDrng auto_drng;
    uint32_t auto_members;
    bool byte_fifo;
    bool dirty;

    MemoryRegion iomem;
    uint32_t regs[RMAX_XLNX_PSX_PKI_RNG];
    RegisterInfo regs_info[RMAX_XLNX_PSX_PKI_RNG];
} XlnxPsxPkiRng;

#endif
