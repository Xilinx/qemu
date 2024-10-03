/*
 * ASPEED I3C Controller
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
#include "hw/i3c/aspeed_i3c.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

/* I3C Controller Registers */
REG32(I3C1_REG0, 0x10)
REG32(I3C1_REG1, 0x14)
    FIELD(I3C1_REG1, I2C_MODE,      0,  1)
    FIELD(I3C1_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C1_REG1, ACT_MODE,      2,  2)
    FIELD(I3C1_REG1, PENDING_INT,   4,  4)
    FIELD(I3C1_REG1, SA,            8,  7)
    FIELD(I3C1_REG1, SA_EN,         15, 1)
    FIELD(I3C1_REG1, INST_ID,       16, 4)
REG32(I3C2_REG0, 0x20)
REG32(I3C2_REG1, 0x24)
    FIELD(I3C2_REG1, I2C_MODE,      0,  1)
    FIELD(I3C2_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C2_REG1, ACT_MODE,      2,  2)
    FIELD(I3C2_REG1, PENDING_INT,   4,  4)
    FIELD(I3C2_REG1, SA,            8,  7)
    FIELD(I3C2_REG1, SA_EN,         15, 1)
    FIELD(I3C2_REG1, INST_ID,       16, 4)
REG32(I3C3_REG0, 0x30)
REG32(I3C3_REG1, 0x34)
    FIELD(I3C3_REG1, I2C_MODE,      0,  1)
    FIELD(I3C3_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C3_REG1, ACT_MODE,      2,  2)
    FIELD(I3C3_REG1, PENDING_INT,   4,  4)
    FIELD(I3C3_REG1, SA,            8,  7)
    FIELD(I3C3_REG1, SA_EN,         15, 1)
    FIELD(I3C3_REG1, INST_ID,       16, 4)
REG32(I3C4_REG0, 0x40)
REG32(I3C4_REG1, 0x44)
    FIELD(I3C4_REG1, I2C_MODE,      0,  1)
    FIELD(I3C4_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C4_REG1, ACT_MODE,      2,  2)
    FIELD(I3C4_REG1, PENDING_INT,   4,  4)
    FIELD(I3C4_REG1, SA,            8,  7)
    FIELD(I3C4_REG1, SA_EN,         15, 1)
    FIELD(I3C4_REG1, INST_ID,       16, 4)
REG32(I3C5_REG0, 0x50)
REG32(I3C5_REG1, 0x54)
    FIELD(I3C5_REG1, I2C_MODE,      0,  1)
    FIELD(I3C5_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C5_REG1, ACT_MODE,      2,  2)
    FIELD(I3C5_REG1, PENDING_INT,   4,  4)
    FIELD(I3C5_REG1, SA,            8,  7)
    FIELD(I3C5_REG1, SA_EN,         15, 1)
    FIELD(I3C5_REG1, INST_ID,       16, 4)
REG32(I3C6_REG0, 0x60)
REG32(I3C6_REG1, 0x64)
    FIELD(I3C6_REG1, I2C_MODE,      0,  1)
    FIELD(I3C6_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C6_REG1, ACT_MODE,      2,  2)
    FIELD(I3C6_REG1, PENDING_INT,   4,  4)
    FIELD(I3C6_REG1, SA,            8,  7)
    FIELD(I3C6_REG1, SA_EN,         15, 1)
    FIELD(I3C6_REG1, INST_ID,       16, 4)

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
    FIELD(HW_CAPABILITY, ENTDAA,  0, 1)
    FIELD(HW_CAPABILITY, HDR_DDR, 3, 1)
    FIELD(HW_CAPABILITY, HDR_TS,  4, 1)
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
REG32(SLV_MIPI_PID_VALUE,           0x70)
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
/* Device characteristic table fields */
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC1, 0x200)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, 0x200)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, DYNAMIC_ADDR, 0, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, DCR, 8, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, BCR, 16, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC_SECONDARY, STATIC_ADDR, 24, 8)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC2, 0x204)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC2, MSB_PID, 0, 16)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC3, 0x208)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC3, DCR, 0, 8)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC3, BCR, 8, 8)
REG32(DEVICE_CHARACTERISTIC_TABLE_LOC4, 0x20c)
    FIELD(DEVICE_CHARACTERISTIC_TABLE_LOC4, DEV_DYNAMIC_ADDR, 0, 8)
