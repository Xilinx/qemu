/*
 * DwC I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2023 Google LLC
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/i3c/dwc_i3c.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/i3c/i3c.h"
#include "hw/irq.h"

/*
 * Disable event command values. sent along with a DISEC CCC to disable certain
 * events on targets.
 */
#define DISEC_HJ 0x08
#define DISEC_CR 0x02
#define DISEC_INT 0x01

/*
 * Device Roles
 */
#define DR_MASTER_ONLY 1
#define DR_PROG_MASTER_SLAVE 2
#define DR_SECONDARY_MASTER 3
#define DR_SLAVE_ONLY 4


/* I3C Device Registers */
REG32(DEVICE_CTRL,                  0x00)
    FIELD(DEVICE_CTRL, I3C_BROADCAST_ADDR_INC,    0, 1)
    FIELD(DEVICE_CTRL, I2C_SLAVE_PRESENT,         7, 1)
    FIELD(DEVICE_CTRL, HOT_JOIN_ACK_NACK_CTRL,    8, 1)
    FIELD(DEVICE_CTRL, IDLE_CNT_MULTIPLIER,       24, 2)
    FIELD(DEVICE_CTRL, SLV_ADAPT_TO_I2C_I3C_MODE, 27, 1)
    FIELD(DEVICE_CTRL, DMA_HANDSHAKE_EN,          28, 1)
    FIELD(DEVICE_CTRL, I3C_ABORT,                 29, 1)
    FIELD(DEVICE_CTRL, I3C_RESUME,                30, 1)
    FIELD(DEVICE_CTRL, I3C_EN,                    31, 1)
REG32(DEVICE_ADDR,                  0x04)
    FIELD(DEVICE_ADDR, STATIC_ADDR,         0, 7)
    FIELD(DEVICE_ADDR, STATIC_ADDR_VALID,   15, 1)
    FIELD(DEVICE_ADDR, DYNAMIC_ADDR,        16, 7)
    FIELD(DEVICE_ADDR, DYNAMIC_ADDR_VALID,  15, 1)
REG32(HW_CAPABILITY,                0x08)
    FIELD(HW_CAPABILITY, DEVICE_ROLE_CONFIG, 0, 3)
    FIELD(HW_CAPABILITY, HDR_DDR, 3, 1)
    FIELD(HW_CAPABILITY, HDR_TS,  4, 1)
    FIELD(HW_CAPABILITY, CLOCK_PERIOD, 5, 6)
    FIELD(HW_CAPABILITY, HDR_TX_CLOCK_PERIOD, 11, 6)
    FIELD(HW_CAPABILITY, DMA_EN, 17, 1)
    FIELD(HW_CAPABILITY, SLV_HJ_CAP, 18, 1)
    FIELD(HW_CAPABILITY, SLV_IBI_CAP, 19, 1)
REG32(COMMAND_QUEUE_PORT,           0x0c)
    FIELD(COMMAND_QUEUE_PORT, CMD_ATTR, 0, 3)
    /* Transfer command structure */
    FIELD(COMMAND_QUEUE_PORT, TID, 3, 4)
    FIELD(COMMAND_QUEUE_PORT, CMD, 7, 8)
    FIELD(COMMAND_QUEUE_PORT, CP, 15, 1)
    FIELD(COMMAND_QUEUE_PORT, DEV_INDEX, 16, 5)
    FIELD(COMMAND_QUEUE_PORT, SPEED, 21, 3)
    FIELD(COMMAND_QUEUE_PORT, ROC, 26, 1)
    FIELD(COMMAND_QUEUE_PORT, SDAP, 27, 1)
    FIELD(COMMAND_QUEUE_PORT, RNW, 28, 1)
    FIELD(COMMAND_QUEUE_PORT, TOC, 30, 1)
    FIELD(COMMAND_QUEUE_PORT, PEC, 31, 1)
    /* Transfer argument data structure */
    FIELD(COMMAND_QUEUE_PORT, DB, 8, 8)
    FIELD(COMMAND_QUEUE_PORT, DL, 16, 16)
    /* Short data argument data structure */
    FIELD(COMMAND_QUEUE_PORT, BYTE_STRB, 3, 3)
    FIELD(COMMAND_QUEUE_PORT, BYTE0, 8, 8)
    FIELD(COMMAND_QUEUE_PORT, BYTE1, 16, 8)
    FIELD(COMMAND_QUEUE_PORT, BYTE2, 24, 8)
    /* Address assignment command structure */
    /*
     * bits 3..21 and 26..31 are the same as the transfer command structure, or
     * marked as reserved.
     */
    FIELD(COMMAND_QUEUE_PORT, DEV_COUNT, 21, 3)
REG32(RESPONSE_QUEUE_PORT,          0x10)
    FIELD(RESPONSE_QUEUE_PORT, DL, 0, 16)
    FIELD(RESPONSE_QUEUE_PORT, CCCT, 16, 8)
    FIELD(RESPONSE_QUEUE_PORT, TID, 24, 4)
    FIELD(RESPONSE_QUEUE_PORT, RX_RSP, 27, 1)
    FIELD(RESPONSE_QUEUE_PORT, ERR_STATUS, 28, 4)
REG32(RX_TX_DATA_PORT,              0x14)
REG32(IBI_QUEUE_STATUS,             0x18)
    FIELD(IBI_QUEUE_STATUS, IBI_DATA_LEN,   0, 8)
    FIELD(IBI_QUEUE_STATUS, IBI_ID,         8, 8)
    FIELD(IBI_QUEUE_STATUS, LAST_STATUS,  24, 1)
    FIELD(IBI_QUEUE_STATUS, ERROR,  30, 1)
    FIELD(IBI_QUEUE_STATUS, IBI_STATUS,  31, 1)
REG32(IBI_QUEUE_DATA,               0x18)
REG32(QUEUE_THLD_CTRL,              0x1c)
    FIELD(QUEUE_THLD_CTRL, CMD_BUF_EMPTY_THLD,  0, 8);
    FIELD(QUEUE_THLD_CTRL, RESP_BUF_THLD, 8, 8);
    FIELD(QUEUE_THLD_CTRL, IBI_DATA_THLD, 16, 8);
    FIELD(QUEUE_THLD_CTRL, IBI_STATUS_THLD,     24, 8);
REG32(DATA_BUFFER_THLD_CTRL,        0x20)
    FIELD(DATA_BUFFER_THLD_CTRL, TX_BUF_THLD,   0, 3)
    FIELD(DATA_BUFFER_THLD_CTRL, RX_BUF_THLD,   10, 3)
    FIELD(DATA_BUFFER_THLD_CTRL, TX_START_THLD, 16, 3)
    FIELD(DATA_BUFFER_THLD_CTRL, RX_START_THLD, 24, 3)
REG32(IBI_QUEUE_CTRL,               0x24)
    FIELD(IBI_QUEUE_CTRL, NOTIFY_REJECTED_HOT_JOIN,   0, 1)
    FIELD(IBI_QUEUE_CTRL, NOTIFY_REJECTED_MASTER_REQ, 1, 1)
    FIELD(IBI_QUEUE_CTRL, NOTIFY_REJECTED_SLAVE_IRQ,  3, 1)
REG32(IBI_MR_REQ_REJECT,            0x2c)
REG32(IBI_SIR_REQ_REJECT,           0x30)
REG32(RESET_CTRL,                   0x34)
    FIELD(RESET_CTRL, CORE_RESET,       0, 1)
    FIELD(RESET_CTRL, CMD_QUEUE_RESET,  1, 1)
    FIELD(RESET_CTRL, RESP_QUEUE_RESET, 2, 1)
    FIELD(RESET_CTRL, TX_BUF_RESET,     3, 1)
    FIELD(RESET_CTRL, RX_BUF_RESET,     4, 1)
    FIELD(RESET_CTRL, IBI_QUEUE_RESET,  5, 1)
REG32(SLV_EVENT_CTRL,               0x38)
    FIELD(SLV_EVENT_CTRL, SLV_INTERRUPT,      0, 1)
    FIELD(SLV_EVENT_CTRL, MASTER_INTERRUPT,   1, 1)
    FIELD(SLV_EVENT_CTRL, HOT_JOIN_INTERRUPT, 3, 1)
    FIELD(SLV_EVENT_CTRL, ACTIVITY_STATE,     4, 2)
    FIELD(SLV_EVENT_CTRL, MRL_UPDATED,        6, 1)
    FIELD(SLV_EVENT_CTRL, MWL_UPDATED,        7, 1)
REG32(INTR_STATUS,                  0x3c)
    FIELD(INTR_STATUS, TX_THLD,           0, 1)
    FIELD(INTR_STATUS, RX_THLD,           1, 1)
    FIELD(INTR_STATUS, IBI_THLD,          2, 1)
    FIELD(INTR_STATUS, CMD_QUEUE_RDY,     3, 1)
    FIELD(INTR_STATUS, RESP_RDY,          4, 1)
    FIELD(INTR_STATUS, TRANSFER_ABORT,    5, 1)
    FIELD(INTR_STATUS, CCC_UPDATED,       6, 1)
    FIELD(INTR_STATUS, DYN_ADDR_ASSGN,    8, 1)
    FIELD(INTR_STATUS, TRANSFER_ERR,      9, 1)
    FIELD(INTR_STATUS, DEFSLV,            10, 1)
    FIELD(INTR_STATUS, READ_REQ_RECV,     11, 1)
    FIELD(INTR_STATUS, IBI_UPDATED,       12, 1)
    FIELD(INTR_STATUS, BUSOWNER_UPDATED,  13, 1)
REG32(INTR_STATUS_EN,               0x40)
    FIELD(INTR_STATUS_EN, TX_THLD,          0, 1)
    FIELD(INTR_STATUS_EN, RX_THLD,          1, 1)
    FIELD(INTR_STATUS_EN, IBI_THLD,         2, 1)
    FIELD(INTR_STATUS_EN, CMD_QUEUE_RDY,    3, 1)
    FIELD(INTR_STATUS_EN, RESP_RDY,         4, 1)
    FIELD(INTR_STATUS_EN, TRANSFER_ABORT,   5, 1)
    FIELD(INTR_STATUS_EN, CCC_UPDATED,      6, 1)
    FIELD(INTR_STATUS_EN, DYN_ADDR_ASSGN,   8, 1)
    FIELD(INTR_STATUS_EN, TRANSFER_ERR,     9, 1)
    FIELD(INTR_STATUS_EN, DEFSLV,           10, 1)
    FIELD(INTR_STATUS_EN, READ_REQ_RECV,    11, 1)
    FIELD(INTR_STATUS_EN, IBI_UPDATED,      12, 1)
    FIELD(INTR_STATUS_EN, BUSOWNER_UPDATED, 13, 1)
REG32(INTR_SIGNAL_EN,               0x44)
    FIELD(INTR_SIGNAL_EN, TX_THLD,          0, 1)
    FIELD(INTR_SIGNAL_EN, RX_THLD,          1, 1)
    FIELD(INTR_SIGNAL_EN, IBI_THLD,         2, 1)
    FIELD(INTR_SIGNAL_EN, CMD_QUEUE_RDY,    3, 1)
    FIELD(INTR_SIGNAL_EN, RESP_RDY,         4, 1)
    FIELD(INTR_SIGNAL_EN, TRANSFER_ABORT,   5, 1)
    FIELD(INTR_SIGNAL_EN, CCC_UPDATED,      6, 1)
    FIELD(INTR_SIGNAL_EN, DYN_ADDR_ASSGN,   8, 1)
    FIELD(INTR_SIGNAL_EN, TRANSFER_ERR,     9, 1)
    FIELD(INTR_SIGNAL_EN, DEFSLV,           10, 1)
    FIELD(INTR_SIGNAL_EN, READ_REQ_RECV,    11, 1)
    FIELD(INTR_SIGNAL_EN, IBI_UPDATED,      12, 1)
    FIELD(INTR_SIGNAL_EN, BUSOWNER_UPDATED, 13, 1)
