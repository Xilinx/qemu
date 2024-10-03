/*
 * DwC I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2023 Google, LLC
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef DWC_I3C_H
#define DWC_I3C_H

#include "qemu/fifo32.h"
#include "hw/i3c/i3c.h"
#include "hw/sysbus.h"

#define TYPE_DWC_I3C "dwc.i3c"
#define TYPE_DWC_I3C_TARGET "dwc.i3c-target"

#define DWC_I3C_NR_REGS (0x300 >> 2)

#define DWC_I3C_CMD_QUEUE_CAPACITY  0x10
#define DWC_I3C_RESP_QUEUE_CAPACITY 0x10
#define DWC_I3C_TX_QUEUE_CAPACITY   0x40
#define DWC_I3C_RX_QUEUE_CAPACITY   0x40
#define DWC_I3C_IBI_QUEUE_CAPACITY  0x10

/* From datasheet. */
#define DWC_I3C_CMD_ATTR_TRANSFER_CMD 0
#define DWC_I3C_CMD_ATTR_TRANSFER_ARG 1
#define DWC_I3C_CMD_ATTR_SHORT_DATA_ARG 2
#define DWC_I3C_CMD_ATTR_ADDR_ASSIGN_CMD 3

/* Enum values from datasheet. */
typedef enum DwcI3CRespQueueErr {
    DWC_I3C_RESP_QUEUE_ERR_NONE = 0,
    DWC_I3C_RESP_QUEUE_ERR_CRC = 1,
    DWC_I3C_RESP_QUEUE_ERR_PARITY = 2,
    DWC_I3C_RESP_QUEUE_ERR_FRAME = 3,
    DWC_I3C_RESP_QUEUE_ERR_BROADCAST_NACK = 4,
    DWC_I3C_RESP_QUEUE_ERR_DAA_NACK = 5,
    DWC_I3C_RESP_QUEUE_ERR_OVERFLOW = 6,
    DWC_I3C_RESP_QUEUE_ERR_ABORTED = 8,
    DWC_I3C_RESP_QUEUE_ERR_I2C_NACK = 9,
} DwcI3CRespQueueErr;

typedef enum DwcI3CTransferState {
    DWC_I3C_TRANSFER_STATE_IDLE = 0x00,
    DWC_I3C_TRANSFER_STATE_START = 0x01,
    DWC_I3C_TRANSFER_STATE_RESTART = 0x02,
    DWC_I3C_TRANSFER_STATE_STOP = 0x03,
    DWC_I3C_TRANSFER_STATE_START_HOLD = 0x04,
    DWC_I3C_TRANSFER_STATE_BROADCAST_W = 0x05,
    DWC_I3C_TRANSFER_STATE_BROADCAST_R = 0x06,
    DWC_I3C_TRANSFER_STATE_DAA = 0x07,
    DWC_I3C_TRANSFER_STATE_DAA_GEN = 0x08,
    DWC_I3C_TRANSFER_STATE_CCC_BYTE = 0x0b,
    DWC_I3C_TRANSFER_STATE_HDR_CMD = 0x0c,
    DWC_I3C_TRANSFER_STATE_WRITE = 0x0d,
    DWC_I3C_TRANSFER_STATE_READ = 0x0e,
    DWC_I3C_TRANSFER_STATE_IBI_READ = 0x0f,
    DWC_I3C_TRANSFER_STATE_IBI_DIS = 0x10,
    DWC_I3C_TRANSFER_STATE_HDR_DDR_CRC = 0x11,
    DWC_I3C_TRANSFER_STATE_CLK_STRETCH = 0x12,
    DWC_I3C_TRANSFER_STATE_HALT = 0x13,
} DwcI3CTransferState;

typedef enum DwcI3CTransferStatus {
    DWC_I3C_TRANSFER_STATUS_IDLE = 0x00,
    DWC_I3C_TRANSFER_STATUS_BROACAST_CCC = 0x01,
    DWC_I3C_TRANSFER_STATUS_DIRECT_CCC_W = 0x02,
    DWC_I3C_TRANSFER_STATUS_DIRECT_CCC_R = 0x03,
    DWC_I3C_TRANSFER_STATUS_ENTDAA = 0x04,
    DWC_I3C_TRANSFER_STATUS_SETDASA = 0x05,
    DWC_I3C_TRANSFER_STATUS_I3C_SDR_W = 0x06,
    DWC_I3C_TRANSFER_STATUS_I3C_SDR_R = 0x07,
    DWC_I3C_TRANSFER_STATUS_I2C_SDR_W = 0x08,
    DWC_I3C_TRANSFER_STATUS_I2C_SDR_R = 0x09,
    DWC_I3C_TRANSFER_STATUS_HDR_TS_W = 0x0a,
    DWC_I3C_TRANSFER_STATUS_HDR_TS_R = 0x0b,
    DWC_I3C_TRANSFER_STATUS_HDR_DDR_W = 0x0c,
    DWC_I3C_TRANSFER_STATUS_HDR_DDR_R = 0x0d,
    DWC_I3C_TRANSFER_STATUS_IBI = 0x0e,
    DWC_I3C_TRANSFER_STATUS_HALT = 0x0f,
} DwcI3CTransferStatus;

