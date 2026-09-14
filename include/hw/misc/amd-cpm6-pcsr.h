/*
 * This block implements the standard PCSR interface for Reset/Initialization of
 * CPM
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AMD_CPM6_PCSR_H
#define HW_MISC_AMD_CPM6_PCSR_H

#include "hw/sysbus.h"

#define TYPE_AMD_CPM6_PCSR "amd-cpm6-pcsr"
OBJECT_DECLARE_SIMPLE_TYPE(AmdCpm6PcsrState, AMD_CPM6_PCSR)

#define AMD_CPM6_PCSR_MMIO_LEN 0x34

struct AmdCpm6PcsrState {
    SysBusDevice parent_obj;

    /* registers */
    uint32_t mask;
    uint32_t pcr;
    bool lock;

    MemoryRegion iomem;
};

#endif