REG32(INTR_FORCE,                   0x48)
    FIELD(INTR_FORCE, TX_THLD,          0, 1)
    FIELD(INTR_FORCE, RX_THLD,          1, 1)
    FIELD(INTR_FORCE, IBI_THLD,         2, 1)
    FIELD(INTR_FORCE, CMD_QUEUE_RDY,    3, 1)
    FIELD(INTR_FORCE, RESP_RDY,         4, 1)
    FIELD(INTR_FORCE, TRANSFER_ABORT,   5, 1)
    FIELD(INTR_FORCE, CCC_UPDATED,      6, 1)
    FIELD(INTR_FORCE, DYN_ADDR_ASSGN,   8, 1)
    FIELD(INTR_FORCE, TRANSFER_ERR,     9, 1)
    FIELD(INTR_FORCE, DEFSLV,           10, 1)
    FIELD(INTR_FORCE, READ_REQ_RECV,    11, 1)
    FIELD(INTR_FORCE, IBI_UPDATED,      12, 1)
    FIELD(INTR_FORCE, BUSOWNER_UPDATED, 13, 1)
REG32(QUEUE_STATUS_LEVEL,           0x4c)
    FIELD(QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,  0, 8)
    FIELD(QUEUE_STATUS_LEVEL, RESP_BUF_BLR,         8, 8)
    FIELD(QUEUE_STATUS_LEVEL, IBI_BUF_BLR,          16, 8)
    FIELD(QUEUE_STATUS_LEVEL, IBI_STATUS_CNT,       24, 5)
REG32(DATA_BUFFER_STATUS_LEVEL,     0x50)
    FIELD(DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC, 0, 8)
    FIELD(DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,       16, 8)
REG32(PRESENT_STATE,                0x54)
    FIELD(PRESENT_STATE, SCL_LINE_SIGNAL_LEVEL, 0, 1)
    FIELD(PRESENT_STATE, SDA_LINE_SIGNAL_LEVEL, 1, 1)
    FIELD(PRESENT_STATE, CURRENT_MASTER,        2, 1)
    FIELD(PRESENT_STATE, CM_TFR_STATUS,         8, 6)
    FIELD(PRESENT_STATE, CM_TFR_ST_STATUS,      16, 6)
    FIELD(PRESENT_STATE, CMD_TID,               24, 4)
REG32(CCC_DEVICE_STATUS,            0x58)
    FIELD(CCC_DEVICE_STATUS, PENDING_INTR,      0, 4)
    FIELD(CCC_DEVICE_STATUS, PROTOCOL_ERR,      4, 2)
    FIELD(CCC_DEVICE_STATUS, ACTIVITY_MODE,     6, 2)
    FIELD(CCC_DEVICE_STATUS, UNDER_ERR,         8, 1)
    FIELD(CCC_DEVICE_STATUS, SLV_BUSY,          9, 1)
    FIELD(CCC_DEVICE_STATUS, OVERFLOW_ERR,      10, 1)
    FIELD(CCC_DEVICE_STATUS, DATA_NOT_READY,    11, 1)
    FIELD(CCC_DEVICE_STATUS, BUFFER_NOT_AVAIL,  12, 1)
REG32(DEVICE_ADDR_TABLE_POINTER,    0x5c)
    FIELD(DEVICE_ADDR_TABLE_POINTER, DEPTH, 16, 16)
    FIELD(DEVICE_ADDR_TABLE_POINTER, ADDR,  0,  16)
REG32(DEV_CHAR_TABLE_POINTER,       0x60)
    FIELD(DEV_CHAR_TABLE_POINTER, P_DEV_CHAR_TABLE_START_ADDR,  0, 12)
    FIELD(DEV_CHAR_TABLE_POINTER, DEV_CHAR_TABLE_DEPTH,         12, 7)
    FIELD(DEV_CHAR_TABLE_POINTER, PRESENT_DEV_CHAR_TABLE_INDEX, 19, 3)
REG32(VENDOR_SPECIFIC_REG_POINTER,  0x6c)
    FIELD(VENDOR_SPECIFIC_REG_POINTER, P_VENDOR_REG_START_ADDR, 0, 16)
REG32(SLV_MIPI_ID_VALUE,           0x70)
    FIELD(SLV_MIPI_ID_VALUE, SLV_PROV_ID_SEL, 0, 1)
    FIELD(SLV_MIPI_ID_VALUE, SLV_MIPI_MFG_ID, 1, 15)
REG32(SLV_PID_VALUE,                0x74)
    FIELD(SLV_PID_VALUE, SLV_PID_DCR, 0, 12)
    FIELD(SLV_PID_VALUE, SLV_INST_ID, 12, 4)
    FIELD(SLV_PID_VALUE, SLV_PART_ID, 16, 16)
REG32(SLV_CHAR_CTRL,                0x78)
    FIELD(SLV_CHAR_CTRL, BCR,     0, 8)
    FIELD(SLV_CHAR_CTRL, DCR,     8, 8)
    FIELD(SLV_CHAR_CTRL, HDR_CAP, 16, 8)
REG32(SLV_MAX_LEN,                  0x7c)
    FIELD(SLV_MAX_LEN, MWL, 0, 16)
    FIELD(SLV_MAX_LEN, MRL, 16, 16)
REG32(MAX_READ_TURNAROUND,          0x80)
REG32(MAX_DATA_SPEED,               0x84)
    FIELD(MAX_DATA_SPEED, MXDS_MAX_WR_SPEED, 0, 3)
    FIELD(MAX_DATA_SPEED, MXDS_MAX_RD_SPEED, 8, 3)
    FIELD(MAX_DATA_SPEED, MXDS_CLK_DATA_TURN, 16, 3)
REG32(SLV_DEBUG_STATUS,             0x88)
REG32(SLV_INTR_REQ,                 0x8c)
    FIELD(SLV_INTR_REQ, SIR,      0, 1)
    FIELD(SLV_INTR_REQ, SIR_CTRL, 1, 2)
    FIELD(SLV_INTR_REQ, MIR,      3, 1)
    FIELD(SLV_INTR_REQ, IBI_STS,  8, 2)
REG32(SLV_TSX_SYMBL_TIMING,         0x90)
    FIELD(SLV_TSX_SYMBL_TIMING, SLV_TSX_SYMBL_CNT, 0, 6)
REG32(DEVICE_CTRL_EXTENDED,         0xb0)
    FIELD(DEVICE_CTRL_EXTENDED, MODE, 0, 2)
    FIELD(DEVICE_CTRL_EXTENDED, REQMST_ACK_CTRL, 3, 1)
REG32(SCL_I3C_OD_TIMING,            0xb4)
    FIELD(SCL_I3C_OD_TIMING, I3C_OD_LCNT, 0, 8)
    FIELD(SCL_I3C_OD_TIMING, I3C_OD_HCNT, 16, 8)
REG32(SCL_I3C_PP_TIMING,            0xb8)
    FIELD(SCL_I3C_PP_TIMING, I3C_PP_LCNT, 0, 8)
    FIELD(SCL_I3C_PP_TIMING, I3C_PP_HCNT, 16, 8)
REG32(SCL_I2C_FM_TIMING,            0xbc)
REG32(SCL_I2C_FMP_TIMING,           0xc0)
    FIELD(SCL_I2C_FMP_TIMING, I2C_FMP_LCNT, 0, 16)
    FIELD(SCL_I2C_FMP_TIMING, I2C_FMP_HCNT, 16, 8)
REG32(SCL_EXT_LCNT_TIMING,          0xc8)
REG32(SCL_EXT_TERMN_LCNT_TIMING,    0xcc)
REG32(BUS_FREE_TIMING,              0xd4)
REG32(BUS_IDLE_TIMING,              0xd8)
    FIELD(BUS_IDLE_TIMING, BUS_IDLE_TIME, 0, 20)
REG32(I3C_VER_ID,                   0xe0)
REG32(I3C_VER_TYPE,                 0xe4)
REG32(EXTENDED_CAPABILITY,          0xe8)
    FIELD(EXTENDED_CAPABILITY, APP_IF_MODE,       0, 2)
    FIELD(EXTENDED_CAPABILITY, APP_IF_DATA_WIDTH, 2, 2)
    FIELD(EXTENDED_CAPABILITY, OPERATION_MODE,    4, 2)
    FIELD(EXTENDED_CAPABILITY, CLK_PERIOD,        8, 6)
REG32(SLAVE_CONFIG,                 0xec)
    FIELD(SLAVE_CONFIG, DMA_EN,     0, 1)
    FIELD(SLAVE_CONFIG, HJ_CAP,     0, 1)
    FIELD(SLAVE_CONFIG, CLK_PERIOD, 2, 14)
/*
 * Device characteristic table fields
 * Offset of Char table is pointed by DEV_CHAR_TABLE_POINTER
 * reg.
 */
#define R_DEV_CHAR_TBL(s)  \
        (ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER, \
                          P_DEV_CHAR_TABLE_START_ADDR) >> 2)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC1, 0x0)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC2, 0x4)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC2, MSB_PID, 0, 16)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC3, 0x8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC3, DCR, 0, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC3, BCR, 8, 8)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC4, 0xc)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC4, DEV_DYNAMIC_ADDR, 0, 8)
/*
 * Dev addr table fields
 * Offset of Address-table is pointed by DEVICE_ADDR_TABLE_POINTER
 * reg.
 */
#define R_DEV_ADDR_TBL(s) \
        (ARRAY_FIELD_EX32(s->regs, DEVICE_ADDR_TABLE_POINTER, ADDR) >> 2)
REG32(DEVICE_ADDR_TABLE_LOC1, 0x0)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_STATIC_ADDR, 0, 7)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_PEC_EN, 11, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_WITH_DATA, 12, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, SIR_REJECT, 13, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, MR_REJECT, 14, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_DYNAMIC_ADDR, 16, 8)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_ADDR_MASK, 24, 2)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_NACK_RETRY_CNT, 29, 2)
    FIELD(DEVICE_ADDR_TABLE_LOC1, LEGACY_I2C_DEVICE, 31, 1)

static void dwc_i3c_device_cmd_queue_execute(DwcI3CDevice *s);

static const uint32_t ast2600_i3c_device_resets[DWC_I3C_NR_REGS] = {
    [R_HW_CAPABILITY]               = 0x000e00b8,
    [R_QUEUE_THLD_CTRL]             = 0x01000101,
    [R_DATA_BUFFER_THLD_CTRL]       = 0x01010100,
    [R_SLV_EVENT_CTRL]              = 0x0000000b,
    [R_QUEUE_STATUS_LEVEL]          = 0x00000002,
    [R_DATA_BUFFER_STATUS_LEVEL]    = 0x00000010,
    [R_PRESENT_STATE]               = 0x00000003,
    [R_I3C_VER_ID]                  = 0x3130302a,
    [R_I3C_VER_TYPE]                = 0x6c633033,
    [R_DEV_CHAR_TABLE_POINTER]      = 0x00000200,
    [R_SLV_CHAR_CTRL]               = 0x00010000,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0x000000b0,
    [R_SLV_MAX_LEN]                 = 0x00ff00ff,
    [R_SLV_TSX_SYMBL_TIMING]        = 0x0000003f,
    [R_SCL_I3C_OD_TIMING]           = 0x000a0010,
    [R_SCL_I3C_PP_TIMING]           = 0x000a000a,
    [R_SCL_I2C_FM_TIMING]           = 0x00100010,
    [R_SCL_I2C_FMP_TIMING]          = 0x00100010,
    [R_SCL_EXT_LCNT_TIMING]         = 0x20202020,
    [R_SCL_EXT_TERMN_LCNT_TIMING]   = 0x00300000,
    [R_BUS_FREE_TIMING]             = 0x00200020,
    [R_BUS_IDLE_TIMING]             = 0x00000020,
    [R_EXTENDED_CAPABILITY]         = 0x00000239,
    [R_SLAVE_CONFIG]                = 0x00000023,
};

