/*
 * UFS UPIU Descriptor blocks
 * Based on JESD220
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UFS_UPIU_H
#define UFS_UPIU_H

#include "qemu/osdep.h"

/*
 * UFS Protocol Information Units
 */

/*
 * Transaction Codes
 */
#define TRNS_NOP_OUT 0x0
#define TRNS_NOP_IN 0x20
#define TRNS_COMMAND 0x1
#define TRNS_RESPONSE 0x21
#define TRNS_DATA_OUT 0x2
#define TRNS_DATA_IN 0x22
#define TRNS_TASK_MNG_REQ 0x4
#define TRNS_TASK_MNG_RESP 0x24
#define TRNS_RDY_TO_TRANSFER 0x31
#define TRNS_QUERY_REQ 0x16
#define TRNS_QUERY_RESP 0x36
#define TRNS_REJECT 0x3f

/*
 * Flags
 */
#define FLAG_READ 0x40
#define FLAG_OVERFLOW 0x40
#define FLAG_WRITE 0x20
#define FLAG_UNDERFLOW 0x20
#define FLAG_DATA_OUT_MISMATCH 0x10
#define FLAG_CP 0x4

/*
 * Response
 */
#define RESP_TARGET_SUCCESS 0x0
#define RESP_TARGET_FAILURE 0x1

/*
 * UPIU Header
 */
typedef struct QEMU_PACKED upiu_header {
    uint8_t transaction_type;
    uint8_t flags;
    uint8_t lun;
    uint8_t task_tag;
    uint8_t iid_cmd_type;
    uint8_t request_type;
    uint8_t response;
    uint8_t status;
    uint8_t ehs_len;
    uint8_t device_info;
    uint16_t data_seg_len;
} upiu_header;


#define UPIU_HDR_TRANSACTION_TYPE 0
#define UPIU_HDR_TRANSACTION_TYPE_SIZE 1
#define UPIU_HDR_FLAGS 1
#define UPIU_HDR_FLAGS_SIZE 1
#define UPIU_HDR_LUN 2
#define UPIU_HDR_LUN_SIZE 1
#define UPIU_HDR_TASK_TAG 3
#define UPIU_HDR_TASK_TAG_SIZE 1
#define UPIU_HDR_IID_CMD_TYPE 4
#define UPIU_HDR_IID_CMD_TYPE_SIZE 1
#define UPIU_HDR_REQUEST_TYPE 5
#define UPIU_HDR_REQUEST_TYPE_SIZE 1
#define UPIU_HDR_RESPONSE 6
#define UPIU_HDR_RESPONSE_SIZE 1
#define UPIU_HDR_STATUS 7
#define UPIU_HDR_STATUS_SIZE 1
#define UPIU_HDR_EHS_LEN 8
#define UPIU_HDR_EHS_LEN_SIZE 1
#define UPIU_HDR_DEVICE_INFO 9
#define UPIU_HDR_DEVICE_INFO_SIZE 1
#define UPIU_HDR_DATA_SEG_LEN 10
#define UPIU_HDR_DATA_SEG_LEN_SIZE 2

/*
 * UPIU Command and Response
 */
typedef struct QEMU_PACKED upiu_cmd {
    upiu_header hdr;
    uint32_t exp_data_len;
    uint32_t cbd[4];
} upiu_cmd;

#define UPIU_CMD { \
        .hdr.transaction_type = TRNS_COMMAND  \
    }

#define UPIU_CMD_EXP_DATA_LEN 12
#define UPIU_CMD_EXP_DATA_LEN_SIZE 4
#define UPIU_CMD_CDB 16
#define UPIU_CMD_CDB_SIZE 16

typedef struct QEMU_PACKED upiu_resp {
    upiu_header hdr;
    uint32_t res_tran_count;
    uint32_t rsvd[4];
    /*
     * Data Segment...
     */
} upiu_resp;

#define UPIU_RESP { \
        .hdr.transaction_type = TRNS_RESPONSE  \
    }

#define UPIU_RESP_RES_TRAN_COUNT 12
#define UPIU_RESP_RES_TRAN_COUNT_SIZE 4

/*
 * UPIU DATA IN and OUT
 */
typedef struct QEMU_PACKED upiu_data {
    upiu_header hdr;
    uint32_t data_offset;
    uint32_t data_trns_count;
    uint32_t rsvd[3];
} upiu_data;

#define UPIU_DATA_OUT { \
        .hdr.transaction_type = TRNS_DATA_OUT  \
    }
#define UPIU_DATA_IN { \
        .hdr.transaction_type = TRNS_DATA_IN  \
    }
#define UPIU_RDY_TO_TRANSFER { \
        .hdr.transaction_type = TRNS_RDY_TO_TRANSFER  \
    }

#define UPIU_DATA_DATA_OFFSET 12
#define UPIU_DATA_DATA_OFFSET_SIZE 4
#define UPIU_DATA_DATA_TRNS_COUNT 16
#define UPIU_DATA_DATA_TRNS_COUNT_SIZE 4

/*
 * UPIU NOP IN and OUT
 */
typedef struct QEMU_PACKED upiu_nop {
    upiu_header hdr;
    uint32_t rsvd[5];
} upiu_nop;

#define UPIU_NOP_OUT { \
        .hdr.transaction_type = TRNS_NOP_OUT  \
    }
#define UPIU_NOP_IN { \
        .hdr.transaction_type = TRNS_NOP_IN  \
    }

/*
 * UPIU Task Managment request & response
 */
