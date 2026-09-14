/*
 * Includes Interrupt,error and Isolation registers
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_CPM6_INT_WRAP_TOP_CSR_H
#define HW_MISC_CPM6_INT_WRAP_TOP_CSR_H

#include "hw/sysbus.h"

#define TYPE_CPM6_INT_WRAP_TOP_CSR "amd-cpm6-int-wrap-top-csr"
OBJECT_DECLARE_SIMPLE_TYPE(Cpm6IntWrapTopCsrState, CPM6_INT_WRAP_TOP_CSR)

#define CPM6_INT_WRAP_TOP_CSR_MMIO_LEN 0x1004

struct Cpm6IntWrapTopCsrState {
    SysBusDevice parent_obj;

    /* registers */
    uint32_t eco_0;
    uint32_t eco_1;
    uint32_t scratch[10];

    MemoryRegion iomem;
};

#endif