static const uint32_t ast2600_i3c_device_ro[DWC_I3C_NR_REGS] = {
    [R_DEVICE_CTRL]                 = 0x04fffe00,
    [R_DEVICE_ADDR]                 = 0x7f807f80,
    [R_HW_CAPABILITY]               = 0xffffffff,
    [R_IBI_QUEUE_STATUS]            = 0xffffffff,
    [R_DATA_BUFFER_THLD_CTRL]       = 0xf8f8f8f8,
    [R_IBI_QUEUE_CTRL]              = 0xfffffff0,
    [R_RESET_CTRL]                  = 0xffffffc0,
    [R_SLV_EVENT_CTRL]              = 0xffffff3f,
    [R_INTR_STATUS]                 = 0xffff809f,
    [R_INTR_STATUS_EN]              = 0xffff8080,
    [R_INTR_SIGNAL_EN]              = 0xffff8080,
    [R_INTR_FORCE]                  = 0xffff8000,
    [R_QUEUE_STATUS_LEVEL]          = 0xffffffff,
    [R_DATA_BUFFER_STATUS_LEVEL]    = 0xffffffff,
    [R_PRESENT_STATE]               = 0xffffffff,
    [R_CCC_DEVICE_STATUS]           = 0xffffffff,
    [R_I3C_VER_ID]                  = 0xffffffff,
    [R_I3C_VER_TYPE]                = 0xffffffff,
    [R_DEVICE_ADDR_TABLE_POINTER]   = 0xffffffff,
    [R_DEV_CHAR_TABLE_POINTER]      = 0xffcbffff,
    [R_SLV_PID_VALUE]               = 0xffff0fff,
    [R_SLV_CHAR_CTRL]               = 0xffffffff,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0xffffffff,
    [R_SLV_MAX_LEN]                 = 0xffffffff,
    [R_MAX_READ_TURNAROUND]         = 0xffffffff,
    [R_MAX_DATA_SPEED]              = 0xffffffff,
    [R_SLV_INTR_REQ]                = 0xfffffff0,
    [R_SLV_TSX_SYMBL_TIMING]        = 0xffffffc0,
    [R_DEVICE_CTRL_EXTENDED]        = 0xfffffff8,
    [R_SCL_I3C_OD_TIMING]           = 0xff00ff00,
    [R_SCL_I3C_PP_TIMING]           = 0xff00ff00,
    [R_SCL_I2C_FMP_TIMING]          = 0xff000000,
    [R_SCL_EXT_TERMN_LCNT_TIMING]   = 0x0000fff0,
    [R_BUS_IDLE_TIMING]             = 0xfff00000,
    [R_EXTENDED_CAPABILITY]         = 0xffffffff,
    [R_SLAVE_CONFIG]                = 0xffffffff,
};

static inline bool dwc_i3c_device_has_entdaa(DwcI3CDevice *s)
{
    return ARRAY_FIELD_EX32(s->regs, HW_CAPABILITY, DEVICE_ROLE_CONFIG) <
                            DR_SLAVE_ONLY;
}

static inline bool dwc_i3c_device_has_hdr_ts(DwcI3CDevice *s)
{
    return ARRAY_FIELD_EX32(s->regs, HW_CAPABILITY, HDR_TS);
}

static inline bool dwc_i3c_device_has_hdr_ddr(DwcI3CDevice *s)
{
    return ARRAY_FIELD_EX32(s->regs, HW_CAPABILITY, HDR_DDR);
}

static inline bool dwc_i3c_device_can_transmit(DwcI3CDevice *s)
{
    /*
     * We can only transmit if we're enabled and the resume bit is cleared.
     * The resume bit is set on a transaction error, and software must clear it.
     */
    return ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL, I3C_EN) &&
           !ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL, I3C_RESUME);
}

static inline uint8_t dwc_i3c_device_fifo_threshold_from_reg(uint8_t regval)
{
    return regval = regval ? (2 << regval) : 1;
}

static inline uint8_t dwc_i3c_device_ibi_slice_size(DwcI3CDevice *s)
{
    uint8_t ibi_slice_size = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                              IBI_DATA_THLD);
    /* The minimum supported slice size is 4 bytes. */
    if (ibi_slice_size == 0) {
        ibi_slice_size = 1;
    }
    ibi_slice_size *= sizeof(uint32_t);
    /* maximum supported size is 63 bytes. */
    if (ibi_slice_size >= 64) {
        ibi_slice_size = 63;
    }

    return ibi_slice_size;
}

static inline bool dwc_i3c_device_role_master(DwcI3CDevice *s)
{
    return s->cfg.device_role == DR_MASTER_ONLY;
}

static uint16_t dwc_i3c_device_cmd_num_tx_bytes(DwcI3CCmdQueueData arg)
{
    uint8_t bs;
    uint16_t data_len = 0;
    if (arg.transfer_cmd.cmd_attr == DWC_I3C_CMD_ATTR_SHORT_DATA_ARG) {
        bs = arg.short_arg.byte_strb;
        switch (bs) {
        case 0x7:
            data_len = 3;
            break;
        case 0x3:
            data_len = 2;
            break;
        case 0x1:
            data_len = 1;
            break;
        case 0:
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "Invalid byte strobe 0x%x\n", bs);
            break;
        };
    } else {
      data_len = arg.transfer_arg.data_len;
    }
    return data_len;
}

static void dwc_i3c_device_update_irq(DwcI3CDevice *s)
{
    bool level = !!(s->regs[R_INTR_SIGNAL_EN] & s->regs[R_INTR_STATUS]);
    qemu_set_irq(s->irq, level);
}

static void dwc_i3c_device_end_transfer(DwcI3CDevice *s, bool is_i2c)
{
    if (is_i2c) {
        legacy_i2c_end_transfer(s->bus);
    } else {
        i3c_end_transfer(s->bus);
    }
}

static int dwc_i3c_device_send_start(DwcI3CDevice *s, uint8_t addr,
                                        bool is_recv, bool is_i2c)
{
    int ret;

    if (is_i2c) {
        ret = legacy_i2c_start_transfer(s->bus, addr, is_recv);
    } else {
        ret = i3c_start_transfer(s->bus, addr, is_recv);
    }
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NACKed on TX with addr 0x%.2x\n",
                      object_get_canonical_path(OBJECT(s)), addr);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_HALT);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DWC_I3C_TRANSFER_STATUS_HALT);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ERR, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_CTRL, I3C_RESUME, 1);
    }

    return ret;
}

static int dwc_i3c_device_send(DwcI3CDevice *s, const uint8_t *data,
                                  uint32_t num_to_send, uint32_t *num_sent,
                                  bool is_i2c)
{
    int ret;
    uint32_t i;

    *num_sent = 0;
    if (is_i2c) {
        /* Legacy I2C must be byte-by-byte. */
        for (i = 0; i < num_to_send; i++) {
            ret = legacy_i2c_send(s->bus, data[i]);
            if (ret) {
                break;
            }
            (*num_sent)++;
        }
    } else {
        ret = i3c_send(s->bus, data, num_to_send, num_sent);
    }
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NACKed sending byte 0x%.2x\n",
                      object_get_canonical_path(OBJECT(s)), data[*num_sent]);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_HALT);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DWC_I3C_TRANSFER_STATUS_HALT);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ERR, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_CTRL, I3C_RESUME, 1);
    }

    trace_dwc_i3c_device_send(s->id, *num_sent);

    return ret;
}

static int dwc_i3c_device_send_byte(DwcI3CDevice *s, uint8_t byte,
                                       bool is_i2c)
{
    /*
     * Ignored, the caller will know if we sent 0 or 1 bytes depending on if
     * we were ACKed/NACKed.
     */
    uint32_t num_sent;
    return dwc_i3c_device_send(s, &byte, 1, &num_sent, is_i2c);
}

static int dwc_i3c_device_recv_data(DwcI3CDevice *s, bool is_i2c,
                                       uint8_t *data, uint16_t num_to_read,
                                       uint32_t *num_read)
{
    int ret;

    if (is_i2c) {
        for (uint16_t i = 0; i < num_to_read; i++) {
            data[i] = legacy_i2c_recv(s->bus);
        }
        /* I2C devices can neither NACK a read, nor end transfers early. */
        *num_read = num_to_read;
        trace_dwc_i3c_device_recv_data(s->id, *num_read);
        return 0;
    }
    /* I3C devices can NACK if the controller sends an unsupported CCC. */
    ret = i3c_recv(s->bus, data, num_to_read, num_read);
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NACKed receiving byte\n",
                      object_get_canonical_path(OBJECT(s)));
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_HALT);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DWC_I3C_TRANSFER_STATUS_HALT);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ERR, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_CTRL, I3C_RESUME, 1);
    }

    trace_dwc_i3c_device_recv_data(s->id, *num_read);

    return ret;
}

static inline void dwc_i3c_device_ctrl_w(DwcI3CDevice *s,
                                                   uint32_t val)
{
    /*
     * If the user is setting I3C_RESUME, the controller was halted.
     * Try and resume execution and leave the bit cleared.
     */
    if (FIELD_EX32(val, DEVICE_CTRL, I3C_RESUME)) {
        dwc_i3c_device_cmd_queue_execute(s);
        val = FIELD_DP32(val, DEVICE_CTRL, I3C_RESUME, 0);
    }
    /*
     * I3C_ABORT being set sends an I3C STOP. It's cleared when the STOP is
     * sent.
     */
    if (FIELD_EX32(val, DEVICE_CTRL, I3C_ABORT)) {
        dwc_i3c_device_end_transfer(s, /*is_i2c=*/true);
        dwc_i3c_device_end_transfer(s, /*is_i2c=*/false);
        val = FIELD_DP32(val, DEVICE_CTRL, I3C_ABORT, 0);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TRANSFER_ABORT, 1);
        dwc_i3c_device_update_irq(s);
    }
    /* Update present state. */
    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                     DWC_I3C_TRANSFER_STATE_IDLE);
    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                     DWC_I3C_TRANSFER_STATUS_IDLE);

    s->regs[R_DEVICE_CTRL] = val;
}

static void dwc_i3c_device_addr_w(DwcI3CDevice *s, uint32_t val)
{
    s->regs[R_DEVICE_ADDR] = val;
    s->cfg.slv_static_addr_en = FIELD_EX32(val, DEVICE_ADDR, STATIC_ADDR_VALID);

    if (s->cfg.slv_static_addr_en &&
        s->cfg.device_role > DR_PROG_MASTER_SLAVE && s->i3c_target) {
        s->i3c_target->static_address = FIELD_EX32(val, DEVICE_ADDR,
                                        STATIC_ADDR);
    }
}

static inline bool dwc_i3c_device_target_is_i2c(DwcI3CDevice *s,
                                                   uint16_t offset)
{
    uint16_t dev_index = R_DEV_ADDR_TBL(s) + offset;
    return FIELD_EX32(s->regs[dev_index], DEVICE_ADDR_TABLE_LOC1,
                   LEGACY_I2C_DEVICE);
}

static uint8_t dwc_i3c_device_target_addr(DwcI3CDevice *s,
                                             uint16_t offset)
{
    if (offset > s->cfg.num_devices) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Device addr table offset %d out of "
                      "bounds\n", object_get_canonical_path(OBJECT(s)), offset);
        /* If we're out of bounds, return an address of 0. */
        return 0;
    }

    uint16_t dev_index = R_DEV_ADDR_TBL(s) + offset;
    /* I2C devices use a static address. */
    if (dwc_i3c_device_target_is_i2c(s, offset)) {
        return FIELD_EX32(s->regs[dev_index], DEVICE_ADDR_TABLE_LOC1,
                          DEV_STATIC_ADDR);
    }
    return FIELD_EX32(s->regs[dev_index], DEVICE_ADDR_TABLE_LOC1,
                      DEV_DYNAMIC_ADDR) & 0x7f;
}

static int dwc_i3c_device_addr_table_index_from_addr(DwcI3CDevice *s,
                                                        uint8_t addr)
{
    uint8_t table_size = ARRAY_FIELD_EX32(s->regs, DEVICE_ADDR_TABLE_POINTER,
                                          DEPTH);
    for (uint8_t i = 0; i < table_size; i++) {
        if (dwc_i3c_device_target_addr(s, i) == addr) {
            return i;
        }
    }
    return -1;
}

static void dwc_i3c_device_send_disec(DwcI3CDevice *s)
{
    uint8_t ccc = I3C_CCC_DISEC;
    if (s->ibi_data.send_direct_disec) {
        ccc = I3C_CCCD_DISEC;
    }

    dwc_i3c_device_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                 /*is_i2c=*/false);
    dwc_i3c_device_send_byte(s, ccc, /*is_i2c=*/false);
    if (s->ibi_data.send_direct_disec) {
        dwc_i3c_device_send_start(s, s->ibi_data.disec_addr,
                                     /*is_recv=*/false, /*is_i2c=*/false);
    }
    dwc_i3c_device_send_byte(s, s->ibi_data.disec_byte, /*is_i2c=*/false);
}

