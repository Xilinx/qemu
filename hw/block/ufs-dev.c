/*
 * UFS Device
 * Based on JESD220E
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include <math.h>
#include "hw/qdev-properties.h"
#include "trace.h"
#include "hw/block/ufs-dev.h"
#include "ufs-utp.h"
#include "qemu/coroutine.h"
#include "ufs-dev-desc.h"

/*
 * map_lun
 * Maps the boot lun id to configured boot lunA/B.
 * Discard the reserved lun ID's.
 */
static uint8_t ufs_dev_map_lun(UFSDev *s, uint8_t lun)
{
    uint8_t r = 0xFF;

    switch (lun) {
    case 0xB0:
        if (s->attr.BootLunEn == 0) {
            /*
             * Boot Lun not configured
             */
            break;
        }
        r = (s->attr.BootLunEn == 1 ? s->BootLUA :
              s->attr.BootLunEn == 2 ? s->BootLUB : 0xFF);
        break;
    case 0x81:
    case 0xD0:
    case 0xC4:
        r = 0xFF;
        break;
    case 0 ... 0x7F:
        r = lun;
    };

    return r;
}

/*
 * lun_enabled
 * Check if lun/bootlun is enabled
 */
static bool ufs_dev_lun_enable(UFSDev *s, uint8_t lun)
{
    uint8_t bootLun;
    bool r = false;

    switch (lun) {
    case 0xB0:
        if (s->ufsDesc.device[DEV_BOOT_ENABLE] == 1) {
            bootLun = ufs_dev_map_lun(s, lun);
            if (bootLun == 0xFF) {
                /*
                 * Boot Lun not configured
                 */
                r = false;
                break;
            }
            r = !!UFS_REG_R(s->ufsDesc.unit[bootLun], UNIT_LU_ENABLE);
        }
        break;
    case 0x81:
    case 0xD0:
    case 0xC4:
        r = false;
        break;
    case 0x0 ... 0x7F:
        if (lun < s->num_luns) {
            r = !!UFS_REG_R(s->ufsDesc.unit[lun], UNIT_LU_ENABLE);
        }
        break;
    };
    return r;
}

/*
 * Query response encode
 * Encode required feilds for Query response
 */
static void ufs_query_response_encode(upiu_pkt *resp, upiu_pkt *req)
{
    g_assert(resp);
    g_assert(req);

    memcpy(&resp->query.tsf, &req->query.tsf, sizeof(resp->query.tsf));
    UFS_REG_W(resp, UPIU_HDR_REQUEST_TYPE, UPIU_REQ_TYPE(req));
    UFS_REG_W(resp, UPIU_HDR_TASK_TAG, UPIU_TAG(req));
}

/*
 * Flag Read
 * Read the UFS flag requested with Query Command
 */
static void ufs_flag_read(UFSDev *s, upiu_pkt *pkt)
{
    upiu_pkt resp = UPIU_QUERY_RESP;
    uint8_t idn = UFS_REG_R(pkt, QUERY_TSF_IDN);

    ufs_query_response_encode(&resp, pkt);
    switch (idn) {
    case FLAG_DEVICE_INIT:
        UFS_REG_W(&resp, QUERY_TSF_FLAG_VAL, s->flag.DeviceInit);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "flag %d not implemented\n", idn);
    };
    UFS_REG_W(&resp, UPIU_HDR_DEVICE_INFO,
             !!(s->attr.ExceptionEventControl &
                s->attr.ExceptionEventStatus));
    trace_ufsdev_send_upiu("QUERY_RESP", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
}

static void ufs_flag_postw(UFSDev *s, uint8_t idn, uint8_t op)
{
    switch (idn) {
    case FLAG_DEVICE_INIT:
        if (s->flag.DeviceInit) {
            resettable_reset(OBJECT(s), RESET_TYPE_COLD);
            s->flag.DeviceInit = 0;
            s->devInitDone = 1;
        }
        break;
    };
}

/*
 * Flag Write
 * Set/Clear/Toggle the UFS flag requested with Query Command
 */
static void ufs_flag_write(UFSDev *s, upiu_pkt *pkt)
{
    uint8_t idn = UFS_REG_R(pkt, QUERY_TSF_IDN);
    uint8_t op = UFS_REG_R(pkt, QUERY_TSF_OPCODE);
    upiu_pkt resp = UPIU_QUERY_RESP;
    uint8_t *data = NULL;

    ufs_query_response_encode(&resp, pkt);
    switch (idn) {
    case FLAG_DEVICE_INIT:
        data = &s->flag.DeviceInit;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "flag %d not implemented\n", idn);
        break;
    }
    if (data) {
        switch (op) {
        case QUERY_OP_SET_FLAG:
            *data = 1;
            break;
        case QUERY_OP_CLEAR_FLAG:
            *data = 0;
            break;
        case QUERY_OP_TOGGLE_FLAG:
            /*
             * No change
             */
            break;
        }
    }
    ufs_flag_postw(s, idn, op);
    trace_ufsdev_send_upiu("QUERY_RESP", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
}

/*
 * Attribute Read
 */