typedef struct QEMU_PACKED upiu_task_mng_req {
    upiu_header hdr;
    uint32_t input_parm[3];
    uint32_t rsvd[2];
} upiu_task_mng_req;

#define UPIU_TASK_MNG_REQ { \
        .hdr.transaction_type = TRNS_TASK_MNG_REQ  \
    }

typedef struct QEMU_PACKED upiu_task_mng_resp {
    upiu_header hdr;
    uint32_t out_parm[2];
    uint32_t rsvd[3];
} upiu_task_mng_resp;

#define UPIU_TASK_MNG_RESP { \
        .hdr.transaction_type = TRNS_TASK_MNG_RESP  \
    }

/*
 * UPIU Reject
 */
typedef struct QEMU_PACKED upiu_reject {
    upiu_header hdr;
    uint8_t basic_hdr_status;
    uint8_t rsvd0;
    uint8_t e2e_status;
    uint8_t rsvd1;
    uint32_t rsvd3[4];
} upiu_reject;

#define UPIU_REJECT { \
        .hdr.transaction_type = TRNS_REJECT  \
    }

#define UPIU_REJECT_BASIC_HDR_STATUS 12
#define UPIU_REJECT_BASIC_HDR_STATUS_SIZE 1
#define UPIU_REJECT_E2E_STATUS 14
#define UPIU_REJECT_E2E_STATUS_SIZE 1

/*
 * TODO: QUERY desc
 */
typedef struct QEMU_PACKED upiu_query {
    upiu_header hdr;
    /*
     * Transaction specific fields
     */
    uint32_t tsf[4];
    uint32_t rsvd;
} upiu_query;

#define UPIU_QUERY_REQ { \
        .hdr.transaction_type = TRNS_QUERY_REQ \
    }
#define UPIU_QUERY_RESP { \
        .hdr.transaction_type = TRNS_QUERY_RESP \
    }

#define QUERY_TYPE_STANDARD_READ_REQUEST 0x1
#define QUERY_TYPE_STANDARD_WRITE_REQUEST 0x81

#define QUERY_OP_NOP 0x00
#define QUERY_OP_READ_DESCRIPTOR 0x01
#define QUERY_OP_WRITE_DESCRIPTOR 0x02
#define QUERY_OP_READ_ATTRIBUTE 0x03
#define QUERY_OP_WRITE_ATTRIBUTE 0x04
#define QUERY_OP_READ_FLAG 0x05
#define QUERY_OP_SET_FLAG 0x06
#define QUERY_OP_CLEAR_FLAG 0x07
#define QUERY_OP_TOGGLE_FLAG 0x08

/*
 * Transaction specific fields
 */
#define QUERY_TSF_OPCODE 12
#define QUERY_TSF_OPCODE_SIZE 1
#define QUERY_TSF_IDN 13
#define QUERY_TSF_IDN_SIZE 1
#define QUERY_TSF_INDEX 14
#define QUERY_TSF_INDEX_SIZE 1
#define QUERY_TSF_SELECTOR 15
#define QUERY_TSF_SELECTOR_SIZE 1
#define QUERY_TSF_LENGTH 18
#define QUERY_TSF_LENGTH_SIZE 2
#define QUERY_TSF_ATTR_VAL 20
#define QUERY_TSF_ATTR_VAL_SIZE 4
#define QUERY_TSF_FLAG_VAL 23
#define QUERY_TSF_FLAG_VAL_SIZE 1

/*
 * Query Response fields
 */

#define QUERY_RESP_SUCCESS 0x00
#define QUERY_RESP_PARAMETER_NOT_READABLE 0xF6
#define QUERY_RESP_PARAMETER_NOT_WRITEABLE 0xF7
#define QUERY_RESP_PARAMETER_ALREADY_WRITTEN 0xF8
#define QUERY_RESP_INVALID_LENGTH 0xF9
#define QUERY_RESP_INVALID_VALUE 0xFA
#define QUERY_RESP_INVALID_SELECTOR 0xFB
#define QUERY_RESP_INVALID_INDEX 0xFC
#define QUERY_RESP_INVALID_IDN 0xFD
#define QUERY_RESP_INVALID_OPCODE 0xFE
#define QUERY_RESP_GENERAL_FAILURE 0xFF

typedef struct upiu_pkt {
    union {
        upiu_header hdr;
        upiu_cmd cmd;
        upiu_resp resp;
        upiu_data data;
        upiu_nop nop;
        upiu_task_mng_req task_mng_req;
        upiu_task_mng_resp task_mng_resp;
        upiu_reject reject;
        upiu_query query;
    };
} upiu_pkt;

/*
 * UPIU Header Read Macros
 */
#define UPIU_TT(pkt)  (((upiu_pkt *)pkt)->hdr.transaction_type & 0x3F)
#define UPIU_LUN(pkt) (((upiu_pkt *)pkt)->hdr.lun)
#define UPIU_TAG(pkt)  (((upiu_pkt *)pkt)->hdr.task_tag)
#define UPIU_DSL(pkt) be16_to_cpu(((upiu_pkt *)pkt)->hdr.data_seg_len)
#define UPIU_EHS_L(pkt) (((upiu_pkt *)pkt)->hdr.ehs_len)
#define UPIU_REQ_TYPE(pkt) (((upiu_pkt *)pkt)->hdr.request_type)

#define UPIU_DBO(pkt) be32_to_cpu(((upiu_pkt *)pkt)->data.data_offset)
#define UPIU_DTC(pkt) be32_to_cpu(((upiu_pkt *)pkt)->data.data_trns_count)

#endif