static int dwc_i3c_device_handle_hj(DwcI3CDevice *s)
{
    if (ARRAY_FIELD_EX32(s->regs, IBI_QUEUE_CTRL, NOTIFY_REJECTED_HOT_JOIN)) {
        s->ibi_data.notify_ibi_nack = true;
    }

    bool nack_and_disable = ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL,
                                             HOT_JOIN_ACK_NACK_CTRL);
    if (nack_and_disable) {
        s->ibi_data.ibi_queue_status = FIELD_DP32(s->ibi_data.ibi_queue_status,
                                                  IBI_QUEUE_STATUS,
                                                  IBI_STATUS, 1);
        s->ibi_data.ibi_nacked = true;
        s->ibi_data.disec_byte = DISEC_HJ;
        return -1;
    }
    return 0;
}

static int dwc_i3c_device_handle_ctlr_req(DwcI3CDevice *s, uint8_t addr)
{
    if (ARRAY_FIELD_EX32(s->regs, IBI_QUEUE_CTRL, NOTIFY_REJECTED_MASTER_REQ)) {
        s->ibi_data.notify_ibi_nack = true;
    }

    int table_offset = dwc_i3c_device_addr_table_index_from_addr(s, addr);
    /* Doesn't exist in the table, NACK it, don't DISEC. */
    if (table_offset < 0) {
        return -1;
    }

    table_offset += R_DEV_ADDR_TBL(s);
    if (FIELD_EX32(s->regs[table_offset], DEVICE_ADDR_TABLE_LOC1, MR_REJECT)) {
        s->ibi_data.ibi_queue_status = FIELD_DP32(s->ibi_data.ibi_queue_status,
                                                  IBI_QUEUE_STATUS,
                                                  IBI_STATUS, 1);
        s->ibi_data.ibi_nacked = true;
        s->ibi_data.disec_addr = addr;
        /* Tell the requester to disable controller role requests. */
        s->ibi_data.disec_byte = DISEC_CR;
        s->ibi_data.send_direct_disec = true;
        return -1;
    }
    return 0;
}

static int dwc_i3c_device_handle_targ_irq(DwcI3CDevice *s, uint8_t addr)
{
    if (ARRAY_FIELD_EX32(s->regs, IBI_QUEUE_CTRL, NOTIFY_REJECTED_SLAVE_IRQ)) {
        s->ibi_data.notify_ibi_nack = true;
    }

    int table_offset = dwc_i3c_device_addr_table_index_from_addr(s, addr);
    /* Doesn't exist in the table, NACK it, don't DISEC. */
    if (table_offset < 0) {
        return -1;
    }

    table_offset += R_DEV_ADDR_TBL(s);
    if (FIELD_EX32(s->regs[table_offset], DEVICE_ADDR_TABLE_LOC1, SIR_REJECT)) {
        s->ibi_data.ibi_queue_status = FIELD_DP32(s->ibi_data.ibi_queue_status,
                                                  IBI_QUEUE_STATUS,
                                                  IBI_STATUS, 1);
        s->ibi_data.ibi_nacked = true;
        s->ibi_data.disec_addr = addr;
        /* Tell the requester to disable interrupts. */
        s->ibi_data.disec_byte = DISEC_INT;
        s->ibi_data.send_direct_disec = true;
        return -1;
    }
    return 0;
}

static int dwc_i3c_device_ibi_handle(I3CBus *bus, I3CTarget *target,
                                        uint8_t addr, bool is_recv)
{
    DwcI3CDevice *s = DWC_I3C(bus->qbus.parent);

    trace_dwc_i3c_device_ibi_handle(s->id, addr, is_recv);
    s->ibi_data.ibi_queue_status = FIELD_DP32(s->ibi_data.ibi_queue_status,
                                              IBI_QUEUE_STATUS, IBI_ID,
                                              (addr << 1) | is_recv);
    /* Is this a hot join request? */
    if (addr == I3C_HJ_ADDR) {
        return dwc_i3c_device_handle_hj(s);
    }
    /* Is secondary controller requesting access? */
    if (addr == target->address && !is_recv) {
        return dwc_i3c_device_handle_ctlr_req(s, addr);
    }
    /* Is this a target IRQ? */
    if (addr == target->address && is_recv) {
        return dwc_i3c_device_handle_targ_irq(s, addr);
    }

    /* Not sure what this is, NACK it. */
    return -1;
}

static int dwc_i3c_device_ibi_recv(I3CBus *bus, uint8_t data)
{
    DwcI3CDevice *s = DWC_I3C(bus->qbus.parent);
    if (fifo8_is_full(&s->ibi_data.ibi_intermediate_queue)) {
        return -1;
    }

    fifo8_push(&s->ibi_data.ibi_intermediate_queue, data);
    trace_dwc_i3c_device_ibi_recv(s->id, data);
    return 0;
}

static void dwc_i3c_device_ibi_queue_push(DwcI3CDevice *s)
{
    /* Stored value is in 32-bit chunks, convert it to byte chunks. */
    uint8_t ibi_slice_size = dwc_i3c_device_ibi_slice_size(s);
    uint8_t num_slices = fifo8_num_used(&s->ibi_data.ibi_intermediate_queue) /
                         ibi_slice_size;
    uint8_t ibi_status_count = num_slices;
    union {
        uint8_t b[sizeof(uint32_t)];
        uint32_t val32;
    } ibi_data = {
        .val32 = 0
    };

    /* The report was suppressed, do nothing. */
    if (s->ibi_data.ibi_nacked && !s->ibi_data.notify_ibi_nack) {
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_IDLE);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                         DWC_I3C_TRANSFER_STATUS_IDLE);
        return;
    }

    /* If we don't have any slices to push, just push the status. */
    if (num_slices == 0) {
        s->ibi_data.ibi_queue_status =
             FIELD_DP32(s->ibi_data.ibi_queue_status, IBI_QUEUE_STATUS,
                        LAST_STATUS, 1);
        fifo32_push(&s->ibi_queue, s->ibi_data.ibi_queue_status);
        ibi_status_count = 1;
    }

    for (uint8_t i = 0; i < num_slices; i++) {
        /* If this is the last slice, set LAST_STATUS. */
        if (fifo8_num_used(&s->ibi_data.ibi_intermediate_queue) <
            ibi_slice_size) {
            s->ibi_data.ibi_queue_status =
                FIELD_DP32(s->ibi_data.ibi_queue_status, IBI_QUEUE_STATUS,
                           IBI_DATA_LEN,
                           fifo8_num_used(&s->ibi_data.ibi_intermediate_queue));
            s->ibi_data.ibi_queue_status =
                FIELD_DP32(s->ibi_data.ibi_queue_status, IBI_QUEUE_STATUS,
                           LAST_STATUS, 1);
        } else {
            s->ibi_data.ibi_queue_status =
                FIELD_DP32(s->ibi_data.ibi_queue_status, IBI_QUEUE_STATUS,
                           IBI_DATA_LEN, ibi_slice_size);
        }

        /* Push the IBI status header. */
        fifo32_push(&s->ibi_queue, s->ibi_data.ibi_queue_status);
        /* Move each IBI byte into a 32-bit word and push it into the queue. */
        for (uint8_t j = 0; j < ibi_slice_size; ++j) {
            if (fifo8_is_empty(&s->ibi_data.ibi_intermediate_queue)) {
                break;
            }

            ibi_data.b[j & 3] = fifo8_pop(&s->ibi_data.ibi_intermediate_queue);
            /* We have 32-bits, push it to the IBI FIFO. */
            if ((j & 0x03) == 0x03) {
                fifo32_push(&s->ibi_queue, ibi_data.val32);
                ibi_data.val32 = 0;
            }
        }
        /* If the data isn't 32-bit aligned, push the leftover bytes. */
        if (ibi_slice_size & 0x03) {
            fifo32_push(&s->ibi_queue, ibi_data.val32);
        }

        /* Clear out the data length for the next iteration. */
        s->ibi_data.ibi_queue_status = FIELD_DP32(s->ibi_data.ibi_queue_status,
                                         IBI_QUEUE_STATUS, IBI_DATA_LEN, 0);
    }

    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, IBI_BUF_BLR,
                     fifo32_num_used(&s->ibi_queue));
    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, IBI_STATUS_CNT,
                     ibi_status_count);
    /* Threshold is the register value + 1. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                         IBI_STATUS_THLD) + 1;
    if (fifo32_num_used(&s->ibi_queue) >= threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, IBI_THLD, 1);
        dwc_i3c_device_update_irq(s);
    }

    /* State update. */
    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                     DWC_I3C_TRANSFER_STATE_IDLE);
    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_STATUS,
                     DWC_I3C_TRANSFER_STATUS_IDLE);
}

static int dwc_i3c_device_ibi_finish(I3CBus *bus)
{
    DwcI3CDevice *s = DWC_I3C(bus->qbus.parent);
    bool nack_and_disable_hj = ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL,
                                                HOT_JOIN_ACK_NACK_CTRL);

    if (s->ibi_data.disec_byte == DISEC_HJ &&
        (nack_and_disable_hj || s->ibi_data.send_direct_disec)) {
        dwc_i3c_device_send_disec(s);
    }
    dwc_i3c_device_ibi_queue_push(s);

    /* Clear out the intermediate values. */
    s->ibi_data.ibi_queue_status = 0;
    s->ibi_data.disec_addr = 0;
    s->ibi_data.disec_byte = 0;
    s->ibi_data.send_direct_disec = false;
    s->ibi_data.notify_ibi_nack = false;
    s->ibi_data.ibi_nacked = false;

    return 0;
}

static uint32_t dwc_i3c_device_intr_status_r(DwcI3CDevice *s)
{
    /* Only return the status whose corresponding EN bits are set. */
    return s->regs[R_INTR_STATUS] & s->regs[R_INTR_STATUS_EN];
}

static void dwc_i3c_device_intr_status_w(DwcI3CDevice *s, uint32_t val)
{
    /* INTR_STATUS[13:5] is w1c, other bits are RO. */
    val &= 0x3fe0;
    s->regs[R_INTR_STATUS] &= ~val;

    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_intr_status_en_w(DwcI3CDevice *s, uint32_t val)
{
    s->regs[R_INTR_STATUS_EN] = val;
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_intr_signal_en_w(DwcI3CDevice *s, uint32_t val)
{
    s->regs[R_INTR_SIGNAL_EN] = val;
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_intr_force_w(DwcI3CDevice *s, uint32_t val)
{
    /* INTR_FORCE is WO, just set the corresponding INTR_STATUS bits. */
    s->regs[R_INTR_STATUS] = val;
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_cmd_queue_reset(DwcI3CDevice *s)
{
    fifo32_reset(&s->cmd_queue);

    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,
                     fifo32_num_free(&s->cmd_queue));
    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                               CMD_BUF_EMPTY_THLD);
    if (fifo32_num_free(&s->cmd_queue) >= empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, CMD_QUEUE_RDY, 1);
        dwc_i3c_device_update_irq(s);
    };
}

static void dwc_i3c_device_resp_queue_reset(DwcI3CDevice *s)
{
    fifo32_reset(&s->resp_queue);

    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, RESP_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));
    /*
     * This interrupt will always be cleared because the threshold is a minimum
     * of 1 and the queue size is 0.
     */
    ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RESP_RDY, 0);
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_ibi_queue_reset(DwcI3CDevice *s)
{
    fifo32_reset(&s->ibi_queue);

    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, IBI_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));
    /*
     * This interrupt will always be cleared because the threshold is a minimum
     * of 1 and the queue size is 0.
     */
    ARRAY_FIELD_DP32(s->regs, INTR_STATUS, IBI_THLD, 0);
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_tx_queue_reset(DwcI3CDevice *s)
{
    fifo32_reset(&s->tx_queue);

    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC,
                     fifo32_num_free(&s->tx_queue));
    /* TX buf is empty, so this interrupt will always be set. */
    ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TX_THLD, 1);
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_rx_queue_reset(DwcI3CDevice *s)
{
    fifo32_reset(&s->rx_queue);

    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));
    /*
     * This interrupt will always be cleared because the threshold is a minimum
     * of 1 and the queue size is 0.
     */
    ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RX_THLD, 0);
    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_reset(DeviceState *dev)
{
    DwcI3CDevice *s = DWC_I3C(dev);
    trace_dwc_i3c_device_reset(s->id);

    memcpy(s->regs, ast2600_i3c_device_resets, sizeof(s->regs));
    ARRAY_FIELD_DP32(s->regs, HW_CAPABILITY,  DEVICE_ROLE_CONFIG,
                     s->cfg.device_role);
    ARRAY_FIELD_DP32(s->regs, DEV_CHAR_TABLE_POINTER, DEV_CHAR_TABLE_DEPTH,
                     (s->cfg.device_role < DR_SLAVE_ONLY ?
                      s->cfg.num_devices * 4 : s->cfg.num_devices));
    ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR_TABLE_POINTER, DEPTH,
                     s->cfg.num_devices);
    ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR_TABLE_POINTER, ADDR,
                     ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                                      PRESENT_DEV_CHAR_TABLE_INDEX) +
                     ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                                      DEV_CHAR_TABLE_DEPTH) * 4);
    ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR, STATIC_ADDR_VALID,
                     s->cfg.slv_static_addr_en);
    ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR, STATIC_ADDR,
                     s->cfg.slv_static_addr);
    dwc_i3c_device_cmd_queue_reset(s);
    dwc_i3c_device_resp_queue_reset(s);
    dwc_i3c_device_ibi_queue_reset(s);
    dwc_i3c_device_tx_queue_reset(s);
    dwc_i3c_device_rx_queue_reset(s);
}