static void ufs_attr_read(UFSDev *s, upiu_pkt *pkt)
{
    uint8_t idn = UFS_REG_R(pkt, QUERY_TSF_IDN);
    upiu_pkt resp = UPIU_QUERY_RESP;

    ufs_query_response_encode(&resp, pkt);
    switch (idn) {
    case ATTR_BOOT_LUN_EN:
        UFS_REG_W(&resp, QUERY_TSF_ATTR_VAL, s->attr.BootLunEn);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Attr %d not implemented\n", idn);
    };
    UFS_REG_W(&resp, UPIU_HDR_DEVICE_INFO,
             !!(s->attr.ExceptionEventControl &
                s->attr.ExceptionEventStatus));
    trace_ufsdev_send_upiu("QUERY_RESP", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
}

/*
 * Attribute Write
 */
static void ufs_attr_write(UFSDev *s, upiu_pkt *pkt)
{
    uint8_t idn = UFS_REG_R(pkt, QUERY_TSF_IDN);
    upiu_pkt resp = UPIU_QUERY_RESP;

    ufs_query_response_encode(&resp, pkt);
    switch (idn) {
    case ATTR_BOOT_LUN_EN:
        s->attr.BootLunEn = UFS_REG_R(pkt, QUERY_TSF_ATTR_VAL);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Attr %d not implemented\n", idn);
    }
    UFS_REG_W(&resp, UPIU_HDR_DEVICE_INFO,
             !!(s->attr.ExceptionEventControl &
                s->attr.ExceptionEventStatus));
    trace_ufsdev_send_upiu("QUERY_RESP", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
}

/*
 * Record Task
 * Record the request upiu so that it could be processed
 * when data arrives or used to send response.
 */
static void ufs_record_task(UFSDev *s, upiu_pkt *pkt)
{
    UFSTaskQ *task = g_new0(UFSTaskQ, 1);
    memcpy(&task->pkt, pkt, sizeof(upiu_pkt));
    QTAILQ_INSERT_TAIL(&s->taskQ, task, link);
}

static void ufs_desc_read(UFSDev *s, upiu_pkt *pkt)
{
    uint8_t idn = UFS_REG_R(pkt, QUERY_TSF_IDN);
    uint8_t index = UFS_REG_R(pkt, QUERY_TSF_INDEX);
    uint16_t len = UFS_REG_R(pkt, QUERY_TSF_LENGTH);
    upiu_pkt resp = UPIU_QUERY_RESP;
    uint8_t desc_len = 0;
    void *data = NULL;

    ufs_query_response_encode(&resp, pkt);
    switch (idn) {
    case UFS_DEV_DEVICE:
        trace_ufsdev_desc_read("Device desc");
        if (s->devInitDone ||
            UFS_REG_R(s->ufsDesc.config[0], CONFIG_DESCR_ACCESS_EN)) {
            desc_len = s->ufsDesc.device[DEV_LENGTH];
            data = s->ufsDesc.device;
        } else {
            UFS_REG_W(&resp, UPIU_HDR_RESPONSE,
                      QUERY_RESP_PARAMETER_NOT_READABLE);
        }
        break;
    case UFS_DEV_CONFIGURATION:
        trace_ufsdev_desc_read("Configuration desc");
        desc_len = s->ufsDesc.config[index][CONFIG_LENGTH];
        data = s->ufsDesc.config[index];
        break;
    case UFS_DEV_GEOMETRY:
        trace_ufsdev_desc_read("Geometry desc");
        desc_len = s->ufsDesc.geo[GOME_LENGTH];
        data = s->ufsDesc.geo;
        break;
    case UFS_DEV_UNIT:
        trace_ufsdev_desc_read("Unit desc");
        if (index >= s->num_luns) {
            UFS_REG_W(&resp, UPIU_HDR_RESPONSE, QUERY_RESP_INVALID_INDEX);
        } else {
            desc_len = s->ufsDesc.unit[index][UNIT_LENGTH];
            data = s->ufsDesc.unit[index];
        }
        break;
    case UFS_DEV_STRING:
        switch (index) {
        case 0:
            desc_len = s->ufsDesc.manStr[0];
            data = s->ufsDesc.manStr;
            break;
        case 1:
            desc_len = s->ufsDesc.prodStr[0];
            data = s->ufsDesc.prodStr;
            break;
        case 2:
            desc_len = s->ufsDesc.oemIdStr[0];
            data = s->ufsDesc.oemIdStr;
            break;
        case 3:
            desc_len = s->ufsDesc.serialNumStr[0];
            data = s->ufsDesc.serialNumStr;
            break;
        case 4:
            desc_len = s->ufsDesc.prodRevLvlStr[0];
            data = s->ufsDesc.prodRevLvlStr;
            break;
        };
        break;
    case UFS_DEV_INTERCONNECT:
        desc_len = s->ufsDesc.interconnect[INTERCONNECT_LENGTH];
        data = s->ufsDesc.interconnect;
        break;
    case UFS_DEV_DEVICE_HEALTH:
        desc_len = s->ufsDesc.devHealth[DEV_HEALTH_LENGTH];
        data = s->ufsDesc.devHealth;
        break;
    case UFS_DEV_POWER:
        desc_len = s->ufsDesc.pwrParam[0];
        data = s->ufsDesc.pwrParam;
    };

    if (desc_len) {
        len = len <= desc_len ? len : desc_len;
        UFS_REG_W(&resp, UPIU_HDR_DATA_SEG_LEN, len);
    }
    UFS_REG_W(&resp, UPIU_HDR_DEVICE_INFO,
             !!(s->attr.ExceptionEventControl &
                s->attr.ExceptionEventStatus));
    trace_ufsdev_send_upiu("QUERY_RESP", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
    if (data) {
        ufshci_send_data(s->ufs_ini, data, len, UPIU_TAG(pkt));
    }
}

static void ufs_config_desc_write(UFSDev *s, uint8_t *data, uint8_t index,
                                  uint16_t len)
{
    int i;
    uint8_t *config_desc = s->ufsDesc.config[index];
    uint32_t off;

    memcpy(config_desc, data, len);

    UFS_REG_W(s->ufsDesc.device, DEV_BOOT_ENABLE,
              UFS_REG_R(config_desc, CONFIG_BOOT_ENABLE));
    UFS_REG_W(s->ufsDesc.device, DEV_DESCR_ACCESS_EN,
              UFS_REG_R(config_desc, CONFIG_DESCR_ACCESS_EN));
    UFS_REG_W(s->ufsDesc.device, DEV_INIT_POWER_MODE,
              UFS_REG_R(config_desc, CONFIG_INIT_POWER_MODE));
    UFS_REG_W(s->ufsDesc.device, DEV_HIGH_PRIORITY_LUN,
              UFS_REG_R(config_desc, CONFIG_HIGH_PRIORITY_LUN));
    UFS_REG_W(s->ufsDesc.device, DEV_SECURE_REMOVAL_TYPE,
              UFS_REG_R(config_desc, CONFIG_SECURE_REMOVAL_TYPE));
    UFS_REG_W(s->ufsDesc.device, DEV_INIT_ACTIVE_ICCLEVEL,
              UFS_REG_R(config_desc, CONFIG_INIT_ACTIVE_ICCLEVEL));
    UFS_REG_W(s->ufsDesc.device, DEV_INIT_ACTIVE_ICCLEVEL,
              UFS_REG_R(config_desc, CONFIG_INIT_ACTIVE_ICCLEVEL));
    UFS_REG_W(s->ufsDesc.device, DEV_PERIODIC_RTCUPDATE,
              UFS_REG_R(config_desc, CONFIG_PERIODIC_RTCUPDATE));
    UFS_REG_W(s->ufsDesc.device, DEV_WRITE_BOOSTER_BUFFER_TYPE,
         UFS_REG_R(config_desc, CONFIG_WRITE_BOOSTER_BUFFER_TYPE));
    UFS_REG_W(s->ufsDesc.device,
              DEV_NUM_SHARED_WRITE_BOOSTER_BUFFER_ALLOC_UNITS,
              UFS_REG_R(config_desc, \
                      CONFIG_NUM_SHARED_WRITE_BOOSTER_BUFFER_ALLOC_UNITS));

    for (i = index * 8; i < (index + 1) * 8 && i < s->num_luns; i++) {
        off = (i - index * 8) * UNIT_DESC_CONFIG_LENGTH;

        UFS_REG_W(s->ufsDesc.unit[i], UNIT_LU_ENABLE,
                  config_desc[off + CONFIG_LU_ENABLE]);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_BOOT_LUN_ID,
                  config_desc[off + CONFIG_BOOT_LUN_ID]);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_LU_WRITE_PROTECT,
                  config_desc[off + CONFIG_LU_WRITE_PROTECT]);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_MEMORY_TYPE,
                  config_desc[off + CONFIG_MEMORY_TYPE]);
        /*
         * TODO: configure bLogicBlockCount with dNumAllocUnits
         */
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_DATA_RELIABILITY,
                  config_desc[off + CONFIG_DATA_RELIABILITY]);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_LOGICAL_BLOCK_SIZE,
                  config_desc[off + CONFIG_LOGICAL_BLOCK_SIZE]);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_PROVISIONING_TYPE,
                  config_desc[off + CONFIG_PROVISIONING_TYPE]);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_CONTEXT_CAPABILITIES,
                  UFS_REG_R_2(config_desc, off + CONFIG_CONTEXT_CAPABILITIES));
    }
}

