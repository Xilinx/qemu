/*
 * UFS controller
 * Based on JESD223
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UFS_UTP_H
#define UFS_UTP_H

#include "hw/block/ufs-upiu.h"
#include "hw/register.h"

/*
 * UTP Header
 * Header format is same for tranfer request &
 * Task Management request descriptors.
 */
REG32(UTP_DW0, 0x0)
    FIELD(UTP_DW0, I, 24, 1)
    FIELD(UTP_DW0, DD, 25, 2)
    FIELD(UTP_DW0, CT, 28, 4)
REG32(UTP_DW1, 0x4)
REG32(UTP_DW2, 0x8)
    FIELD(UTP_DW2, OCS, 0, 8)
REG32(UTP_DW3, 0xC)

/*
 * UTP Transfer Request fiels
 */
REG32(UTPTR_DW4, 0x10)
    FIELD(UTPTR_DW4, UCDBA, 0, 32)
REG32(UTPTR_DW5, 0x14)
    FIELD(UTPTR_DW5, UCDBAU, 0, 32)
REG32(UTPTR_DW6, 0x18)
    FIELD(UTPTR_DW6, RUL, 0, 16)
    FIELD(UTPTR_DW6, RUO, 16, 16)
REG32(UTPTR_DW7, 0x1C)
    FIELD(UTPTR_DW7, PRDTL, 0, 16)
    FIELD(UTPTR_DW7, PRDTO, 16, 16)

/*
 * UTP PRDT
 */
REG32(PRDT_DW0, 0x0)
    FIELD(PRDT_DW0, DBA, 0, 32)
REG32(PRDT_DW1, 0x4)
    FIELD(PRDT_DW1, DBAU, 0, 32)
REG32(PRDT_DW2, 0x8)
REG32(PRDT_DW3, 0xC)
    FIELD(PRDT_DW3, DBC, 0, 18)

/*
 * UTP Task Managment Request
 */
#define UTPTMR_UPIU_OFFSET 0x10
#define UTPTMR_RESP_UPIU_OFFSET 0x30

#define UTP_OCS_SUCCESS 0x0
#define UTP_OCS_INVALID_COMMAND_TABLE_ATTRIBUTES 0x1
#define UTP_OCS_INVALID_PRDT_ATTRIBUTES 0x2
#define UTP_OCS_MISMATCH_DATA_BUFFER_SIZE 0x3
#define UTP_OCS_MISMATCH_RESPONSE_UPIU_SIZE 0x4
#define UTP_OCS_PEER_COMMUNICATION_FAILURE 0x5
#define UTP_OCS_ABORTED 0x6
#define UTP_OCS_FATAL_ERROR 0x7
#define UTP_OCS_INVALID_OCS_VALUE 0xF

typedef struct QEMU_PACKED utp_header {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
} utp_header;

typedef struct QEMU_PACKED utp_tr_desc {
    utp_header hdr;

    uint32_t ucdba;
    uint32_t ucdbau;
    uint16_t rul;
    uint16_t ruo;
    uint16_t prdtl;
    uint16_t prdto;
} utp_tr_desc;

typedef struct QEMU_PACKED utp_tmr_desc {
    utp_header hdr;

    upiu_task_mng_req req;
    upiu_task_mng_resp resp;
} utp_tmr_desc;

typedef struct QEMU_PACKED ufs_prdt {
    uint32_t addrl;
    uint32_t addrh;
    uint32_t rsvd0;
    uint32_t size;
} ufs_prdt;

typedef struct utp_pkt {
    union {
        utp_header hdr;
        utp_tr_desc tr;
        utp_tmr_desc tmr;
    };
} utp_pkt;

#endif