static void dwc_i3c_device_reset_ctrl_w(DwcI3CDevice *s, uint32_t val)
{
    if (FIELD_EX32(val, RESET_CTRL, CORE_RESET)) {
        dwc_i3c_device_reset(DEVICE(s));
    }
    if (FIELD_EX32(val, RESET_CTRL, CMD_QUEUE_RESET)) {
        dwc_i3c_device_cmd_queue_reset(s);
    }
    if (FIELD_EX32(val, RESET_CTRL, RESP_QUEUE_RESET)) {
        dwc_i3c_device_resp_queue_reset(s);
    }
    if (FIELD_EX32(val, RESET_CTRL, TX_BUF_RESET)) {
        dwc_i3c_device_tx_queue_reset(s);
    }
    if (FIELD_EX32(val, RESET_CTRL, RX_BUF_RESET)) {
        dwc_i3c_device_rx_queue_reset(s);
    }
    if (FIELD_EX32(val, RESET_CTRL, IBI_QUEUE_RESET)) {
        dwc_i3c_device_ibi_queue_reset(s);
    }
}

static uint32_t dwc_i3c_device_pop_rx(DwcI3CDevice *s)
{
    if (fifo32_is_empty(&s->rx_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to read RX FIFO when empty\n",
                      object_get_canonical_path(OBJECT(s)));
        return 0;
    }

    uint32_t val = 0;
    int i = 0;

    if (fifo8_num_used(&s->rx_queue.fifo) <= 3) {
        while (!fifo8_is_empty(&s->rx_queue.fifo)) {
            val |= fifo8_pop(&s->rx_queue.fifo) << i * 8;
            i++;
        }
    } else {
        val = fifo32_pop(&s->rx_queue);
    }
    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,
                     fifo32_num_used(&s->rx_queue));

    /* Threshold is 2^RX_BUF_THLD. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                         RX_BUF_THLD);
    threshold = dwc_i3c_device_fifo_threshold_from_reg(threshold);
    if (fifo32_num_used(&s->rx_queue) < threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RX_THLD, 0);
        dwc_i3c_device_update_irq(s);
    }

    trace_dwc_i3c_device_pop_rx(s->id, val);
    return val;
}

static uint32_t dwc_i3c_device_ibi_queue_r(DwcI3CDevice *s)
{
    if (fifo32_is_empty(&s->ibi_queue)) {
        return 0;
    }

    uint32_t val = fifo32_pop(&s->ibi_queue);
    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, IBI_BUF_BLR,
                     fifo32_num_used(&s->ibi_queue));
    /* Threshold is the register value + 1. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                         IBI_STATUS_THLD) + 1;
    if (fifo32_num_used(&s->ibi_queue) < threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, IBI_THLD, 0);
        dwc_i3c_device_update_irq(s);
    }
    return val;
}

static uint32_t dwc_i3c_device_resp_queue_port_r(DwcI3CDevice *s)
{
    if (fifo32_is_empty(&s->resp_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to read response FIFO when "
                      "empty\n", object_get_canonical_path(OBJECT(s)));
        return 0;
    }

    uint32_t val = fifo32_pop(&s->resp_queue);
    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, RESP_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));

    /* Threshold is the register value + 1. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                         RESP_BUF_THLD) + 1;
    if (fifo32_num_used(&s->resp_queue) < threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RESP_RDY, 0);
        dwc_i3c_device_update_irq(s);
    }

    return val;
}

static uint64_t dwc_i3c_device_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    DwcI3CDevice *s = DWC_I3C(opaque);
    uint32_t addr = offset >> 2;
    uint64_t value;

    switch (addr) {
    /* RAZ */
    case R_COMMAND_QUEUE_PORT:
    case R_RESET_CTRL:
    case R_INTR_FORCE:
        value = 0;
        break;
    case R_IBI_QUEUE_DATA:
        value = dwc_i3c_device_ibi_queue_r(s);
        break;
    case R_INTR_STATUS:
        value = dwc_i3c_device_intr_status_r(s);
        break;
    case R_RX_TX_DATA_PORT:
        value = dwc_i3c_device_pop_rx(s);
        break;
    case R_RESPONSE_QUEUE_PORT:
        value = dwc_i3c_device_resp_queue_port_r(s);
        break;
    default:
        value = s->regs[addr];
        break;
    }

    trace_dwc_i3c_device_read(s->id, offset, value);

    return value;
}

static void dwc_i3c_device_resp_queue_push(DwcI3CDevice *s,
                                              uint8_t err, uint8_t tid,
                                              uint8_t ccc_type,
                                              uint16_t data_len,
                                              bool rx_rsp)
{
    uint32_t val = 0;
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, ERR_STATUS, err);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, RX_RSP, rx_rsp);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, TID, tid);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, CCCT, ccc_type);
    val = FIELD_DP32(val, RESPONSE_QUEUE_PORT, DL, data_len);
    if (!fifo32_is_full(&s->resp_queue)) {
        trace_dwc_i3c_device_resp_queue_push(s->id, val);
        fifo32_push(&s->resp_queue, val);
    }

    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, RESP_BUF_BLR,
                     fifo32_num_used(&s->resp_queue));
    /* Threshold is the register value + 1. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                         RESP_BUF_THLD) + 1;
    if (fifo32_num_used(&s->resp_queue) >= threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RESP_RDY, 1);
        dwc_i3c_device_update_irq(s);
    }
}

static void dwc_i3c_device_push_tx(DwcI3CDevice *s, uint32_t val)
{
    if (fifo32_is_full(&s->tx_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to push to TX FIFO when "
                      "full\n", object_get_canonical_path(OBJECT(s)));
        return;
    }

    trace_dwc_i3c_device_push_tx(s->id, val);
    fifo32_push(&s->tx_queue, val);
    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC,
                     fifo32_num_free(&s->tx_queue));

    /* Threshold is 2^TX_BUF_THLD. */
    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                               TX_BUF_THLD);
    empty_threshold =
        dwc_i3c_device_fifo_threshold_from_reg(empty_threshold);
    if (fifo32_num_free(&s->tx_queue) < empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TX_THLD, 0);
        dwc_i3c_device_update_irq(s);
    }
}

static uint32_t dwc_i3c_device_pop_tx(DwcI3CDevice *s)
{
    if (fifo32_is_empty(&s->tx_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to pop from TX FIFO when "
                      "empty\n", object_get_canonical_path(OBJECT(s)));
        return 0;
    }

    uint32_t val = fifo32_pop(&s->tx_queue);
    trace_dwc_i3c_device_pop_tx(s->id, val);
    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC,
                     fifo32_num_free(&s->tx_queue));

    /* Threshold is 2^TX_BUF_THLD. */
    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                               TX_BUF_THLD);
    empty_threshold =
        dwc_i3c_device_fifo_threshold_from_reg(empty_threshold);
    if (fifo32_num_free(&s->tx_queue) >= empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TX_THLD, 1);
        dwc_i3c_device_update_irq(s);
    }
    return val;
}

static void dwc_i3c_device_push_rx(DwcI3CDevice *s, uint32_t val)
{
    if (fifo32_is_full(&s->rx_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to push to RX FIFO when "
                      "full\n", object_get_canonical_path(OBJECT(s)));
        return;
    }
    trace_dwc_i3c_device_push_rx(s->id, val);
    fifo32_push(&s->rx_queue, val);

    ARRAY_FIELD_DP32(s->regs, DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,
                     fifo32_num_used(&s->rx_queue));
    /* Threshold is 2^RX_BUF_THLD. */
    uint8_t threshold = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                         RX_BUF_THLD);
    threshold = dwc_i3c_device_fifo_threshold_from_reg(threshold);
    if (fifo32_num_used(&s->rx_queue) >= threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RX_THLD, 1);
        dwc_i3c_device_update_irq(s);
    }
}

static void dwc_i3c_device_short_transfer(DwcI3CDevice *s,
                                             DwcI3CTransferCmd cmd,
                                             DwcI3CShortArg arg)
{
    uint8_t err = DWC_I3C_RESP_QUEUE_ERR_NONE;
    uint8_t addr = dwc_i3c_device_target_addr(s, cmd.dev_index);
    bool is_i2c = dwc_i3c_device_target_is_i2c(s, cmd.dev_index);
    uint8_t data[4]; /* Max we can send on a short transfer is 4 bytes. */
    uint8_t len = 0;
    uint32_t bytes_sent; /* Ignored on short transfers. */

    /* Can't do reads on a short transfer. */
    if (cmd.rnw) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Cannot do a read on a short "
                      "transfer\n", object_get_canonical_path(OBJECT(s)));
        return;
    }

    if (dwc_i3c_device_send_start(s, addr, /*is_recv=*/false, is_i2c)) {
        err = DWC_I3C_RESP_QUEUE_ERR_I2C_NACK;
        goto transfer_done;
    }

    /* Are we sending a command? */
    if (cmd.cp) {
        data[len] = cmd.cmd;
        len++;
        /*
         * byte0 is the defining byte for a command, and is only sent if a
         * command is present and if the command has a defining byte present.
         * (byte_strb & 0x01) is always treated as set by the controller, and is
         * ignored.
         */
        if (cmd.dbp) {
            data[len] += arg.byte0;
            len++;
        }
    }

    /* Send the bytes passed in the argument. */
    if (arg.byte_strb & 0x02) {
        data[len] = arg.byte1;
        len++;
    }
    if (arg.byte_strb & 0x04) {
        data[len] = arg.byte2;
        len++;
    }

    if (dwc_i3c_device_send(s, data, len, &bytes_sent, is_i2c)) {
        err = DWC_I3C_RESP_QUEUE_ERR_I2C_NACK;
    } else {
        /* Only go to an idle state on a successful transfer. */
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_IDLE);
    }

transfer_done:
    if (cmd.toc) {
        dwc_i3c_device_end_transfer(s, is_i2c);
    }
    if (cmd.roc) {
        /*
         * ccc_type is always 0 in controller mode, data_len is 0 in short
         * transfers.
         */
        dwc_i3c_device_resp_queue_push(s, err, cmd.tid, /*ccc_type=*/0,
                                          /*data_len=*/0, /*rx_rxp*/0);
    }
}

