/*
 * UFSHC Interface
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef UFSHC_IF_H
#define UFSHC_IF_H

#include "qom/object.h"
#include "hw/block/ufs-upiu.h"
#include "hw/qdev-core.h"
#include "sysemu/dma.h"

#define TYPE_UFSHC_IF "ufshc-if"
typedef struct ufshcIFClass ufshcIFClass;
DECLARE_CLASS_CHECKERS(ufshcIFClass, UFSHC_IF, TYPE_UFSHC_IF)

#define UFSHC_IF(obj) \
    INTERFACE_CHECK(ufshcIF, (obj), TYPE_UFSHC_IF)

typedef struct ufshcIF {
    Object Parent;
} ufshcIF;

typedef struct ufshcIFClass {
     InterfaceClass parent;

     /*
      * SCSI specifc handlers
      */
     void (*handle_upiu)(ufshcIF *ifs, upiu_pkt *pkt);
     void (*handle_data)(ufshcIF *ifs, void *data, uint16_t len,
                         uint8_t task_tag);
     QEMUSGList *(*get_sgl)(ufshcIF *ifs, uint8_t task_tag);
} ufshcIFClass;

/*
 * SCSI specific functions
 */
void ufshci_send_upiu(ufshcIF *ifs, upiu_pkt *pkt);
void ufshci_send_data(ufshcIF *ifs, void *data, uint16_t len, uint8_t task_tag);
QEMUSGList *ufshci_get_sgl(ufshcIF *ifs, uint8_t task_tag);


#define TYPE_UFS_BUS "ufs-bus"
#define UFS_BUS(obj) OBJECT_CHECK(ufsBus, (obj), TYPE_UFS_BUS)
typedef struct ufsBus {
    BusState qbus;
} ufsBus;
#endif
