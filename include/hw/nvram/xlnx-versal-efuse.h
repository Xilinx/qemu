/*
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
#ifndef XLNX_VERSAL_EFUSE_H
#define XLNX_VERSAL_EFUSE_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/zynqmp_aes_key.h"
#include "hw/nvram/xlnx-efuse.h"

#define XLNX_VERSAL_EFUSE_CTRL_R_MAX ((0x100 / 4) + 1)

#define TYPE_XLNX_VERSAL_EFUSE_CTRL  "xlnx.versal-efuse"
#define TYPE_XLNX_VERSAL_EFUSE_CACHE "xlnx.pmc-efuse-cache"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalEFuseCtrl, XLNX_VERSAL_EFUSE_CTRL);
OBJECT_DECLARE_SIMPLE_TYPE(XlnxVersalEFuseCache, XLNX_VERSAL_EFUSE_CACHE);

struct XlnxVersalEFuseCtrl {
    SysBusDevice parent_obj;
    qemu_irq irq_efuse_imr;

    XlnxEFuse *efuse;
    ZynqMPAESKeySink *aes_key_sink;
    ZynqMPAESKeySink *usr_key0_sink;
    ZynqMPAESKeySink *usr_key1_sink;

    void *extra_pg0_lock_spec;      /* Opaque property */
    uint32_t extra_pg0_lock_n16;

    uint32_t regs[XLNX_VERSAL_EFUSE_CTRL_R_MAX];
    RegisterInfo regs_info[XLNX_VERSAL_EFUSE_CTRL_R_MAX];
};

struct XlnxVersalEFuseCache {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    XlnxEFuse *efuse;
};

#endif
