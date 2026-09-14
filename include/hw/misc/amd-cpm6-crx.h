/*
 * CPM6 Clock and Reset Control Registers
 *
 * Copyright Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AMD_CPM6_CRX_H
#define HW_MISC_AMD_CPM6_CRX_H

#include "hw/sysbus.h"

#define TYPE_AMD_CPM6_CRX "amd-cpm6-crx"
OBJECT_DECLARE_SIMPLE_TYPE(AmdCpm6CrxState, AMD_CPM6_CRX)

#define AMD_CPM6_CRX_MMIO_LEN 0x360

struct AmdCpm6CrxState {
    SysBusDevice parent_obj;

    /* registers */
    uint32_t cpll_ctrl;
    uint32_t cpll_cfg;

    MemoryRegion iomem;
};

#endif
