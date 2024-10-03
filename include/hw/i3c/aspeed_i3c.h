/*
 * ASPEED I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2023 Google, LLC
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_I3C_H
#define ASPEED_I3C_H

#include "qemu/fifo32.h"
#include "hw/i3c/i3c.h"
#include "hw/sysbus.h"

#define TYPE_ASPEED_I3C "aspeed.i3c"
#define TYPE_ASPEED_I3C_DEVICE "aspeed.i3c.device"
OBJECT_DECLARE_TYPE(AspeedI3CState, AspeedI3CClass, ASPEED_I3C)

#define ASPEED_I3C_NR_REGS (0x70 >> 2)
#define ASPEED_I3C_DEVICE_NR_REGS (0x300 >> 2)
#define ASPEED_I3C_NR_DEVICES 6

#define ASPEED_I3C_CMD_QUEUE_CAPACITY  0x10
#define ASPEED_I3C_RESP_QUEUE_CAPACITY 0x10
#define ASPEED_I3C_TX_QUEUE_CAPACITY   0x40
#define ASPEED_I3C_RX_QUEUE_CAPACITY   0x40
#define ASPEED_I3C_IBI_QUEUE_CAPACITY  0x10

/* From datasheet. */
#define ASPEED_I3C_CMD_ATTR_TRANSFER_CMD 0
#define ASPEED_I3C_CMD_ATTR_TRANSFER_ARG 1
#define ASPEED_I3C_CMD_ATTR_SHORT_DATA_ARG 2
#define ASPEED_I3C_CMD_ATTR_ADDR_ASSIGN_CMD 3

/* Enum values from datasheet. */
typedef enum AspeedI3CRespQueueErr {
    ASPEED_I3C_RESP_QUEUE_ERR_NONE = 0,
    ASPEED_I3C_RESP_QUEUE_ERR_CRC = 1,
    ASPEED_I3C_RESP_QUEUE_ERR_PARITY = 2,
    ASPEED_I3C_RESP_QUEUE_ERR_FRAME = 3,
    ASPEED_I3C_RESP_QUEUE_ERR_BROADCAST_NACK = 4,
    ASPEED_I3C_RESP_QUEUE_ERR_DAA_NACK = 5,
    ASPEED_I3C_RESP_QUEUE_ERR_OVERFLOW = 6,
    ASPEED_I3C_RESP_QUEUE_ERR_ABORTED = 8,
    ASPEED_I3C_RESP_QUEUE_ERR_I2C_NACK = 9,
} AspeedI3CRespQueueErr;

typedef enum AspeedI3CTransferState {
    ASPEED_I3C_TRANSFER_STATE_IDLE = 0x00,
    ASPEED_I3C_TRANSFER_STATE_START = 0x01,
    ASPEED_I3C_TRANSFER_STATE_RESTART = 0x02,
    ASPEED_I3C_TRANSFER_STATE_STOP = 0x03,
    ASPEED_I3C_TRANSFER_STATE_START_HOLD = 0x04,
    ASPEED_I3C_TRANSFER_STATE_BROADCAST_W = 0x05,
    ASPEED_I3C_TRANSFER_STATE_BROADCAST_R = 0x06,
    ASPEED_I3C_TRANSFER_STATE_DAA = 0x07,
    ASPEED_I3C_TRANSFER_STATE_DAA_GEN = 0x08,
    ASPEED_I3C_TRANSFER_STATE_CCC_BYTE = 0x0b,
    ASPEED_I3C_TRANSFER_STATE_HDR_CMD = 0x0c,
    ASPEED_I3C_TRANSFER_STATE_WRITE = 0x0d,
    ASPEED_I3C_TRANSFER_STATE_READ = 0x0e,
    ASPEED_I3C_TRANSFER_STATE_IBI_READ = 0x0f,
    ASPEED_I3C_TRANSFER_STATE_IBI_DIS = 0x10,
    ASPEED_I3C_TRANSFER_STATE_HDR_DDR_CRC = 0x11,
    ASPEED_I3C_TRANSFER_STATE_CLK_STRETCH = 0x12,
    ASPEED_I3C_TRANSFER_STATE_HALT = 0x13,
} AspeedI3CTransferState;