static void ufs_desc_write(UFSDev *s, upiu_pkt *pkt, uint16_t len,
                           void *data)
{
    uint8_t idn = UFS_REG_R(pkt, QUERY_TSF_IDN);
    uint8_t index = UFS_REG_R(pkt, QUERY_TSF_INDEX);
    uint8_t desc_len = 0;
    void *desc = NULL;
    upiu_pkt resp = UPIU_QUERY_RESP;

    ufs_query_response_encode(&resp, pkt);
    switch (idn) {
    case UFS_DEV_DEVICE:
        desc_len = s->ufsDesc.device[DEV_LENGTH];
        desc = s->ufsDesc.device;
        break;
    case UFS_DEV_CONFIGURATION:
        desc_len = s->ufsDesc.config[index][CONFIG_LENGTH];
        desc = s->ufsDesc.config[index];
        break;
    case UFS_DEV_GEOMETRY:
        desc_len = s->ufsDesc.geo[GOME_LENGTH];
        desc = s->ufsDesc.geo;
        break;
    case UFS_DEV_UNIT:
        if (index >= s->num_luns) {
            UFS_REG_W(&resp, UPIU_HDR_RESPONSE, QUERY_RESP_INVALID_INDEX);
        } else {
            desc_len = s->ufsDesc.unit[index][UNIT_LENGTH];
            desc = s->ufsDesc.unit[index];
            switch (UFS_REG_R(data, UNIT_BOOT_LUN_ID)) {
            case 1:
                s->BootLUA = index;
                break;
            case 2:
                s->BootLUB = index;
                break;
            };

        }
        break;
    default:
        UFS_REG_W(&resp, UPIU_HDR_RESPONSE, QUERY_RESP_INVALID_IDN);
        qemu_log_mask(LOG_UNIMP, "Desc write %d not implemented\n", idn);
    };
    if (len == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "Write request with len == 0 received");
    } else if (desc && !(desc_len != len)) {
        /*
         * Error: Valid IDN but Invalid LENGTH
         */
        UFS_REG_W(&resp, UPIU_HDR_RESPONSE, QUERY_RESP_INVALID_LENGTH);
    } else if (desc) {
        switch (idn) {
        case UFS_DEV_CONFIGURATION:
            ufs_config_desc_write(s, data, index, len);
            break;
        default:
            memcpy(desc, data, desc_len);
        };
    }
    trace_ufsdev_send_upiu("QUERY_RESP", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
}

