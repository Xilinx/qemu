/*
 * QEMU model of the Xilinx zynqmp sdhci.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ZYNQMP_SDHCI_H
#define ZYNQMP_SDHCI_H

#include "hw/sd/sd.h"
#include "hw/sd/sdhci.h"

#define TYPE_ZYNQMP_SDHCI "xilinx.zynqmp-sdhci"

#define ZYNQMP_SDHCI(obj) \
     OBJECT_CHECK(ZynqMPSDHCIState, (obj), TYPE_ZYNQMP_SDHCI)

#define ZYNQMP_SDHCI_PARENT_CLASS \
    object_class_get_parent(object_class_by_name(TYPE_ZYNQMP_SDHCI))

typedef struct ZynqMPSDHCIState {
    /*< private >*/
    SDHCIState parent_obj;

    /*< public >*/
    SDState *card;
    uint8_t drive_index;
    uint8_t emmc_bw;
    bool is_mmc;
} ZynqMPSDHCIState;

#endif
