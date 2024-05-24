/*
 * QEMU model of AMD/Xilinx ASU True Random Number Generator
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XLNX_VERSAL_ASU_TRNG_H
#define XLNX_VERSAL_ASU_TRNG_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/misc/xlnx-trng1-r2.h"

#define TYPE_XLNX_ASU_TRNG "xlnx-asu-trng"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxAsuTRng, XLNX_ASU_TRNG)

#define R_MAX_XLNX_ASU_TRNG_CTL ((0x34 / 4) + 1)

typedef struct XlnxAsuTRng {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_intr;

    XlnxTRng1r2 trng;

    uint32_t regs[R_MAX_XLNX_ASU_TRNG_CTL];
    RegisterInfo regs_info[R_MAX_XLNX_ASU_TRNG_CTL];
} XlnxAsuTRng;

#undef R_MAX_XLNX_ASU_TRNG_CTL

#endif