static void ufs_query_process(UFSDev *s, upiu_pkt *pkt)
{
    uint8_t opcode = UFS_REG_R(pkt, QUERY_TSF_OPCODE);
    uint8_t req_type = UPIU_REQ_TYPE(pkt);

    if (req_type == QUERY_TYPE_STANDARD_READ_REQUEST) {
        switch (opcode) {
        case QUERY_OP_READ_DESCRIPTOR:
            ufs_desc_read(s, pkt);
            break;
        case QUERY_OP_READ_ATTRIBUTE:
            ufs_attr_read(s, pkt);
            break;
        case QUERY_OP_READ_FLAG:
            ufs_flag_read(s, pkt);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Wrong READ Query type mentioned");
        };
    } else if (req_type == QUERY_TYPE_STANDARD_WRITE_REQUEST) {
        switch (opcode) {
        case QUERY_OP_WRITE_DESCRIPTOR:
            ufs_record_task(s, pkt);
            break;
        case QUERY_OP_WRITE_ATTRIBUTE:
            ufs_attr_write(s, pkt);
            break;
        case QUERY_OP_SET_FLAG:
        case QUERY_OP_CLEAR_FLAG:
        case QUERY_OP_TOGGLE_FLAG:
            ufs_flag_write(s, pkt);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Wrong WRITE Query type mentioned");
        };
   }
}

static void ufs_query_process_data(UFSDev *s, upiu_pkt *pkt, uint16_t len,
                                   void *data)
{
    uint8_t opcode = UFS_REG_R(pkt, QUERY_TSF_OPCODE);

    switch (opcode) {
    case QUERY_OP_WRITE_DESCRIPTOR:
        ufs_desc_write(s, pkt, len, data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid data segment received");
    };
}

static bool ufs_cmd_process(UFSDev *s, upiu_pkt *pkt)
{
    uint8_t lun;

    /*
     * BOOT LUN check
     */
    if (!ufs_dev_lun_enable(s, pkt->hdr.lun)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Lun %d not enabled!\n", pkt->hdr.lun);
    }

    ufs_record_task(s, pkt);

    lun = ufs_dev_map_lun(s, pkt->hdr.lun);
    if (lun == 0xFF) {
        /*
         * Unknown Lun
         */
        qemu_log_mask(LOG_GUEST_ERROR, "Lun %d invalid!\n", pkt->hdr.lun);
    }

    if (s->ufs_scsi_target) {
        ufs_scsi_if_handle_scsi(s->ufs_scsi_target, (void *)pkt->cmd.cbd,
                           UPIU_CMD_CDB_SIZE, pkt->hdr.task_tag, lun);
    } else {
        return false;
    }
    return true;
}

static void empty_upiu(upiu_pkt *pkt)
{
    memset(pkt, 0, sizeof(upiu_pkt));
}

static void encode_nop_in(upiu_nop *pkt, uint8_t tag)
{
    g_assert(pkt);
    pkt->hdr.transaction_type = TRNS_NOP_IN;
    pkt->hdr.task_tag = tag;
}

static void respond_nop_in(UFSDev *s, upiu_pkt *pkt)
{
    upiu_pkt resp;

    empty_upiu(&resp);
    encode_nop_in(&resp.nop, UPIU_TAG(pkt));
    trace_ufsdev_send_upiu("NOP_IN", UPIU_TAG(&resp));
    ufshci_send_upiu(s->ufs_ini, &resp);
}

static void ufs_dev_receive_upiu(ufshcIF *ifs, upiu_pkt *pkt)
{
    UFSDev *s = UFS_DEV(ifs);

    switch (UPIU_TT(pkt)) {
    case TRNS_DATA_OUT:
        /*
         * Send data to device
         */
        qemu_log_mask(LOG_UNIMP, "DATA_OUT not implemented\n");
        break;
    case TRNS_NOP_OUT:
        /*
         * Send NOP IN
         */
        trace_ufsdev_recv_upiu("NOP_OUT", UPIU_TAG(pkt));
        respond_nop_in(s, pkt);
        break;
    case TRNS_TASK_MNG_REQ:
        /*
         * Send response base on request.
         */
        qemu_log_mask(LOG_UNIMP, "TASK_MNG_REQ not implemented\n");
        break;
    case TRNS_QUERY_REQ:
        /*
         * Send response base on query.
         */
        trace_ufsdev_recv_upiu("QUERY_REQ", UPIU_TAG(pkt));
        ufs_query_process(s, pkt);
        break;
    case TRNS_COMMAND:
        /*
         * Send CMD
         */
        trace_ufsdev_recv_upiu("COMMAND", UPIU_TAG(pkt));
        ufs_cmd_process(s, pkt);
        break;
    default:
        /*
         * Send Reject
         */
        break;
    };
}

static void ufs_dev_receive_data(ufshcIF *ifs, void *data, uint16_t len,
                                  uint8_t task_tag)
{
    UFSDev *s = UFS_DEV(ifs);
    UFSTaskQ *task = NULL;
    upiu_pkt *pkt = NULL;

    QTAILQ_FOREACH(task, &s->taskQ, link) {
        pkt = &task->pkt;
        if (UPIU_TAG(pkt) == task_tag) {
            break;
        }
    }
    if (task) {
        switch (UPIU_TAG(pkt)) {
        case TRNS_DATA_OUT:
            break;
        case TRNS_QUERY_REQ:
            ufs_query_process_data(s, pkt, len, data);
            break;
        case TRNS_TASK_MNG_REQ:
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "Data cannot be handled\n");
            break;
        };
        if (!task->data_offset) {
            QTAILQ_REMOVE(&s->taskQ, task, link);
        }
    }
}

