/*
 * UFSHC Interface
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/block/ufshc-if.h"

void ufshci_send_upiu(ufshcIF *ifs, upiu_pkt *pkt)
{
    ufshcIFClass *k = UFSHC_IF_GET_CLASS(ifs);

    k->handle_upiu(ifs, pkt);
}

void ufshci_send_data(ufshcIF *ifs, void *data, uint16_t len, uint8_t task_tag)
{
    ufshcIFClass *k = UFSHC_IF_GET_CLASS(ifs);

    k->handle_data(ifs, data, len, task_tag);
}

QEMUSGList *ufshci_get_sgl(ufshcIF *ifs, uint8_t task_tag)
{
    ufshcIFClass *k = UFSHC_IF_GET_CLASS(ifs);

    if (k->get_sgl) {
         return k->get_sgl(ifs, task_tag);
    }
    return NULL;
}

CfgResultCode ufshci_dme_cmd(ufshcIF *ifs, dmeCmd cmd, uint16_t MIBattr,
                            uint16_t GenSel, uint32_t *data)
{
    ufshcIFClass *k = UFSHC_IF_GET_CLASS(ifs);

    if (k->dme_cmd) {
        return k->dme_cmd(ifs, cmd, MIBattr, GenSel, data);
    }
    return DME_FAILURE;
}

void ufshci_pwr_mode_status(ufshcIF *ifs, upmcrs status)
{
    ufshcIFClass *k = UFSHC_IF_GET_CLASS(ifs);

    if (k->pwr_mode_status) {
        k->pwr_mode_status(ifs, status);
    }
}

static const TypeInfo ufshc_if_dev_info = {
    .name          = TYPE_UFSHC_IF,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(ufshcIFClass),
};

static const TypeInfo ufs_bus_info = {
    .name = TYPE_UFS_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ufsBus),
};

static void ufshc_if_types(void)
{
    type_register_static(&ufshc_if_dev_info);
    type_register_static(&ufs_bus_info);
}

type_init(ufshc_if_types)
