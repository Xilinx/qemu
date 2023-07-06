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

typedef enum {
    DME_GET             = 0x1,
    DME_SET             = 0x2,
    DME_PEER_GET        = 0x3,
    DME_PEER_SET        = 0x4,
    DME_POWERON         = 0x10,
    DME_POWEROFF        = 0x11,
    DME_ENABLE          = 0x12,
    DME_RESET           = 0x14,
    DME_ENDPOINTRESET   = 0x15,
    DME_LINKSTARTUP     = 0x16,
    DME_HIBERNATE_ENTER = 0x17,
    DME_HIBERNATE_EXIT  = 0x18,
    DME_TEST_MODE       = 0x1a,
} dmeCmd;

typedef enum {
    DME_SUCCESS                     = 0x0,
    DME_INVALID_MIB_ATTRIBUTE       = 0x1,
    DME_INVALID_MIB_ATTRIBUTE_VALUE = 0x2,
    DME_READ_ONLY_MIB_ATTRIBUTE     = 0x3,
    DME_WRITE_ONLY_MIB_ATTRIBUTE    = 0x4,
    DME_BAD_INDEX                   = 0x5,
    DME_LOCKED_MIB_ATTRIBUTE        = 0x6,
    DME_BAD_TEST_FEATURE_INDEX      = 0x7,
    DME_PEER_COMMUNICATION_FAILURE  = 0x8,
    DME_BUSY                        = 0x9,
    DME_FAILURE                     = 0xa,
} CfgResultCode;

typedef enum {
    PWR_OK          = 0x0,
    PWR_LOCAL       = 0x01,
    PWR_REMOTE      = 0x02,
    PWR_BUSY        = 0x03,
    PWR_ERROR_CAP   = 0x04,
    PWR_FATAL_ERROR = 0x05,
} upmcrs;

typedef struct ufshcIFClass {
     InterfaceClass parent;

     /*
      * SCSI specifc handlers
      */
     void (*handle_upiu)(ufshcIF *ifs, upiu_pkt *pkt);
     void (*handle_data)(ufshcIF *ifs, void *data, uint16_t len,
                         uint8_t task_tag);
     QEMUSGList *(*get_sgl)(ufshcIF *ifs, uint8_t task_tag);
     /*
      * Unipro specific handlers
      */
     CfgResultCode (*dme_cmd)(ufshcIF *ifs, dmeCmd cmd, uint16_t MIBattr,
                    uint16_t GenSel, uint32_t *data);
     void (*pwr_mode_status)(ufshcIF *ifs, upmcrs status);

} ufshcIFClass;

/*
 * SCSI specific functions
 */
void ufshci_send_upiu(ufshcIF *ifs, upiu_pkt *pkt);
void ufshci_send_data(ufshcIF *ifs, void *data, uint16_t len, uint8_t task_tag);
QEMUSGList *ufshci_get_sgl(ufshcIF *ifs, uint8_t task_tag);
CfgResultCode ufshci_dme_cmd(ufshcIF *ifs, dmeCmd cmd, uint16_t MIBattr,
                             uint16_t GenSel, uint32_t *data);
void ufshci_pwr_mode_status(ufshcIF *ifs, upmcrs status);

#define TYPE_UFS_BUS "ufs-bus"
#define UFS_BUS(obj) OBJECT_CHECK(ufsBus, (obj), TYPE_UFS_BUS)
typedef struct ufsBus {
    BusState qbus;
} ufsBus;
#endif