static uint32_t ufs_dev_receive_scsi_data(ufs_scsi_if *ifs, uint8_t *data,
                                      uint32_t size, uint8_t tag)
{
    UFSDev *s = UFS_DEV(ifs);
    UFSTaskQ *task = NULL;
    upiu_pkt *pkt = NULL;
    upiu_pkt data_in = UPIU_DATA_IN;
    uint16_t dsl;
    uint8_t *buf;

    QTAILQ_FOREACH(task, &s->taskQ, link) {
        pkt = &task->pkt;
        if (UPIU_TAG(pkt) == tag) {
            break;
        }
    }
    if (task) {
        /*
         * DATA IN encode
         */
        dsl = ((size + 3) / 4) * 4;
        data_in.hdr.task_tag = pkt->hdr.task_tag;
        data_in.hdr.lun = pkt->hdr.lun;
        data_in.hdr.iid_cmd_type = pkt->hdr.iid_cmd_type;
        data_in.data.data_offset = cpu_to_be32(task->data_offset);
        data_in.data.data_trns_count = cpu_to_be32(size);
        data_in.data.hdr.data_seg_len = cpu_to_be16(dsl);
        trace_ufsdev_send_upiu("DATA_IN", UPIU_TAG(&data_in));
        ufshci_send_upiu(s->ufs_ini, &data_in);
        if (dsl) {
            buf = g_malloc0(dsl);
            memcpy(buf, data, size);
            ufshci_send_data(s->ufs_ini, buf, dsl, tag);
            g_free(buf);
        }
        task->data_offset += size;
        return size;
    }
    return 0;
}

static void ufs_dev_receive_sense_data(ufs_scsi_if *ifs, uint8_t *sense,
                                        uint32_t len, uint8_t tag)
{
    UFSDev *s = UFS_DEV(ifs);
    UFSTaskQ *task = NULL;
    upiu_pkt *pkt = NULL;
    upiu_pkt resp = UPIU_RESP;
    uint16_t dsl;
    uint8_t *buf;

    QTAILQ_FOREACH(task, &s->taskQ, link) {
        pkt = &task->pkt;
        if (UPIU_TAG(pkt) == tag) {
            break;
        }
    }

    if (task) {
        dsl = ((len + 3) / 4) * 4;
        resp.hdr.task_tag = tag;
        resp.hdr.lun = pkt->hdr.lun;
        resp.hdr.iid_cmd_type = pkt->hdr.iid_cmd_type;
        resp.hdr.data_seg_len = cpu_to_be16(dsl);
        /*
         * TODO: Implement residual transfer count
         */
        trace_ufsdev_send_upiu("RESPONSE", UPIU_TAG(&resp));
        ufshci_send_upiu(s->ufs_ini, &resp);
        if (dsl) {
            buf = g_malloc0(dsl);
            memcpy(buf, sense, len);
            ufshci_send_data(s->ufs_ini, buf, dsl, tag);
            g_free(buf);
        }
        QTAILQ_REMOVE(&s->taskQ, task, link);
    }
}

static QEMUSGList *ufs_dev_get_sgl(ufs_scsi_if *ifs, uint8_t tag, uint8_t lun)
{
    UFSDev *s = UFS_DEV(ifs);
    UFSTaskQ *task = NULL;
    upiu_pkt *pkt = NULL;

    QTAILQ_FOREACH(task, &s->taskQ, link) {
        pkt = &task->pkt;
        if (UPIU_TAG(pkt) == tag) {
            break;
        }
    }

    if (task) {
        return ufshci_get_sgl(s->ufs_ini, tag);
    }
    return NULL;
}

