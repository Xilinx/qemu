/*
 * Xilinx ASU ECC
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Author: Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CRYPTO_XILINX_ASU_ECC_H
#define HW_CRYPTO_XILINX_ASU_ECC_H

#include "hw/sysbus.h"

#define XILINX_ASU_ECC_MMIO_SIZE 0x350
#define XILINX_ASU_ECC_MEM_SIZE 84

#define TYPE_XILINX_ASU_ECC "xlnx.asu_ecc"
OBJECT_DECLARE_SIMPLE_TYPE(XilinxAsuEccState, XILINX_ASU_ECC)

struct XilinxAsuEccState {
    SysBusDevice parent_obj;

    uint32_t ctrl;
    uint32_t status;
    uint32_t cfg;
    uint32_t isr;
    uint32_t imr;

    uint32_t mem[XILINX_ASU_ECC_MEM_SIZE];
    bool reset;

    qemu_irq irq;
    MemoryRegion iomem;
};

#endif
