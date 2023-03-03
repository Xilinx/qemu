/*
 * UFS SCSI Device
 * Based on JESD220E
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UFS_SCSI_CORE_H
#define UFS_SCSI_CORE_H

#include "hw/scsi/scsi.h"

#define TYPE_UFS_SCSI_CORE "ufs-scsi-core"
#define UFS_SCSI_CORE(obj) \
        OBJECT_CHECK(UFSScsiCore, (obj), TYPE_UFS_SCSI_CORE)

typedef struct UFSScsiTask {
    SCSIRequest *req;
    uint32_t buf_size;
    uint32_t buf_off;
    int32_t data_size;
    QTAILQ_ENTRY(UFSScsiTask) link;
} UFSScsiTask;

typedef struct UFSScsiCore {
    DeviceState dev;
    SCSIBus bus;

    uint8_t rc10_resp[8];

    ufs_scsi_if *ufs_scsi_ini;
    QTAILQ_HEAD(, UFSScsiTask) taskQ;
} UFSScsiCore;

#endif