static void ufsdev_realize(DeviceState *dev, Error **errp)
{
    UFSDev *s = UFS_DEV(dev);
    unsigned int luns = s->num_luns;
    unsigned int i, j;

    object_property_set_link(OBJECT(&s->core), "ufs-scsi-init",
                             OBJECT(s), NULL);

    if (!qdev_realize(DEVICE(&s->core), BUS(s->bus), errp)) {
        return;
    }

    if (!luns) {
        qdev_prop_set_uint32(dev, "len-luns", 8);
        luns = 8;
    }

    /*
     * Allocate the configure descriptor w.r.t number of luns
     *  supported by device.
     */
    if (luns > 24) {
        s->ufsDesc.config[3] = g_malloc0(UFS_DEV_CONFIG_DESC_SIZE +
                                         (luns % 8 ? luns % 8 : 8) *
                                         UNIT_DESC_CONFIG_LENGTH);
        luns -= luns % 8 ? luns % 8 : 8;
    }
    if (luns > 16) {
        s->ufsDesc.config[2] = g_malloc0(UFS_DEV_CONFIG_DESC_SIZE +
                                         (luns % 8 ? luns % 8 : 8) *
                                         UNIT_DESC_CONFIG_LENGTH);
        luns -= luns % 8 ? luns % 8 : 8;
    }
    if (luns > 8) {
        s->ufsDesc.config[1] = g_malloc0(UFS_DEV_CONFIG_DESC_SIZE +
                                         (luns % 8 ? luns % 8 : 8) *
                                         UNIT_DESC_CONFIG_LENGTH);
        luns -= luns % 8 ? luns % 8 : 8;
    }
    s->ufsDesc.config[0] = g_malloc0(UFS_DEV_CONFIG_DESC_SIZE +
                                    (luns % 8 ? luns % 8 : 8) *
                                     UNIT_DESC_CONFIG_LENGTH);

    /*
     * Initialize Configuration Descriptor
     * Also includes Unit Descriptor
     */
    for (i = s->num_luns - 1; i; i -= i % 8 ? i % 8 : 8) {
        j = i % 8 ? i % 8 : 8;
        UFS_REG_W(s->ufsDesc.config[i / 8], CONFIG_LENGTH,
                  UFS_DEV_CONFIG_DESC_SIZE +
                  j * UNIT_DESC_CONFIG_LENGTH);
        UFS_REG_W(s->ufsDesc.config[i / 8], CONFIG_DESCRIPTOR_IDN,
                  UFS_DEV_CONFIGURATION);
    }

    /*
     * Initialize GOMETRY Descriptor
     */
    UFS_REG_W(s->ufsDesc.geo, GOME_MAX_NUMBER_LU, s->num_luns <= 8 ? 0 : 1);
    UFS_REG_W(s->ufsDesc.geo, GOME_SEGMENT_SIZE, UFS_SEGMENT_SIZE / 512);
    UFS_REG_W(s->ufsDesc.geo, GOME_ALLOCATION_UNIT_SIZE, 1);
    UFS_REG_W(s->ufsDesc.geo, GOME_MIN_ADDR_BLOCK_SIZE, 0x8);
    UFS_REG_W(s->ufsDesc.geo, GOME_MAX_IN_BUFFER_SIZE, 0x8);
    UFS_REG_W(s->ufsDesc.geo, GOME_MAX_OUT_BUFFER_SIZE, 0x8);

    /*
     * Allocate Unit descriptors
     */
    for (i = 0; i < s->num_luns; i++) {
        s->ufsDesc.unit[i] = g_malloc0(UFS_UNIT_DESC_SIZE);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_UNIT_INDEX, i);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_LENGTH, UFS_UNIT_DESC_SIZE);
        UFS_REG_W(s->ufsDesc.unit[i], UNIT_DESCRIPTOR_IDN, UFS_DEV_UNIT);
    }

    /*
     * Initialize Device Descriptor
     */
    UFS_REG_W(s->ufsDesc.device, DEV_LENGTH, UFS_DEV_DESC_SIZE);
    UFS_REG_W(s->ufsDesc.device, DEV_DESCRIPTOR_IDN, 0x00);
    UFS_REG_W(s->ufsDesc.device, DEV_NUMBER_WLU, 0x04);
    UFS_REG_W(s->ufsDesc.device, DEV_INIT_POWER_MODE, 0x01);
    UFS_REG_W(s->ufsDesc.device, DEV_HIGH_PRIORITY_LUN, 0x7F);
    UFS_REG_W(s->ufsDesc.device, DEV_SECURITY_LU, 0x01);
    UFS_REG_W(s->ufsDesc.device, DEV_SPEC_VERSION, 0x0310);
    UFS_REG_W(s->ufsDesc.device, DEV_UD0BASE_OFFSET, 0x16);
    UFS_REG_W(s->ufsDesc.device, DEV_UDCONFIG_PLENGTH, 0x1A);

    /*
     * Initialize Geometry Descriptor
     */
    UFS_REG_W(s->ufsDesc.geo, GOME_LENGTH, UFS_GOME_DESC_SIZE);
    UFS_REG_W(s->ufsDesc.geo, GOME_DESCRIPTOR_IDN, UFS_DEV_GEOMETRY);

    /*
     * Initialize Interconnect descriptor
     */
    UFS_REG_W(s->ufsDesc.interconnect, INTERCONNECT_LENGTH,
                UFS_INTRCON_DESC_SIZE);
    UFS_REG_W(s->ufsDesc.interconnect, INTERCONNECT_DESCRIPTOR_IDN,
                UFS_DEV_INTERCONNECT);
    UFS_REG_W(s->ufsDesc.interconnect, INTERCONNECT_BCD_UNIPRO_VERSION,
                0x0180);
    UFS_REG_W(s->ufsDesc.interconnect, INTERCONNECT_BCD_MPHY_VERSION,
                0x0410);

    /*
     * Manufacturer Name String
     */
    s->ufsDesc.manStr[0] = UFS_MAN_STR_DESC_SIZE;
    s->ufsDesc.manStr[1] = UFS_DEV_STRING;
    UFS_REG_W_2(s->ufsDesc.manStr, 2, 0x0051);  /* Q */
    UFS_REG_W_2(s->ufsDesc.manStr, 4, 0x0045);  /* E */
    UFS_REG_W_2(s->ufsDesc.manStr, 6, 0x004d);  /* M */
    UFS_REG_W_2(s->ufsDesc.manStr, 8, 0x0055);  /* U */
    UFS_REG_W_2(s->ufsDesc.manStr, 10, 0x0000); /* NULL */
    UFS_REG_W(s->ufsDesc.device, DEV_MANUFACTURER_NAME, 0);

    /*
     * Product Name String
     */
    s->ufsDesc.prodStr[0] = UFS_PROD_STR_DESC_SIZE;
    s->ufsDesc.prodStr[1] = UFS_DEV_STRING;
    UFS_REG_W_2(s->ufsDesc.prodStr, 2, 0x0055);  /* U */
    UFS_REG_W_2(s->ufsDesc.prodStr, 4, 0x0046);  /* F */
    UFS_REG_W_2(s->ufsDesc.prodStr, 6, 0x0053);  /* S */
    UFS_REG_W_2(s->ufsDesc.prodStr, 8, 0x002d);  /* - */
    UFS_REG_W_2(s->ufsDesc.prodStr, 10, 0x0044); /* D */
    UFS_REG_W_2(s->ufsDesc.prodStr, 12, 0x0045); /* E */
    UFS_REG_W_2(s->ufsDesc.prodStr, 14, 0x0056); /* V */
    UFS_REG_W_2(s->ufsDesc.prodStr, 16, 0x0000);
    UFS_REG_W(s->ufsDesc.device, DEV_PRODUCT_NAME, 1);

    /*
     * OEM ID String
     */
    s->ufsDesc.oemIdStr[0] = UFS_OEM_ID_STR_SIZE;
    s->ufsDesc.oemIdStr[1] = UFS_DEV_STRING;
    s->ufsDesc.oemIdStr[2] = 0;
    s->ufsDesc.oemIdStr[3] = 0;
    UFS_REG_W(s->ufsDesc.device, DEV_OEM_ID, 2);

    /*
     * Serial Number String
     */
    s->ufsDesc.oemIdStr[0] = UFS_OEM_ID_STR_SIZE;
    s->ufsDesc.oemIdStr[1] = UFS_SERIAL_NUM_STR_SIZE;
    s->ufsDesc.oemIdStr[2] = 0;
    s->ufsDesc.oemIdStr[3] = 0;
    UFS_REG_W(s->ufsDesc.device, DEV_SERIAL_NUMBER, 3);

    /*
     * Product Revision String
     */
    s->ufsDesc.prodRevLvlStr[0] = UFS_PROD_REV_LVL_STR_SIZE;
    s->ufsDesc.prodRevLvlStr[1] = UFS_DEV_STRING;
    UFS_REG_W_2(s->ufsDesc.prodRevLvlStr, 2, 0x0030);
    UFS_REG_W_2(s->ufsDesc.prodRevLvlStr, 4, 0x0030);
    UFS_REG_W_2(s->ufsDesc.prodRevLvlStr, 6, 0x0030);
    UFS_REG_W_2(s->ufsDesc.prodRevLvlStr, 8, 0x0030);
    UFS_REG_W_2(s->ufsDesc.prodRevLvlStr, 10, 0x0000);
    UFS_REG_W(s->ufsDesc.device, DEV_PRODUCT_REVISION_LEVEL, 4);

    /*
     * Device Health descriptor
     */
    s->ufsDesc.devHealth[0] = UFS_DEV_HEALTH_DESC_SIZE;
    s->ufsDesc.devHealth[1] = UFS_DEV_DEVICE_HEALTH;

    /*
     * Power Parameter descriptor
     */
    s->ufsDesc.pwrParam[0] = UFS_DEV_PWR_PARAM_DESC_SIZE;
    s->ufsDesc.pwrParam[1] = UFS_DEV_POWER;
    for (i = 0; i < 16; i++) {
        UFS_REG_W_2(s->ufsDesc.pwrParam, 2 + i * 2, 0x8096);
        UFS_REG_W_2(s->ufsDesc.pwrParam, 0x22 + i * 2, 0x0000);
        UFS_REG_W_2(s->ufsDesc.pwrParam, 0x42 + i * 2, 0x815E);
    }

    /*
     * Configure BOOT LUN A, B based on qdev props
     */
    UFS_REG_W(s->ufsDesc.unit[s->BootLUA], UNIT_BOOT_LUN_ID, 1);
    UFS_REG_W(s->ufsDesc.unit[s->BootLUB], UNIT_BOOT_LUN_ID, 2);
    UFS_REG_W(
      &s->ufsDesc.config[s->BootLUA / 8][CONFIG_UNIT_OFFSET(s->BootLUA % 8)],
      CONFIG_BOOT_LUN_ID, 1);
    UFS_REG_W(
      &s->ufsDesc.config[s->BootLUB / 8][CONFIG_UNIT_OFFSET(s->BootLUB % 8)],
      CONFIG_BOOT_LUN_ID, 2);

    QTAILQ_INIT(&s->taskQ);
}