/* Returns number of bytes transmitted. */
static uint16_t dwc_i3c_device_tx(DwcI3CDevice *s, uint16_t num,
                                     bool is_i2c)
{
    uint16_t bytes_sent = 0;
    union {
        uint8_t b[sizeof(uint32_t)];
        uint32_t val;
    } val32;

    while (bytes_sent < num) {
        val32.val = dwc_i3c_device_pop_tx(s);
        for (uint8_t i = 0; i < sizeof(val32.val); i++) {
            if (dwc_i3c_device_send_byte(s, val32.b[i], is_i2c)) {
                return bytes_sent;
            }
            bytes_sent++;

            /* We're not sending the full 32-bits, break early. */
            if (bytes_sent >= num) {
                break;
            }
        }
    }

    return bytes_sent;
}

/* Returns number of bytes received. */
static uint16_t dwc_i3c_device_rx(DwcI3CDevice *s, uint16_t num,
                                     bool is_i2c)
{
    /*
     * Allocate a temporary buffer to read data from the target.
     * Zero it and word-align it as well in case we're reading unaligned data.
     */
    g_autofree uint8_t *data = g_new0(uint8_t, num + (num & 0x03));
    uint32_t *data32 = (uint32_t *)data;
    /*
     * 32-bits since the I3C API wants a 32-bit number, even though the
     * controller can only do 16-bit transfers.
     */
    uint32_t num_read = 0;

    /* Can NACK if the target receives an unsupported CCC. */
    if (dwc_i3c_device_recv_data(s, is_i2c, data, num, &num_read)) {
        return 0;
    }

    for (uint16_t i = 0; i < num_read / 4; i++) {
        dwc_i3c_device_push_rx(s, *data32);
        data32++;
    }
    /*
     * If we're pushing data that isn't 32-bit aligned, push what's left.
     * It's software's responsibility to know what bits are valid in the partial
     * data.
     */
    if (num_read & 0x03) {
        dwc_i3c_device_push_rx(s, *data32);
    }

    return num_read;
}

static int dwc_i3c_device_transfer_ccc(DwcI3CDevice *s,
                                           DwcI3CTransferCmd cmd,
                                           DwcI3CTransferArg arg)
{
    /* CCC start is always a write. CCCs cannot be done on I2C devices. */
    if (dwc_i3c_device_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                     /*is_i2c=*/false)) {
        return DWC_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
    }
    trace_dwc_i3c_device_transfer_ccc(s->id, cmd.cmd);
    if (dwc_i3c_device_send_byte(s, cmd.cmd, /*is_i2c=*/false)) {
        return DWC_I3C_RESP_QUEUE_ERR_I2C_NACK;
    }

    /* On a direct CCC, we do a restart and then send the target's address. */
    if (CCC_IS_DIRECT(cmd.cmd)) {
        bool is_recv = cmd.rnw;
        uint8_t addr = dwc_i3c_device_target_addr(s, cmd.dev_index);
        if (dwc_i3c_device_send_start(s, addr, is_recv, /*is_i2c=*/false)) {
            return DWC_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
        }
    }

    return DWC_I3C_RESP_QUEUE_ERR_NONE;
}

static void dwc_i3c_device_transfer(DwcI3CDevice *s,
                                       DwcI3CTransferCmd cmd,
                                       DwcI3CTransferArg arg)
{
    bool is_recv = cmd.rnw;
    uint8_t err = DWC_I3C_RESP_QUEUE_ERR_NONE;
    uint8_t addr = dwc_i3c_device_target_addr(s, cmd.dev_index);
    bool is_i2c = dwc_i3c_device_target_is_i2c(s, cmd.dev_index);
    uint16_t bytes_transferred = 0;

    if (cmd.cp) {
        /* We're sending a CCC. */
        err = dwc_i3c_device_transfer_ccc(s, cmd, arg);
        if (err != DWC_I3C_RESP_QUEUE_ERR_NONE) {
            goto transfer_done;
        }
    } else {
        if (ARRAY_FIELD_EX32(s->regs, DEVICE_CTRL, I3C_BROADCAST_ADDR_INC) &&
            is_i2c == false) {
            if (dwc_i3c_device_send_start(s, I3C_BROADCAST,
                                             /*is_recv=*/false, is_i2c)) {
                err = DWC_I3C_RESP_QUEUE_ERR_I2C_NACK;
                goto transfer_done;
            }
        }
        /* Otherwise we're doing a private transfer. */
        if (dwc_i3c_device_send_start(s, addr, is_recv, is_i2c)) {
            err = DWC_I3C_RESP_QUEUE_ERR_I2C_NACK;
            goto transfer_done;
        }
    }

    if (is_recv) {
        bytes_transferred = dwc_i3c_device_rx(s, arg.data_len, is_i2c);
    } else {
        bytes_transferred = dwc_i3c_device_tx(s, arg.data_len, is_i2c);
    }

    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                     DWC_I3C_TRANSFER_STATE_IDLE);

transfer_done:
    if (cmd.toc) {
        dwc_i3c_device_end_transfer(s, is_i2c);
    }
    if (cmd.roc) {
        /*
         * data_len is the number of bytes that still need to be TX'd, or the
         * number of bytes RX'd.
         */
        uint16_t data_len = is_recv ? bytes_transferred : arg.data_len -
                                                          bytes_transferred;
        /* CCCT is always 0 in controller mode. */
        dwc_i3c_device_resp_queue_push(s, err, cmd.tid, /*ccc_type=*/0,
                                          data_len, 0);
    }

    dwc_i3c_device_update_irq(s);
}

static void dwc_i3c_device_transfer_cmd(DwcI3CDevice *s,
                                           DwcI3CTransferCmd cmd,
                                           DwcI3CCmdQueueData arg)
{
    uint8_t arg_attr = FIELD_EX32(arg.word, COMMAND_QUEUE_PORT, CMD_ATTR);

    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CMD_TID, cmd.tid);

    /* User is trying to do HDR transfers, see if we can do them. */
    if (cmd.speed == 0x06 && !dwc_i3c_device_has_hdr_ddr(s)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: HDR DDR is not supported\n",
                      object_get_canonical_path(OBJECT(s)));
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_HALT);
        return;
    }
    if (cmd.speed == 0x05 && !dwc_i3c_device_has_hdr_ts(s)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: HDR TS is not supported\n",
                      object_get_canonical_path(OBJECT(s)));
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_HALT);
        return;
    }

    if (arg_attr == DWC_I3C_CMD_ATTR_TRANSFER_ARG) {
        dwc_i3c_device_transfer(s, cmd, arg.transfer_arg);
    } else if (arg_attr == DWC_I3C_CMD_ATTR_SHORT_DATA_ARG) {
        dwc_i3c_device_short_transfer(s, cmd, arg.short_arg);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unknown command queue cmd_attr 0x%x"
                      "\n", object_get_canonical_path(OBJECT(s)), arg_attr);
        ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                         DWC_I3C_TRANSFER_STATE_HALT);
    }
}

static void dwc_i3c_device_update_char_table(DwcI3CDevice *s,
                                                uint8_t offset, uint64_t pid,
                                                uint8_t bcr, uint8_t dcr,
                                                uint8_t addr)
{
    if (offset > s->cfg.num_devices) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Device char table offset %d out of "
                      "bounds\n", object_get_canonical_path(OBJECT(s)), offset);
        /* If we're out of bounds, do nothing. */
        return;
    }

    /* Each char table index is 128 bits apart. */
    uint16_t dev_index = R_DEV_CHAR_TBL(s) + offset * sizeof(uint32_t);
    s->regs[dev_index] = pid & 0xffffffff;
    pid >>= 32;
    s->regs[dev_index + 1] = FIELD_DP32(s->regs[dev_index + 1],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC2,
                                        MSB_PID, pid);
    s->regs[dev_index + 2] = FIELD_DP32(s->regs[dev_index + 2],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC3, DCR,
                                        dcr);
    s->regs[dev_index + 2] = FIELD_DP32(s->regs[dev_index + 2],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC3, BCR,
                                        bcr);
    s->regs[dev_index + 3] = FIELD_DP32(s->regs[dev_index + 3],
                                        DEVICE_CHARACTERISTIC_TABLE_LOC4,
                                        DEV_DYNAMIC_ADDR, addr);

    /* Increment PRESENT_DEV_CHAR_TABLE_INDEX. */
    uint8_t idx = ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                     PRESENT_DEV_CHAR_TABLE_INDEX);
    /* Increment and rollover. */
    idx++;
    if (idx >= ARRAY_FIELD_EX32(s->regs, DEV_CHAR_TABLE_POINTER,
                               DEV_CHAR_TABLE_DEPTH) / 4) {
        idx = 0;
    }
    ARRAY_FIELD_DP32(s->regs, DEV_CHAR_TABLE_POINTER,
                     PRESENT_DEV_CHAR_TABLE_INDEX, idx);
}

static void dwc_i3c_device_addr_assign_cmd(DwcI3CDevice *s,
                                              DwcI3CAddrAssignCmd cmd)
{
    uint8_t i = 0;
    uint8_t err = DWC_I3C_RESP_QUEUE_ERR_NONE;

    if (!dwc_i3c_device_has_entdaa(s)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: ENTDAA is not supported\n",
                      object_get_canonical_path(OBJECT(s)));
        return;
    }

    /* Tell everyone to ENTDAA. If these error, no one is on the bus. */
    if (dwc_i3c_device_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                     /*is_i2c=*/false)) {
        err = DWC_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
        goto transfer_done;
    }
    if (dwc_i3c_device_send_byte(s, cmd.cmd, /*is_i2c=*/false)) {
        err = DWC_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
        goto transfer_done;
    }

    /* Go through each device in the table and assign it an address. */
    for (i = 0; i < cmd.dev_count; i++) {
        uint8_t addr = dwc_i3c_device_target_addr(s, cmd.dev_index + i);
        union {
            uint64_t pid:48;
            uint8_t bcr;
            uint8_t dcr;
            uint32_t w[2];
            uint8_t b[8];
        } target_info;

        /* If this fails, there was no one left to ENTDAA. */
        if (dwc_i3c_device_send_start(s, I3C_BROADCAST, /*is_recv=*/false,
                                         /*is_i2c=*/false)) {
            err = DWC_I3C_RESP_QUEUE_ERR_BROADCAST_NACK;
            break;
        }

        /*
         * In ENTDAA, we read 8 bytes from the target, which will be the
         * target's PID, BCR, and DCR. After that, we send it the dynamic
         * address.
         * Don't bother checking the number of bytes received, it must send 8
         * bytes during ENTDAA.
         */
        uint32_t num_read;
        if (dwc_i3c_device_recv_data(s, /*is_i2c=*/false, target_info.b,
                                        I3C_ENTDAA_SIZE, &num_read)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Target NACKed ENTDAA CCC\n",
                          object_get_canonical_path(OBJECT(s)));
            err = DWC_I3C_RESP_QUEUE_ERR_DAA_NACK;
            goto transfer_done;
        }
        if (dwc_i3c_device_send_byte(s, addr, /*is_i2c=*/false)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Target NACKed addr 0x%.2x "
                          "during ENTDAA\n",
                          object_get_canonical_path(OBJECT(s)), addr);
            err = DWC_I3C_RESP_QUEUE_ERR_DAA_NACK;
            break;
        }
        dwc_i3c_device_update_char_table(s, cmd.dev_index + i,
                                            target_info.pid, target_info.bcr,
                                            target_info.dcr, addr);

        /* Push the PID, BCR, and DCR to the RX queue. */
        dwc_i3c_device_push_rx(s, target_info.w[0]);
        dwc_i3c_device_push_rx(s, target_info.w[1]);
    }

transfer_done:
    /* Do we send a STOP? */
    if (cmd.toc) {
        dwc_i3c_device_end_transfer(s, /*is_i2c=*/false);
    }
    /*
     * For addr assign commands, the length field is the number of devices
     * left to assign. CCCT is always 0 in controller mode.
     */
    if (cmd.roc) {
        dwc_i3c_device_resp_queue_push(s, err, cmd.tid, /*ccc_type=*/0,
                                         cmd.dev_count - i, 0);
    }
}

