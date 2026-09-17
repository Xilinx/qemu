/*
 * Xilinx PMXC Key Transfer Remote-Port Transport
 *
 * Bridges the PMXC key transfer interface across remote-port for
 * inter-QEMU ASU key transfer.
 *
 * Copyright Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef XLNX_PMXC_KEY_TRANSFER_RP_H
#define XLNX_PMXC_KEY_TRANSFER_RP_H

#include "qom/object.h"

#define TYPE_XLNX_PMXC_KEY_TRANSFER_RP "xlnx-pmxc-key-transfer-rp"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxPmxcKeyTransferRp, XLNX_PMXC_KEY_TRANSFER_RP)

#define XLNX_PMXC_KT_KEY_SIZE 32  /* 256-bit keys */

#endif /* XLNX_PMXC_KEY_TRANSFER_RP_H */