static void ufsdev_unrealize(DeviceState *dev)
{
    UFSDev *s = UFS_DEV(dev);
    int i;

    g_free(s->ufsDesc.config[3]);
    g_free(s->ufsDesc.config[2]);
    g_free(s->ufsDesc.config[1]);
    g_free(s->ufsDesc.config[0]);

    for (i = 0; i < s->num_luns; i++) {
        g_free(s->ufsDesc.unit[i]);
    }
}

static void ufsdev_reset_enter(Object *obj, ResetType type)
{
    UFSDev *s = UFS_DEV(obj);
    uint32_t resp[2];
    uint64_t raw_size = 0;
    uint64_t num_alloc;
    uint8_t bs;
    int i;

    /*
     * Issue scsi_Read_Capacity10 to each lun to find its blocksize
     * and raw device size.
     */
    for (i = 0; i < s->num_luns; i++) {
        if (ufs_scsi_read_capacity10(s->ufs_scsi_target, i, (uint8_t *)resp)) {
            resp[0] = be32_to_cpu(resp[0]);
            resp[1] = be32_to_cpu(resp[1]);

            if (resp[1] < 4096) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "ufs lun %d: block size is less than 4k", i);
                g_assert_not_reached();
            }

            bs = floor(log2(resp[1]));
            num_alloc = ((resp[0] + 1ULL) * resp[1]) / UFS_SEGMENT_SIZE;
            UFS_REG_W(s->ufsDesc.unit[i], UNIT_LU_ENABLE, 1);
            UFS_REG_W(&s->ufsDesc.config[i / 8][CONFIG_UNIT_OFFSET(i % 8)],
                CONFIG_LU_ENABLE, 1);
            UFS_REG_W(s->ufsDesc.unit[i], UNIT_LOGICAL_BLOCK_SIZE,
                      bs);
            UFS_REG_W(&s->ufsDesc.config[i / 8][CONFIG_UNIT_OFFSET(i % 8)],
                CONFIG_LOGICAL_BLOCK_SIZE, bs);
            UFS_REG_W(s->ufsDesc.unit[i], UNIT_LOGICAL_BLOCK_COUNT,
                     resp[0]);
            UFS_REG_W(s->ufsDesc.unit[i], UNIT_PHY_MEM_RESOURCE_COUNT,
                     resp[0]);
            UFS_REG_W(&s->ufsDesc.config[i / 8][CONFIG_UNIT_OFFSET(i % 8)],
                CONFIG_NUM_ALLOC_UNITS, num_alloc);

            UFS_REG_W(s->ufsDesc.unit[i], UNIT_ERASE_BLOCK_SIZE, 1);
            raw_size += num_alloc * UFS_SEGMENT_SIZE;
        }
    }
    UFS_REG_W(s->ufsDesc.geo, GOME_TOTAL_RAW_DEVICE_CAPACITY, raw_size);
}