/* Dev addr table fields */
REG32(DEVICE_ADDR_TABLE_LOC1, 0x280)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_STATIC_ADDR, 0, 7)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_PEC_EN, 11, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_WITH_DATA, 12, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, SIR_REJECT, 13, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, MR_REJECT, 14, 1)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_DYNAMIC_ADDR, 16, 8)
    FIELD(DEVICE_ADDR_TABLE_LOC1, IBI_ADDR_MASK, 24, 2)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_NACK_RETRY_CNT, 29, 2)
    FIELD(DEVICE_ADDR_TABLE_LOC1, LEGACY_I2C_DEVICE, 31, 1)

static const uint32_t ast2600_i3c_device_resets[ASPEED_I3C_DEVICE_NR_REGS] = {
    [R_HW_CAPABILITY]               = 0x000e00bf,
    [R_QUEUE_THLD_CTRL]             = 0x01000101,
    [R_I3C_VER_ID]                  = 0x3130302a,
    [R_I3C_VER_TYPE]                = 0x6c633033,
    [R_DEVICE_ADDR_TABLE_POINTER]   = 0x00080280,
    [R_DEV_CHAR_TABLE_POINTER]      = 0x00020200,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0x000000b0,
    [R_SLV_MAX_LEN]                 = 0x00ff00ff,
};

static uint64_t aspeed_i3c_device_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(opaque);
    uint32_t addr = offset >> 2;
    uint64_t value;

    switch (addr) {
    case R_COMMAND_QUEUE_PORT:
        value = 0;
        break;
    default:
        value = s->regs[addr];
        break;
    }

    trace_aspeed_i3c_device_read(s->id, offset, value);

    return value;
}

static void aspeed_i3c_device_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(opaque);
    uint32_t addr = offset >> 2;

    trace_aspeed_i3c_device_write(s->id, offset, value);

    switch (addr) {
    case R_HW_CAPABILITY:
    case R_RESPONSE_QUEUE_PORT:
    case R_IBI_QUEUE_DATA:
    case R_QUEUE_STATUS_LEVEL:
    case R_PRESENT_STATE:
    case R_CCC_DEVICE_STATUS:
    case R_DEVICE_ADDR_TABLE_POINTER:
    case R_VENDOR_SPECIFIC_REG_POINTER:
    case R_SLV_CHAR_CTRL:
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
    case R_RX_TX_DATA_PORT:
        break;
    case R_RESET_CTRL:
        break;
    default:
        s->regs[addr] = value;
        break;
    }
}

static const VMStateDescription aspeed_i3c_device_vmstate = {
    .name = TYPE_ASPEED_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32_ARRAY(regs, AspeedI3CDevice, ASPEED_I3C_DEVICE_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const MemoryRegionOps aspeed_i3c_device_ops = {
    .read = aspeed_i3c_device_read,
    .write = aspeed_i3c_device_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void aspeed_i3c_device_reset(DeviceState *dev)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(dev);

    memcpy(s->regs, ast2600_i3c_device_resets, sizeof(s->regs));
}

static void aspeed_i3c_device_realize(DeviceState *dev, Error **errp)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(dev);
    g_autofree char *name = g_strdup_printf(TYPE_ASPEED_I3C_DEVICE ".%d",
                                            s->id);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    memory_region_init_io(&s->mr, OBJECT(s), &aspeed_i3c_device_ops,
                          s, name, ASPEED_I3C_DEVICE_NR_REGS << 2);
}

static uint64_t aspeed_i3c_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedI3CState *s = ASPEED_I3C(opaque);
    uint64_t val = 0;

    val = s->regs[addr >> 2];

    trace_aspeed_i3c_read(addr, val);

    return val;
}