static uint32_t dwc_i3c_device_cmd_queue_pop(DwcI3CDevice *s)
{
    if (fifo32_is_empty(&s->cmd_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Tried to dequeue command queue "
                      "when it was empty\n",
                      object_get_canonical_path(OBJECT(s)));
        return 0;
    }
    uint32_t val = fifo32_pop(&s->cmd_queue);

    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                               CMD_BUF_EMPTY_THLD);
    uint8_t cmd_queue_empty_loc = ARRAY_FIELD_EX32(s->regs,
                                                   QUEUE_STATUS_LEVEL,
                                                   CMD_QUEUE_EMPTY_LOC);
    cmd_queue_empty_loc++;
    ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,
                     cmd_queue_empty_loc);
    if (cmd_queue_empty_loc >= empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, CMD_QUEUE_RDY, 1);
        dwc_i3c_device_update_irq(s);
    }

    return val;
}

static int dwc_i3c_device_cmd_queue_invalid(DwcI3CDevice *s)
{
   /*
     * We only start executing when a command is passed into the FIFO.
     * We expect there to be a multiple of 2 items in the queue. The first item
     * should be an argument to a command, and the command should be the second
     * item.
     */
    if (fifo32_num_used(&s->cmd_queue) & 1) {
        return 1;
    }
    return 0;
}

static void dwc_i3c_device_cmd_queue_execute(DwcI3CDevice *s)
{
    ARRAY_FIELD_DP32(s->regs, PRESENT_STATE, CM_TFR_ST_STATUS,
                     DWC_I3C_TRANSFER_STATE_IDLE);
    if (!dwc_i3c_device_can_transmit(s)) {
        return;
    }

    if (dwc_i3c_device_cmd_queue_invalid(s)) {
        return;
    }
     while (!fifo32_is_empty(&s->cmd_queue)) {
        DwcI3CCmdQueueData arg;
        arg.word = dwc_i3c_device_cmd_queue_pop(s);
        DwcI3CCmdQueueData cmd;
        cmd.word = dwc_i3c_device_cmd_queue_pop(s);
        trace_dwc_i3c_device_cmd_queue_execute(s->id, cmd.word, arg.word);

        uint8_t cmd_attr = FIELD_EX32(cmd.word, COMMAND_QUEUE_PORT, CMD_ATTR);
        switch (cmd_attr) {
        case DWC_I3C_CMD_ATTR_TRANSFER_CMD:
            dwc_i3c_device_transfer_cmd(s, cmd.transfer_cmd, arg);
            break;
        case DWC_I3C_CMD_ATTR_ADDR_ASSIGN_CMD:
            /* Arg is discarded for addr assign commands. */
            dwc_i3c_device_addr_assign_cmd(s, cmd.addr_assign_cmd);
            break;
        case DWC_I3C_CMD_ATTR_TRANSFER_ARG:
        case DWC_I3C_CMD_ATTR_SHORT_DATA_ARG:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Command queue received argument"
                          " packet when it expected a command packet\n",
                          object_get_canonical_path(OBJECT(s)));
            break;
        default:
            /*
             * The caller's check before queueing an item should prevent this
             * from happening.
             */
            g_assert_not_reached();
            break;
        }
    }
}

static void dwc_i3c_device_cmd_queue_push(DwcI3CDevice *s, uint32_t val)
{
    if (fifo32_is_full(&s->cmd_queue)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Command queue received packet when "
                      "already full\n", object_get_canonical_path(OBJECT(s)));
        return;
    }
    trace_dwc_i3c_device_cmd_queue_push(s->id, val);
    fifo32_push(&s->cmd_queue, val);

    uint8_t empty_threshold = ARRAY_FIELD_EX32(s->regs, QUEUE_THLD_CTRL,
                                               CMD_BUF_EMPTY_THLD);
    uint8_t cmd_queue_empty_loc = ARRAY_FIELD_EX32(s->regs,
                                                   QUEUE_STATUS_LEVEL,
                                                   CMD_QUEUE_EMPTY_LOC);
    if (cmd_queue_empty_loc) {
        cmd_queue_empty_loc--;
        ARRAY_FIELD_DP32(s->regs, QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,
                         cmd_queue_empty_loc);
    }
    if (cmd_queue_empty_loc < empty_threshold) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, CMD_QUEUE_RDY, 0);
        dwc_i3c_device_update_irq(s);
    }
}

static void dwc_i3c_device_cmd_queue_port_w(DwcI3CDevice *s, uint32_t val)
{
    uint8_t cmd_attr = FIELD_EX32(val, COMMAND_QUEUE_PORT, CMD_ATTR);

    switch (cmd_attr) {
    /* If a command is received we can start executing it. */
    case DWC_I3C_CMD_ATTR_TRANSFER_CMD:
    case DWC_I3C_CMD_ATTR_ADDR_ASSIGN_CMD:
        dwc_i3c_device_cmd_queue_push(s, val);
        if (dwc_i3c_device_role_master(s)) {
            dwc_i3c_device_cmd_queue_execute(s);
        }
        break;
    /* If we get an argument just push it. */
    case DWC_I3C_CMD_ATTR_TRANSFER_ARG:
    case DWC_I3C_CMD_ATTR_SHORT_DATA_ARG:
        dwc_i3c_device_cmd_queue_push(s, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Command queue received packet with "
                      "unknown cmd attr 0x%x\n",
                      object_get_canonical_path(OBJECT(s)), cmd_attr);
        break;
    }
}

static void dwc_i3c_device_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    DwcI3CDevice *s = DWC_I3C(opaque);
    uint32_t addr = offset >> 2;
    uint32_t val32 = (uint32_t)value;

    trace_dwc_i3c_device_write(s->id, offset, value);

    val32 &= ~ast2600_i3c_device_ro[addr];
    switch (addr) {
    case R_HW_CAPABILITY:
    case R_RESPONSE_QUEUE_PORT:
    case R_IBI_QUEUE_DATA:
    case R_QUEUE_STATUS_LEVEL:
    case R_PRESENT_STATE:
    case R_CCC_DEVICE_STATUS:
    case R_DEVICE_ADDR_TABLE_POINTER:
    case R_VENDOR_SPECIFIC_REG_POINTER:
    case R_SLV_MAX_LEN:
    case R_MAX_READ_TURNAROUND:
    case R_I3C_VER_ID:
    case R_I3C_VER_TYPE:
    case R_EXTENDED_CAPABILITY:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to readonly register[0x%02" HWADDR_PRIx
                      "] = 0x%08" PRIx64 "\n",
                      __func__, offset, value);
        break;
    case R_DEVICE_CTRL:
        dwc_i3c_device_ctrl_w(s, val32);
        break;
    case R_DEVICE_ADDR:
        dwc_i3c_device_addr_w(s, val32);
        break;
    case R_RX_TX_DATA_PORT:
        dwc_i3c_device_push_tx(s, val32);
        break;
    case R_COMMAND_QUEUE_PORT:
        dwc_i3c_device_cmd_queue_port_w(s, val32);
        break;
    case R_RESET_CTRL:
        dwc_i3c_device_reset_ctrl_w(s, val32);
        break;
    case R_INTR_STATUS:
        dwc_i3c_device_intr_status_w(s, val32);
        break;
    case R_INTR_STATUS_EN:
        dwc_i3c_device_intr_status_en_w(s, val32);
        break;
    case R_INTR_SIGNAL_EN:
        dwc_i3c_device_intr_signal_en_w(s, val32);
        break;
    case R_INTR_FORCE:
        dwc_i3c_device_intr_force_w(s, val32);
        break;
    default:
        s->regs[addr] = val32;
        break;
    }
}

static int dwc_i3c_target_event(I3CTarget *i3c, enum I3CEvent event)
{
    DwcI3CTarget *ss = DWC_I3C_TARGET(i3c);
    DwcI3CDevice *s = ss->dwc_i3c;
    uint8_t thld;

    switch (event) {
    case I3C_START_SEND:
        thld = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL, RX_START_THLD);
        thld = 1 << (thld + 1);
        thld = thld == 2 ? 1 : thld;
        if (fifo32_num_free(&s->rx_queue) * 4 < thld) {
            /*
             *  Receive buf space if not sufficent w.r.t RX_START_THLD
             */
            return -1;
        }
        s->target.curr_event = event;
        break;
    case I3C_START_RECV:
        if (dwc_i3c_device_cmd_queue_invalid(s)) {
            return -1;
        }
        s->target.tx_arg.word = dwc_i3c_device_cmd_queue_pop(s);
        s->target.tx_cmd.word = dwc_i3c_device_cmd_queue_pop(s);
        s->target.curr_event = event;
        break;
    case I3C_STOP:
        if (s->target.curr_event == I3C_START_SEND) {
            dwc_i3c_device_resp_queue_push(s, 0, 0, 0,
                             s->target.tr_bytes, true);
            s->target.tr_bytes = 0;
        } else if (s->target.curr_event == I3C_START_RECV) {
            dwc_i3c_device_resp_queue_push(s, 0,
                s->target.tx_cmd.transfer_cmd.tid, 0,
                dwc_i3c_device_cmd_num_tx_bytes(s->target.tx_arg) -
                s->target.tr_bytes, false);
            s->target.tr_bytes = 0;
            s->target.tx_cmd.word = 0;
            s->target.tx_arg.word = 0;
        }
        s->target.curr_event = event;
        break;
    case I3C_NACK:
    case I3C_CCC_WR:
    case I3C_CCC_RD:
        break;
    };
    return 0;
}

static uint32_t device_i3c_target_rx(I3CTarget *i3c, uint8_t *data,
                                     uint32_t num_to_read)
{
    DwcI3CTarget *ss = DWC_I3C_TARGET(i3c);
    DwcI3CDevice *s = ss->dwc_i3c;
    uint16_t cmd_data_len = 0;
    uint32_t send;
    int i, j;
    bool sdap = s->target.tx_cmd.transfer_cmd.sdap;
    uint8_t thld = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                    TX_START_THLD);
    thld = 1 << (thld + 1);
    thld = thld == 2 ? 1 : thld;

    cmd_data_len = dwc_i3c_device_cmd_num_tx_bytes(s->target.tx_arg);

    if (cmd_data_len) {
        return -1;
    }

    send = MIN(num_to_read, cmd_data_len - s->target.tr_bytes);
    if (sdap) {
        for (j = 0, i = s->target.tr_bytes; i < send; i++, j++) {
            data[j] = i == 0 ? s->target.tx_arg.short_arg.byte0 :
                      i == 1 ? s->target.tx_arg.short_arg.byte1 :
                      i == 2 ? s->target.tx_arg.short_arg.byte2 : 0;
        }
    } else {
        for (i = 0; i < send; i++) {
            if (fifo8_is_empty(&s->tx_queue.fifo)) {
                data[i] = fifo8_pop(&s->tx_queue.fifo);
             }
        }
    }

    if (fifo8_num_free(&s->tx_queue.fifo) >= thld) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, TX_THLD, 1);
    }

    s->target.tr_bytes += send;
    dwc_i3c_device_update_irq(s);
    return send;
}

static int device_i3c_target_tx(I3CTarget *i3c, const uint8_t *data,
                                uint32_t num_to_send, uint32_t *num_sent)
{
    DwcI3CTarget *ss = DWC_I3C_TARGET(i3c);
    DwcI3CDevice *s = ss->dwc_i3c;
    unsigned int recv = num_to_send;
    uint8_t thld = ARRAY_FIELD_EX32(s->regs, DATA_BUFFER_THLD_CTRL,
                                    RX_START_THLD);
    thld = 1 << (thld + 1);
    thld = thld == 2 ? 1 : thld;

    if (fifo8_num_free(&s->rx_queue.fifo) < num_to_send) {
        recv = fifo8_num_free(&s->rx_queue.fifo);
    }

    fifo8_push_all(&s->rx_queue.fifo, data, recv);
    s->target.tr_bytes += recv;

    if (fifo8_num_used(&s->rx_queue.fifo) >= thld) {
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, RX_THLD, 1);
    }

    *num_sent = recv;
    dwc_i3c_device_update_irq(s);
    return 0;
}

