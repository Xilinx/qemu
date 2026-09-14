/*
 * gtmpw_control_core_gtmpw_ublaze_top
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AMD_CPM6_GTMPW_NPI_H
#define HW_MISC_AMD_CPM6_GTMPW_NPI_H

#include "hw/sysbus.h"

#define TYPE_AMD_CPM6_GTMPW_NPI "amd-cpm6-gtmpw-npi"
OBJECT_DECLARE_SIMPLE_TYPE(AmdCpm6GtmpwNpiState, AMD_CPM6_GTMPW_NPI)

#define AMD_CPM6_GTMPW_NPI_MMIO_LEN 0x8774

struct AmdCpm6GtmpwNpiState {
    SysBusDevice parent_obj;

    /* registers */
    bool lock;
    uint32_t pcsr_control;
    uint32_t pcsr_mask;

    MemoryRegion iomem;
};

#endif
