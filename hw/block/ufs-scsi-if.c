/*
 * UFS SCSI Interface
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/block/ufs-scsi-if.h"

void ufs_scsi_if_handle_scsi(ufs_scsi_if *ifs, void *pkt, uint32_t size,
                             uint8_t tag, uint8_t lun)
{
    ufs_scsi_if_class *k = UFS_SCSI_IF_GET_CLASS(ifs);

    if (k->handle_scsi) {
        k->handle_scsi(ifs, pkt, size, tag, lun);
    }
}

uint32_t ufs_scsi_if_handle_data(ufs_scsi_if *ifs, uint8_t *data, uint32_t size,
                             uint8_t tag)
{
    ufs_scsi_if_class *k = UFS_SCSI_IF_GET_CLASS(ifs);

    if (k->handle_data) {
        return k->handle_data(ifs, data, size, tag);
    }
    return 0;
}

void ufs_scsi_if_handle_sense(ufs_scsi_if *ifs, uint8_t *sense, uint32_t len,
                              uint8_t tag)
{
    ufs_scsi_if_class *k = UFS_SCSI_IF_GET_CLASS(ifs);

    if (k->handle_sense) {
        k->handle_sense(ifs, sense, len, tag);
    }
}

bool ufs_scsi_read_capacity10(ufs_scsi_if *ifs, uint8_t lun, uint8_t *rbuf)
{
    ufs_scsi_if_class *k = UFS_SCSI_IF_GET_CLASS(ifs);

    if (k->read_capacity10) {
        return k->read_capacity10(ifs, lun, rbuf);
    }
    return false;
}

QEMUSGList * ufs_scsi_if_get_sgl(ufs_scsi_if *ifs, uint8_t tag, uint8_t lun)
{
    ufs_scsi_if_class *k = UFS_SCSI_IF_GET_CLASS(ifs);
    if (k->get_sgl) {
        return k->get_sgl(ifs, tag, lun);
    }
    return NULL;
}

static const TypeInfo ufs_scsi_dev_info = {
    .name          = TYPE_UFS_SCSI_IF,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(ufs_scsi_if_class),
};

static void ufs_scsi_if_types(void)
{
    type_register_static(&ufs_scsi_dev_info);
}

type_init(ufs_scsi_if_types);