static int device_i3c_target_ccc_read(I3CTarget *i3c, uint8_t *data,
                                      uint32_t num_to_read, uint32_t *num_read)
{
    DwcI3CTarget *ss = DWC_I3C_TARGET(i3c);
    DwcI3CDevice *s = ss->dwc_i3c;

    switch (i3c->curr_ccc) {
    case I3C_CCCD_GETSTATUS:
        if (i3c->ccc_byte_offset == 1 && num_to_read == 2) {
            data[0] = (s->regs[R_CCC_DEVICE_STATUS] & 0xFF00) >> 8;
            data[1] = s->regs[R_CCC_DEVICE_STATUS] & 0xFF;
            *num_read = 2;
        }
        break;
    case I3C_CCCD_GETMXDS:
        data[0] = ARRAY_FIELD_EX32(s->regs, MAX_DATA_SPEED, MXDS_MAX_WR_SPEED);
        data[1] = ARRAY_FIELD_EX32(s->regs, MAX_DATA_SPEED, MXDS_MAX_RD_SPEED);
        *num_read = 2;
        if (num_to_read == 3) {
            data[2] = ARRAY_FIELD_EX32(s->regs, MAX_DATA_SPEED,
                                       MXDS_CLK_DATA_TURN);
            *num_read = 3;
        }
        break;
    case I3C_CCCD_GETMRL:
        data[0] = ARRAY_FIELD_EX32(s->regs, SLV_MAX_LEN, MRL) >> 8;
        data[1] = ARRAY_FIELD_EX32(s->regs, SLV_MAX_LEN, MRL) & 0xFF;
        *num_read = 2;
        break;
    case I3C_CCCD_GETMWL:
        data[0] = ARRAY_FIELD_EX32(s->regs, SLV_MAX_LEN, MWL) >> 8;
        data[1] = ARRAY_FIELD_EX32(s->regs, SLV_MAX_LEN, MWL) & 0xFF;
        *num_read = 2;
        break;
    case I3C_CCCD_GETPID:
    case I3C_CCCD_GETBCR:
        break;
    case I3C_CCCD_GETCAPS:
        data[0] = ARRAY_FIELD_EX32(s->regs, SLV_CHAR_CTRL, HDR_CAP);
        *num_read = 1;
        break;
    case I3C_CCC_DEFTGTS:
    case I3C_CCCD_GETACCCR:
    case I3C_CCC_ENEC:
        break;
    default:
        break;
    };
    dwc_i3c_device_update_irq(s);
    return 0;
}

#define CCC_BC_CHECK(bytes_rec, ccc_offset)                         \
    if (bytes_rec <= ccc_offset) {                                  \
        qemu_log_mask(LOG_GUEST_ERROR, "Broadcast CCC should have"  \
                      " followup data");                            \
        return -1;                                                  \
    }

static int device_i3c_target_ccc_write(I3CTarget *i3c, const uint8_t *data,
                                       uint32_t num_to_send,
                                       uint32_t *num_sent)
{
    DwcI3CTarget *ss = DWC_I3C_TARGET(i3c);
    DwcI3CDevice *s = ss->dwc_i3c;

    switch (i3c->curr_ccc) {
    case I3C_CCC_ENEC:
        i3c->ccc_byte_offset++;
        *num_sent = 1;
        CCC_BC_CHECK(num_to_send, i3c->ccc_byte_offset)
        /*
         * fall through
         */
    case I3C_CCCD_ENEC:
        if (i3c->ccc_byte_offset == 1) {
            s->regs[R_SLV_EVENT_CTRL] |= data[*num_sent] & 0xF;
            *num_sent += 1;
            i3c->ccc_byte_offset++;
        }
        break;
    case I3C_CCC_DISEC:
        i3c->ccc_byte_offset++;
        *num_sent = 1;
        CCC_BC_CHECK(num_to_send, i3c->ccc_byte_offset)
       /*
        * fall through
        */
    case I3C_CCCD_DISEC:
        s->regs[R_SLV_EVENT_CTRL] &= ~((uint32_t)(data[*num_sent] & 0xF));
        *num_sent += 1;
        i3c->ccc_byte_offset++;
        break;
    case I3C_CCCD_SETDASA:
        if (!s->cfg.slv_static_addr_en) {
            return -1;
        }
       /*
        * Fall through
        */
    case I3C_CCCD_SETNEWDA:
    case I3C_CCC_ENTDAA:
        ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR, DYNAMIC_ADDR_VALID, 1);
        ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR, DYNAMIC_ADDR, i3c->address);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, DYN_ADDR_ASSGN, 1);
        break;
    case I3C_CCC_RSTDAA:
        ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR, DYNAMIC_ADDR_VALID, 0);
        ARRAY_FIELD_DP32(s->regs, DEVICE_ADDR, DYNAMIC_ADDR, 0);
        ARRAY_FIELD_DP32(s->regs, INTR_STATUS, DYN_ADDR_ASSGN, 0);
        break;
    case I3C_CCC_ENTHDR0:
        *num_sent = 1;
        i3c->ccc_byte_offset++;
        if (!dwc_i3c_device_has_hdr_ddr(s)) {
            return -1;
        }
        break;
    case I3C_CCC_ENTHDR1:
    case I3C_CCC_ENTHDR2:
        *num_sent = 1;
        i3c->ccc_byte_offset++;
        if (!dwc_i3c_device_has_hdr_ts(s)) {
            return -1;
        }
        break;
    case I3C_CCC_SETMRL:
        i3c->ccc_byte_offset++;
        *num_sent = 1;
        CCC_BC_CHECK(num_to_send, i3c->ccc_byte_offset)
        /*
         * Fall through
         */
    case I3C_CCCD_SETMRL:
        /*
         * 0: mrl msb
         * 1: mrl lsb
         * 2: ibi size (optional)
         */
        ARRAY_FIELD_DP32(s->regs, SLV_MAX_LEN, MRL,
                          (data[*num_sent] << 8 | data[*num_sent + 1]));
        *num_sent += 2;
        break;
    case I3C_CCC_SETMWL:
        i3c->ccc_byte_offset++;
        *num_sent = 1;
        CCC_BC_CHECK(num_to_send, i3c->ccc_byte_offset)
        /*
         * Fall through
         */
    case I3C_CCCD_SETMWL:
        ARRAY_FIELD_DP32(s->regs, SLV_MAX_LEN, MWL,
                         (data[*num_sent] << 8 | data[*num_sent + 1]));
        *num_sent += 2;
        break;
    case I3C_CCC_ENTAS0:
    case I3C_CCCD_ENTAS0:
    case I3C_CCC_ENTAS1:
    case I3C_CCCD_ENTAS1:
    case I3C_CCC_ENTAS2:
    case I3C_CCCD_ENTAS2:
    case I3C_CCC_ENTAS3:
    case I3C_CCCD_ENTAS3:
        *num_sent = 1;
        i3c->ccc_byte_offset++;
        break;
    default:
        break;
    };
    dwc_i3c_device_update_irq(s);
    return 0;
}

static const VMStateDescription dwc_i3c_device_vmstate = {
    .name = TYPE_DWC_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32_ARRAY(regs, DwcI3CDevice, DWC_I3C_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const MemoryRegionOps dwc_i3c_device_ops = {
    .read = dwc_i3c_device_read,
    .write = dwc_i3c_device_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void dwc_i3c_device_realize(DeviceState *dev, Error **errp)
{
    DwcI3CDevice *s = DWC_I3C(dev);
    g_autofree char *name = g_strdup_printf(TYPE_DWC_I3C ".%d",
                                            s->id);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    memory_region_init_io(&s->mr, OBJECT(s), &dwc_i3c_device_ops,
                          s, name, DWC_I3C_NR_REGS << 2);

    fifo32_create(&s->cmd_queue, DWC_I3C_CMD_QUEUE_CAPACITY);
    fifo32_create(&s->resp_queue, DWC_I3C_RESP_QUEUE_CAPACITY);
    fifo32_create(&s->tx_queue, DWC_I3C_TX_QUEUE_CAPACITY);
    fifo32_create(&s->rx_queue, DWC_I3C_RX_QUEUE_CAPACITY);
    fifo32_create(&s->ibi_queue, DWC_I3C_IBI_QUEUE_CAPACITY);
    /* Arbitrarily large enough to not be an issue. */
    fifo8_create(&s->ibi_data.ibi_intermediate_queue,
                  DWC_I3C_IBI_QUEUE_CAPACITY * 8);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);

    if (s->cfg.device_role <= DR_SECONDARY_MASTER) {
        /*
         * AMD: Fix bus name for "i3c", which makes reg parser
         * simple.
         */
        s->bus = i3c_init_bus(DEVICE(s), "i3c");
        I3CBusClass *bc = I3C_BUS_GET_CLASS(s->bus);
        bc->ibi_handle = dwc_i3c_device_ibi_handle;
        bc->ibi_recv = dwc_i3c_device_ibi_recv;
        bc->ibi_finish = dwc_i3c_device_ibi_finish;
    }

    if (s->cfg.device_role == DR_SLAVE_ONLY) {
        g_assert(s->i3c_target);
    }
}

static Property dwc_i3c_device_properties[] = {
    DEFINE_PROP_UINT8("device-id", DwcI3CDevice, id, 0),
    /*
     * Role Configuration
     */
    DEFINE_PROP_UINT8("device-role", DwcI3CDevice, cfg.device_role,
                      DR_SECONDARY_MASTER),
    /*
     * Queues and Interfaces Parameters
     */
    DEFINE_PROP_UINT8("buf-lvl-sel", DwcI3CDevice, cfg.buf_lvl_sel, 0x3),
    /*
     * Master Configuration Parameters
     */
    DEFINE_PROP_UINT8("num-devices", DwcI3CDevice, cfg.num_devices, 8),
    DEFINE_PROP_UINT8("ibi-buf-lvl-sel", DwcI3CDevice,
                      cfg.ibi_buf_lvl_sel, 0x3),
    /*
     * Slave Configuration Parameters
     */
    DEFINE_PROP_BOOL("slv-ibi", DwcI3CDevice, cfg.slv_ibi, false),
    DEFINE_PROP_UINT16("slv-dflt-mwl", DwcI3CDevice, cfg.slv_mwl, 0xFF),
    DEFINE_PROP_UINT16("slv-dflt-mrl", DwcI3CDevice, cfg.slv_mrl, 0xFF),
    DEFINE_PROP_BOOL("slave-static-addr-en", DwcI3CDevice,
                     cfg.slv_static_addr_en, false),
    DEFINE_PROP_UINT8("slave-static-addr", DwcI3CDevice,
                      cfg.slv_static_addr, 0x0),
    DEFINE_PROP_LINK("i3c-target", DwcI3CDevice, i3c_target,
                     TYPE_I3C_TARGET, I3CTarget *),
    DEFINE_PROP_END_OF_LIST(),
};

static Property dwc_i3c_target_properties[] = {
    DEFINE_PROP_LINK("dwc-i3c-device", DwcI3CTarget, dwc_i3c,
                     TYPE_DWC_I3C, DwcI3CDevice *),
    DEFINE_PROP_END_OF_LIST(),
};

static void dwc_i3c_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "DWC I3C Device";
    dc->realize = dwc_i3c_device_realize;
    dc->reset = dwc_i3c_device_reset;
    device_class_set_props(dc, dwc_i3c_device_properties);
    dc->vmsd = &dwc_i3c_device_vmstate;
}

static void dwc_i3c_device_target_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I3CTargetClass *k = I3C_TARGET_CLASS(klass);

    dc->desc = "DWC I3C Target";
    k->event = dwc_i3c_target_event;
    k->recv = device_i3c_target_rx;
    k->send = device_i3c_target_tx;
    k->handle_ccc_read = device_i3c_target_ccc_read;
    k->handle_ccc_write = device_i3c_target_ccc_write;
    device_class_set_props(dc, dwc_i3c_target_properties);
}

static const TypeInfo dwc_i3c_device_info = {
    .name = TYPE_DWC_I3C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DwcI3CDevice),
    .class_init = dwc_i3c_device_class_init,
};

static const TypeInfo dwc_i3c_device_target_info = {
    .name = TYPE_DWC_I3C_TARGET,
    .parent = TYPE_I3C_TARGET,
    .instance_size = sizeof(DwcI3CTarget),
    .class_init = dwc_i3c_device_target_class_init,
};

static void dwc_i3c_register_types(void)
{
    type_register_static(&dwc_i3c_device_info);
    type_register_static(&dwc_i3c_device_target_info);
}

type_init(dwc_i3c_register_types);
