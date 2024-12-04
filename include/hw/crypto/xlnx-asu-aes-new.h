/*
 * ASU AES Engine
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

#ifndef HW_CRYPTO_XILINX_ASU_AES_H
#define HW_CRYPTO_XILINX_ASU_AES_H

#include "hw/sysbus.h"
#include "hw/stream.h"

#define TYPE_XILINX_ASU_AES "xlnx.asu-aes"
OBJECT_DECLARE_TYPE(XilinxAsuAesState, XilinxAsuAesClass, XILINX_ASU_AES)

#define XILINX_ASU_AES_MMIO_LEN 0x238

#define ASU_AES_BLOCK_SIZE 16
typedef uint8_t QEMU_ALIGNED(16) AsuAesBlock[ASU_AES_BLOCK_SIZE];

typedef struct XilinxAsuKvState XilinxAsuKvState;

struct XilinxAsuAesState {
    SysBusDevice parent_obj;

    /* == registers == */
    uint32_t iv_in[4];
    uint32_t iv_mask_in[4];
    uint32_t mac_out[4];
    uint32_t int_mac_in[4];
    uint32_t int_mac_mask_in[4];
    uint32_t s0_in[4];
    uint32_t s0_mask_in[4];
    uint32_t gcmlen_in[4];
    uint32_t split_cfg;
    bool reset;
    bool ready;
    bool cm_enabled;

    /* == internal state == */
    struct {
        uint8_t key[32];
        AsuAesBlock iv;
        AsuAesBlock mac;
        AsuAesBlock s0_gcmlen; /* used for CCM S0 and GCM "len(A) || len(C)" */
        uint32_t key_size;
    } aes_ctx;

    MemoryRegion iomem;
    qemu_irq irq;

    XilinxAsuKvState *kv;
};

struct XilinxAsuAesClass {
    SysBusDeviceClass parent;
};

#endif