/*
 * Transfer commands and arguments are 32-bit wide values that the user passes
 * into the command queue. We interpret each 32-bit word based on the cmd_attr
 * field.
 */
typedef struct DwcI3CTransferCmd {
    uint8_t cmd_attr:3;
    uint8_t tid:4; /* Transaction ID */
    uint16_t cmd:8;
    uint8_t cp:1; /* Command present */
    uint8_t dev_index:5;
    uint8_t speed:3;
    uint8_t resv0:1;
    uint8_t dbp:1; /* Defining byte present */
    uint8_t roc:1; /* Response on completion */
    uint8_t sdap:1; /* Short data argument present */
    uint8_t rnw:1; /* Read not write */
    uint8_t resv1:1;
    uint8_t toc:1; /* Termination (I3C STOP) on completion */
    uint8_t pec:1; /* Parity error check enabled */
} DwcI3CTransferCmd;

typedef struct DwcI3CTransferArg {
    uint8_t cmd_attr:3;
    uint8_t resv:5;
    uint8_t db; /* Defining byte */
    uint16_t data_len;
} DwcI3CTransferArg;

typedef struct DwcI3CShortArg {
    uint8_t cmd_attr:3;
    uint8_t byte_strb:3;
    uint8_t resv:2;
    uint8_t byte0;
    uint8_t byte1;
    uint8_t byte2;
} DwcI3CShortArg;

typedef struct DwcI3CAddrAssignCmd {
    uint8_t cmd_attr:3;
    uint8_t tid:4; /* Transaction ID */
    uint16_t cmd:8;
    uint8_t resv0:1;
    uint8_t dev_index:5;
    uint16_t dev_count:5;
    uint8_t roc:1; /* Response on completion */
    uint8_t resv1:3;
    uint8_t toc:1; /* Termination (I3C STOP) on completion */
    uint8_t resv2:1;
} DwcI3CAddrAssignCmd;

typedef union DwcI3CCmdQueueData {
    uint32_t word;
    DwcI3CTransferCmd transfer_cmd;
    DwcI3CTransferArg transfer_arg;
    DwcI3CShortArg short_arg;
    DwcI3CAddrAssignCmd addr_assign_cmd;
} DwcI3CCmdQueueData;

/*
 * When we receive an IBI with data, we need to store it temporarily until
 * the target is finished sending data. Then we can set the IBI queue status
 * appropriately.
 */
typedef struct DwcI3CDeviceIBIData {
    /* Do we notify the user that an IBI was NACKed? */
    bool notify_ibi_nack;
    /* Intermediate storage of IBI_QUEUE_STATUS. */
    uint32_t ibi_queue_status;
    /* Temporary buffer to store IBI data from the target. */
    Fifo8 ibi_intermediate_queue;
    /* The address we should send a CCC_DISEC to. */
    uint8_t disec_addr;
    /* The byte we should send along with the CCC_DISEC. */
    uint8_t disec_byte;
    /* Should we send a direct DISEC CCC? (As opposed to global). */
    bool send_direct_disec;
    /* Was this IBI NACKed? */
    bool ibi_nacked;
} DwcI3CDeviceIBIData;

OBJECT_DECLARE_SIMPLE_TYPE(DwcI3CDevice, DWC_I3C)
typedef struct DwcI3CDevice {
    /* <private> */
    SysBusDevice parent;

    /* <public> */
    MemoryRegion mr;
    qemu_irq irq;
    I3CBus *bus;

    Fifo32 cmd_queue;
    Fifo32 resp_queue;
    Fifo32 tx_queue;
    Fifo32 rx_queue;
    Fifo32 ibi_queue;

    struct {
        uint8_t device_role;
        uint8_t buf_lvl_sel;
        uint8_t num_devices;
        uint8_t ibi_buf_lvl_sel;
        bool slv_ibi;
        uint16_t slv_mwl;
        uint16_t slv_mrl;
        bool slv_static_addr_en;
        uint8_t slv_static_addr;
    } cfg;
    I3CTarget *i3c_target;

    struct {
        I3CEvent curr_event;
        uint32_t tr_bytes;
        DwcI3CCmdQueueData tx_cmd;
        DwcI3CCmdQueueData tx_arg;
   } target;

    /* Temporary storage for IBI data. */
    DwcI3CDeviceIBIData ibi_data;
    uint8_t id;
    uint32_t regs[DWC_I3C_NR_REGS];
} DwcI3CDevice;

OBJECT_DECLARE_SIMPLE_TYPE(DwcI3CTarget, DWC_I3C_TARGET)
typedef struct DwcI3CTarget {
    I3CTarget parent;

    DwcI3CDevice *dwc_i3c;
} DwcI3CTarget;
#endif
