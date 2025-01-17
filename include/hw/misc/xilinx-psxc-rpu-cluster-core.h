/*
 * PSX and PSXC RPU core control registers
 *
 * This model covers versal-net and versal2 versions and can be used for both
 * core 0 and core 1 (one instance per core). Core 1 has the slsplit input GPIO
 * connected while core 0 hasn't.
 *
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

#ifndef HW_MISC_XILINX_PSXC_RPU_CLUSTER_CORE_H
#define HW_MISC_XILINX_PSXC_RPU_CLUSTER_CORE_H

#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_XILINX_PSXC_RPU_CLUSTER_CORE "xlnx.psxc-rpu-cluster-core"
OBJECT_DECLARE_SIMPLE_TYPE(XilinxPsxcRpuClusterCoreState,
                           XILINX_PSXC_RPU_CLUSTER_CORE)

#define XILINX_PSXC_RPU_CLUSTER_CORE_MMIO_LEN 0x204

enum {
    XILINX_PSXC_RPU_CLUSTER_CORE_VERSAL_NET = 0,
    XILINX_PSXC_RPU_CLUSTER_CORE_VERSAL2 = 1
};

struct XilinxPsxcRpuClusterCoreState {
    SysBusDevice parent_obj;

    /* registers */
    uint32_t cfg0;
    uint32_t cfg1;
    uint32_t vectable;
    bool pwrdwn;

    /* internal state (corresponding input GPIO latch) */
    bool cpu_rst;
    bool slsplit;

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq halt;
    qemu_irq thumb;

    uint32_t version;
    DeviceState *core;
    MemoryRegion *tcm_mr;
};

#endif