typedef enum AspeedI3CTransferStatus {
    ASPEED_I3C_TRANSFER_STATUS_IDLE = 0x00,
    ASPEED_I3C_TRANSFER_STATUS_BROACAST_CCC = 0x01,
    ASPEED_I3C_TRANSFER_STATUS_DIRECT_CCC_W = 0x02,
    ASPEED_I3C_TRANSFER_STATUS_DIRECT_CCC_R = 0x03,
    ASPEED_I3C_TRANSFER_STATUS_ENTDAA = 0x04,
    ASPEED_I3C_TRANSFER_STATUS_SETDASA = 0x05,
    ASPEED_I3C_TRANSFER_STATUS_I3C_SDR_W = 0x06,
    ASPEED_I3C_TRANSFER_STATUS_I3C_SDR_R = 0x07,
    ASPEED_I3C_TRANSFER_STATUS_I2C_SDR_W = 0x08,
    ASPEED_I3C_TRANSFER_STATUS_I2C_SDR_R = 0x09,
    ASPEED_I3C_TRANSFER_STATUS_HDR_TS_W = 0x0a,
    ASPEED_I3C_TRANSFER_STATUS_HDR_TS_R = 0x0b,
    ASPEED_I3C_TRANSFER_STATUS_HDR_DDR_W = 0x0c,
    ASPEED_I3C_TRANSFER_STATUS_HDR_DDR_R = 0x0d,
    ASPEED_I3C_TRANSFER_STATUS_IBI = 0x0e,
    ASPEED_I3C_TRANSFER_STATUS_HALT = 0x0f,
} AspeedI3CTransferStatus;

/*
 * Transfer commands and arguments are 32-bit wide values that the user passes
 * into the command queue. We interpret each 32-bit word based on the cmd_attr
 * field.
 */
typedef struct AspeedI3CTransferCmd {
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
} AspeedI3CTransferCmd;

typedef struct AspeedI3CTransferArg {
    uint8_t cmd_attr:3;
    uint8_t resv:5;
    uint8_t db; /* Defining byte */
    uint16_t data_len;
} AspeedI3CTransferArg;

typedef struct AspeedI3CShortArg {
    uint8_t cmd_attr:3;
    uint8_t byte_strb:3;
    uint8_t resv:2;
    uint8_t byte0;
    uint8_t byte1;
    uint8_t byte2;
} AspeedI3CShortArg;

typedef struct AspeedI3CAddrAssignCmd {
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
} AspeedI3CAddrAssignCmd;

typedef union AspeedI3CCmdQueueData {
    uint32_t word;
    AspeedI3CTransferCmd transfer_cmd;
    AspeedI3CTransferArg transfer_arg;
    AspeedI3CShortArg short_arg;
    AspeedI3CAddrAssignCmd addr_assign_cmd;
} AspeedI3CCmdQueueData;

/*
 * When we receive an IBI with data, we need to store it temporarily until
 * the target is finished sending data. Then we can set the IBI queue status
 * appropriately.
 */
typedef struct AspeedI3CDeviceIBIData {
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
} AspeedI3CDeviceIBIData;

OBJECT_DECLARE_SIMPLE_TYPE(AspeedI3CDevice, ASPEED_I3C_DEVICE)
typedef struct AspeedI3CDevice {
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

    /* Temporary storage for IBI data. */
    AspeedI3CDeviceIBIData ibi_data;

    uint8_t id;
    uint32_t regs[ASPEED_I3C_DEVICE_NR_REGS];
} AspeedI3CDevice;

typedef struct AspeedI3CState {
    /* <private> */
    SysBusDevice parent;

    /* <public> */
    MemoryRegion iomem;
    MemoryRegion iomem_container;
    qemu_irq irq;

    uint32_t regs[ASPEED_I3C_NR_REGS];
    AspeedI3CDevice devices[ASPEED_I3C_NR_DEVICES];
} AspeedI3CState;
#endif /* ASPEED_I3C_H */
