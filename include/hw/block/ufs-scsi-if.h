/*
 * UFS SCSI Interface
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UFS_SCSI_IF_H
#define UFS_SCSI_IF_H

#include "qom/object.h"
#include "sysemu/dma.h"

#define TYPE_UFS_SCSI_IF "ufs-scsi-if"
typedef struct ufs_scsi_if_class ufs_scsi_if_class;
DECLARE_CLASS_CHECKERS(ufs_scsi_if_class, UFS_SCSI_IF, TYPE_UFS_SCSI_IF)

#define UFS_SCSI_IF(obj) \
    INTERFACE_CHECK(ufs_scsi_if, (obj), TYPE_UFS_SCSI_IF)

typedef struct ufs_scsi_if {
    Object Parent;
} ufs_scsi_if;

typedef struct ufs_scsi_if_class {
     InterfaceClass parent;

     void (*handle_scsi)(ufs_scsi_if *ifs, void *pkt, uint32_t size,
                         uint8_t tag, uint8_t lun);
     uint32_t (*handle_data)(ufs_scsi_if *ifs, uint8_t *data, uint32_t size,
                          uint8_t tag);
     void (*handle_sense)(ufs_scsi_if *ifs, uint8_t *sense, uint32_t len,
                         size_t residual, uint8_t tag);
     bool (*read_capacity10)(ufs_scsi_if *ifs, uint8_t lun, uint8_t *rbuf);
     QEMUSGList *(* get_sgl)(ufs_scsi_if *ifs, uint8_t tag,
                                        uint8_t lun);
} ufs_scsi_if_class;

void ufs_scsi_if_handle_scsi(ufs_scsi_if *ifs, void *pkt, uint32_t size,
                            uint8_t tag, uint8_t lun);
uint32_t ufs_scsi_if_handle_data(ufs_scsi_if *ifs, uint8_t *data, uint32_t size,
                             uint8_t tag);
void ufs_scsi_if_handle_sense(ufs_scsi_if *ifs, uint8_t *sense, uint32_t len,
                              size_t residual, uint8_t tag);
bool ufs_scsi_read_capacity10(ufs_scsi_if *ifs, uint8_t lun, uint8_t *rbuf);

QEMUSGList *ufs_scsi_if_get_sgl(ufs_scsi_if *ifs, uint8_t tag, uint8_t lun);
#endif