static void aspeed_i3c_write(void *opaque,
                             hwaddr addr,
                             uint64_t data,
                             unsigned int size)
{
    AspeedI3CState *s = ASPEED_I3C(opaque);

    trace_aspeed_i3c_write(addr, data);

    addr >>= 2;

    /* I3C controller register */
    switch (addr) {
    case R_I3C1_REG1:
    case R_I3C2_REG1:
    case R_I3C3_REG1:
    case R_I3C4_REG1:
    case R_I3C5_REG1:
    case R_I3C6_REG1:
        if (data & R_I3C1_REG1_I2C_MODE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Unsupported I2C mode [0x%08" HWADDR_PRIx
                          "]=%08" PRIx64 "\n",
                          __func__, addr << 2, data);
            break;
        }
        if (data & R_I3C1_REG1_SA_EN_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Unsupported slave mode [%08" HWADDR_PRIx
                          "]=0x%08" PRIx64 "\n",
                          __func__, addr << 2, data);
            break;
        }
        s->regs[addr] = data;
        break;
    default:
        s->regs[addr] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_i3c_ops = {
    .read = aspeed_i3c_read,
    .write = aspeed_i3c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    }
};

static void aspeed_i3c_reset(DeviceState *dev)
{
    AspeedI3CState *s = ASPEED_I3C(dev);
    memset(s->regs, 0, sizeof(s->regs));
}

static void aspeed_i3c_instance_init(Object *obj)
{
    AspeedI3CState *s = ASPEED_I3C(obj);
    int i;

    for (i = 0; i < ASPEED_I3C_NR_DEVICES; ++i) {
        object_initialize_child(obj, "device[*]", &s->devices[i],
                TYPE_ASPEED_I3C_DEVICE);
    }
}

static void aspeed_i3c_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedI3CState *s = ASPEED_I3C(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init(&s->iomem_container, OBJECT(s),
            TYPE_ASPEED_I3C ".container", 0x8000);

    sysbus_init_mmio(sbd, &s->iomem_container);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_i3c_ops, s,
            TYPE_ASPEED_I3C ".regs", ASPEED_I3C_NR_REGS << 2);

    memory_region_add_subregion(&s->iomem_container, 0x0, &s->iomem);

    for (i = 0; i < ASPEED_I3C_NR_DEVICES; ++i) {
        Object *dev = OBJECT(&s->devices[i]);

        if (!object_property_set_uint(dev, "device-id", i, errp)) {
            return;
        }

        if (!sysbus_realize(SYS_BUS_DEVICE(dev), errp)) {
            return;
        }

        /*
         * Register Address of I3CX Device =
         *     (Base Address of Global Register) + (Offset of I3CX) + Offset
         * X = 0, 1, 2, 3, 4, 5
         * Offset of I3C0 = 0x2000
         * Offset of I3C1 = 0x3000
         * Offset of I3C2 = 0x4000
         * Offset of I3C3 = 0x5000
         * Offset of I3C4 = 0x6000
         * Offset of I3C5 = 0x7000
         */
        memory_region_add_subregion(&s->iomem_container,
                0x2000 + i * 0x1000, &s->devices[i].mr);
    }

}

static Property aspeed_i3c_device_properties[] = {
    DEFINE_PROP_UINT8("device-id", AspeedI3CDevice, id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_i3c_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Aspeed I3C Device";
    dc->realize = aspeed_i3c_device_realize;
    dc->reset = aspeed_i3c_device_reset;
    device_class_set_props(dc, aspeed_i3c_device_properties);
}

static const TypeInfo aspeed_i3c_device_info = {
    .name = TYPE_ASPEED_I3C_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedI3CDevice),
    .class_init = aspeed_i3c_device_class_init,
};

static const VMStateDescription vmstate_aspeed_i3c = {
    .name = TYPE_ASPEED_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedI3CState, ASPEED_I3C_NR_REGS),
        VMSTATE_STRUCT_ARRAY(devices, AspeedI3CState, ASPEED_I3C_NR_DEVICES, 1,
                             aspeed_i3c_device_vmstate, AspeedI3CDevice),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_i3c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_i3c_realize;
    dc->reset = aspeed_i3c_reset;
    dc->desc = "Aspeed I3C Controller";
    dc->vmsd = &vmstate_aspeed_i3c;
}

static const TypeInfo aspeed_i3c_info = {
    .name = TYPE_ASPEED_I3C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_i3c_instance_init,
    .instance_size = sizeof(AspeedI3CState),
    .class_init = aspeed_i3c_class_init,
};

static void aspeed_i3c_register_types(void)
{
    type_register_static(&aspeed_i3c_device_info);
    type_register_static(&aspeed_i3c_info);
}

type_init(aspeed_i3c_register_types);
