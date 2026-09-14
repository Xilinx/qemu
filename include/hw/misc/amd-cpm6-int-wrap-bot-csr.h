/*
 * Includes Interrupt,error and Isolation registers
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_CPM6_INT_WRAP_BOT_CSR_H
#define HW_MISC_CPM6_INT_WRAP_BOT_CSR_H

#include "hw/sysbus.h"

#define TYPE_CPM6_INT_WRAP_BOT_CSR "amd-cpm6-int-wrap-bot-csr"
OBJECT_DECLARE_SIMPLE_TYPE(Cpm6IntWrapBotCsrState, CPM6_INT_WRAP_BOT_CSR)

#define CPM6_INT_WRAP_BOT_CSR_MMIO_LEN 0xfc30

struct Cpm6IntWrapBotCsrState {
    SysBusDevice parent_obj;

    /* registers */
    uint32_t eco_0;
    uint32_t eco_1;
    uint32_t scratch[10];

    MemoryRegion iomem;
};

#endif
