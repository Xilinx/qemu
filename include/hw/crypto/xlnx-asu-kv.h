/*
 * Xilinx ASU keyvault
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

#ifndef HW_CRYPTO_XILINX_ASU_KV_H
#define HW_CRYPTO_XILINX_ASU_KV_H

#include "hw/sysbus.h"
#include "hw/crypto/xlnx-pmxc-key-transfer.h"

#define TYPE_XILINX_ASU_KV "xlnx.asu-kv"
OBJECT_DECLARE_TYPE(XilinxAsuKvState, XilinxAsuKvClass, XILINX_ASU_KV)

#define XILINX_ASU_KV_MMIO_LEN 0x1bc

struct XilinxAsuKvState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
};

struct XilinxAsuKvClass {
    SysBusDeviceClass parent_class;
};

#endif