static void ufsdev_instance_init(Object *obj)
{
    UFSDev *s = UFS_DEV(obj);

    object_property_add_link(obj, "ufs-initiator", TYPE_UFSHC_IF,
                             (Object **)&s->ufs_ini,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "ufs-scsi-core", TYPE_UFS_SCSI_IF,
                             (Object **)&s->ufs_scsi_target,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_initialize_child(obj, "ufs-scsi-dev", &s->core, TYPE_UFS_SCSI_CORE);
    qdev_alias_all_properties(DEVICE(&s->core), obj);
    s->ufs_scsi_target = UFS_SCSI_IF(&s->core);
    s->bus = UFS_BUS(qbus_new(TYPE_UFS_BUS, DEVICE(obj), NULL));
}

static Property ufsdev_props[] = {
    DEFINE_PROP_UINT8("num-luns", UFSDev, num_luns, 8),
    DEFINE_PROP_UINT8("boot-lun-a", UFSDev, BootLUA, 0),
    DEFINE_PROP_UINT8("boot-lun-b", UFSDev, BootLUB, 1),
    DEFINE_PROP_UINT8("boot-lun-active", UFSDev, attr.BootLunEn, 1),
    DEFINE_PROP_UINT8("devBootEn", UFSDev, ufsDesc.device[DEV_BOOT_ENABLE], 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void ufsdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ufshcIFClass *uc = UFSHC_IF_CLASS(klass);
    ufs_scsi_if_class *usc = UFS_SCSI_IF_CLASS(klass);

    dc->realize = ufsdev_realize;
    dc->unrealize = ufsdev_unrealize;
    device_class_set_props(dc, ufsdev_props);
    rc->phases.enter = ufsdev_reset_enter;
    /*
     * ufshc interface
     */
    uc->handle_upiu = ufs_dev_receive_upiu;
    uc->handle_data = ufs_dev_receive_data;
    dc->bus_type = TYPE_UFS_BUS;
    /*
     * ufs scsi device interface
     */
    usc->handle_data = ufs_dev_receive_scsi_data;
    usc->handle_sense = ufs_dev_receive_sense_data;
    usc->get_sgl = ufs_dev_get_sgl;
}

static const TypeInfo ufsdev_info = {
    .name = TYPE_UFS_DEV,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(UFSDev),
    .instance_init = ufsdev_instance_init,
    .class_init = ufsdev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_UFSHC_IF },
        { TYPE_UFS_SCSI_IF },
        { },
    },
};

static void ufsdev_types(void)
{
    type_register_static(&ufsdev_info);
}

type_init(ufsdev_types)
