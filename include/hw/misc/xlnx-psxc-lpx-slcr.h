/*
 * QEMU model of the XlnxPsxcLpxSlcr Global system level control registers for
 * the low power domain
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
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

#ifndef HW_MISC_XLNX_PSXC_LPX_SLCR_H
#define HW_MISC_XLNX_PSXC_LPX_SLCR_H

#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/arm/pchannel.h"

#define TYPE_XILINX_PSXC_LPX_SLCR "xlnx.psxc-lpx-slcr"

#define XILINX_PSXC_LPX_SLCR(obj) \
     OBJECT_CHECK(XlnxPsxcLpxSlcr, (obj), TYPE_XILINX_PSXC_LPX_SLCR)

#define PSXC_LPX_SLCR_MMIO_SIZE 0x600f4

typedef struct XlnxPsxcLpxSlcrCorePowerCtrl {
    qemu_irq pwr;

    uint32_t reg0;
    uint32_t reg1;
    uint32_t reg2;
    uint32_t wprot;
} XlnxPsxcLpxSlcrCorePowerCtrl;

typedef struct XlnxPsxcLpxSlcrIrq {
    uint32_t status;
    uint32_t mask;
} XlnxPsxcLpxSlcrIrq;

typedef struct XlnxPsxcLpxSlcrRpuPChannel {
    XlnxPsxcLpxSlcrIrq irq; /* IRQ when PACTIVE[1] is set (core on) */
    ARMPChannelIf *iface;
    bool preq;
    uint32_t pstate;
    uint32_t pactive;
} XlnxPsxcLpxSlcrRpuPChannel;

typedef struct XlnxPsxcLpxSlcr {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    qemu_irq ocm_pwr[16];
    qemu_irq rpu_tcm_pwr[10];
    qemu_irq gem_pwr[2];

    qemu_irq pwr_reset_irq;

    XlnxPsxcLpxSlcrCorePowerCtrl core_pwr[18];
    XlnxPsxcLpxSlcrRpuPChannel rpu_pcil_pchan[10];

    uint32_t ocm_pwr_ctrl;
    uint32_t rpu_tcm_pwr_ctrl;
    uint32_t gem_pwr_ctrl;

    XlnxPsxcLpxSlcrIrq wakeup0_irq;
    XlnxPsxcLpxSlcrIrq wakeup1_irq;
    XlnxPsxcLpxSlcrIrq power_dwn_irq;
    XlnxPsxcLpxSlcrIrq pwr_rst_irq;
    XlnxPsxcLpxSlcrIrq req_pwrup0_irq;
    XlnxPsxcLpxSlcrIrq req_pwrup1_irq;
    XlnxPsxcLpxSlcrIrq req_pwrdwn0_irq;
    XlnxPsxcLpxSlcrIrq req_pwrdwn1_irq;
    XlnxPsxcLpxSlcrIrq rpu_pcil_wfi_irq;

    uint32_t num_rpu;
} XlnxPsxcLpxSlcr;

#endif
