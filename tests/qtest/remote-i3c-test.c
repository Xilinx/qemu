/*
 * QTest testcase for the remote I3C device, using the AST2600 I3C controller.
 *
 * Copyright 2023 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/registerfields.h"
#include "hw/i3c/i3c.h"
#include "hw/i3c/remote-i3c.h"
#include "hw/i3c/aspeed_i3c.h"

/* Starting address of the AST2600 I3C block. */
#define ASPEED_I3C_BASE 0x1e7a0000
/* Offset to the first controller in the block. */
#define ASPEED_I3C_CONTROLLER_OFFSET 0x2000
#define I3C(x) (ASPEED_I3C_BASE + ASPEED_I3C_CONTROLLER_OFFSET + ((x) * 0x1000))
#define TARGET_ADDR 0x10

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
    FIELD(IBI_QUEUE_STATUS, IBI_RX_STATUS,  28, 4)
REG32(IBI_QUEUE_DATA,               0x18)
REG32(QUEUE_STATUS_LEVEL,           0x4c)
    FIELD(QUEUE_STATUS_LEVEL, CMD_QUEUE_EMPTY_LOC,  0, 8)
    FIELD(QUEUE_STATUS_LEVEL, RESP_BUF_BLR,         8, 8)
    FIELD(QUEUE_STATUS_LEVEL, IBI_BUF_BLR,          16, 8)
    FIELD(QUEUE_STATUS_LEVEL, IBI_STATUS_CNT,       24, 5)
REG32(DATA_BUFFER_STATUS_LEVEL,     0x50)
    FIELD(DATA_BUFFER_STATUS_LEVEL, TX_BUF_EMPTY_LOC, 0, 8)
    FIELD(DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR,       16, 8)
/* Dev addr table fields */
REG32(DEVICE_ADDR_TABLE_LOC1, 0x280)
    FIELD(DEVICE_ADDR_TABLE_LOC1, DEV_DYNAMIC_ADDR, 16, 8)

typedef union AspeedI3CResponse {
    uint32_t word;
    uint16_t data_len;
    uint8_t ccc_type;
    uint8_t tid:4;
    uint8_t err:4;
} AspeedI3CResponse;

static int sock;
static int fd;

static in_port_t open_socket(void)
{
    struct sockaddr_in myaddr;
    struct timeval timeout = { .tv_sec = 1, };
    socklen_t addrlen;

    myaddr.sin_family = AF_INET;
    myaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    myaddr.sin_port = 0;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    g_assert(sock != -1);
    g_assert(bind(sock, (struct sockaddr *) &myaddr, sizeof(myaddr)) != -1);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    addrlen = sizeof(myaddr);
    g_assert(getsockname(sock, (struct sockaddr *) &myaddr , &addrlen) != -1);
    g_assert(listen(sock, 1) != -1);
    return ntohs(myaddr.sin_port);
}

static void setup_fd(void)
{
    fd_set readfds;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    g_assert(select(sock + 1, &readfds, NULL, NULL, NULL) == 1);

    fd = accept(sock, NULL, 0);
}

static AspeedI3CTransferCmd aspeed_i3c_create_xfer_cmd(uint8_t cmd,
                                                       uint8_t dev_index,
                                                       bool rnw,
                                                       bool dbp)
{
    return ((AspeedI3CTransferCmd) {
        .cmd_attr = ASPEED_I3C_CMD_ATTR_TRANSFER_CMD,
        .tid = 0x01,
        .cmd = cmd,
        .cp = (cmd != 0) ? 1 : 0,
        .dev_index = dev_index,
        .speed = 0, /* SDR */
        .dbp = dbp,
        .roc = 1,
        .sdap = (cmd == 0x02) ? 1 : 0, /* Short data arg present. */
        .rnw = rnw,
        .toc = 1,
        .pec = 0
    });
}

static AspeedI3CTransferArg aspeed_i3c_create_xfer_arg(uint8_t db,
                                                       uint16_t data_len)
{
    return ((AspeedI3CTransferArg) {
        .cmd_attr = ASPEED_I3C_CMD_ATTR_TRANSFER_ARG,
        .db = db,
        .data_len = data_len,
    });
}

static void aspeed_i3c_enable(uint32_t base)
{
    uint32_t val = readl(base + A_DEVICE_CTRL);
    val = FIELD_DP32(val, DEVICE_CTRL, I3C_RESUME, 1);
    val = FIELD_DP32(val, DEVICE_CTRL, I3C_EN, 1);
    writel(base + A_DEVICE_CTRL, val);
    /*
     * Sanity check the enable write. I3C_RESUME is auto-cleared so don't
     * check it.
     */
    g_assert(readl(base + A_DEVICE_CTRL) & R_DEVICE_CTRL_I3C_EN_MASK);
}

static AspeedI3CResponse aspeed_i3c_read_resp(uint32_t base)
{
    AspeedI3CResponse resp;
    uint32_t queue_status = readl(base + A_QUEUE_STATUS_LEVEL);
    /* No response to read. */
    if (FIELD_EX32(queue_status, QUEUE_STATUS_LEVEL, RESP_BUF_BLR) == 0) {
        resp.word = 0;
    } else {
        resp.word = readl(base + A_DEVICE_CTRL);
    }
    return resp;
}

static void aspeed_i3c_send(uint32_t base, uint8_t dev_index,
                            const uint32_t *data, uint16_t len)
{
    AspeedI3CCmdQueueData cmd;
    AspeedI3CCmdQueueData arg;
    uint16_t words_txed = 0;

    /* Start doing the transfer. */
    while (words_txed < len) {
        /* Push data to the TX queue. */
        uint32_t tx_num_empty = FIELD_EX32(readl(base +
                                                 A_DATA_BUFFER_STATUS_LEVEL),
                                           DATA_BUFFER_STATUS_LEVEL,
                                           TX_BUF_EMPTY_LOC);
        for (uint16_t i = 0; i < tx_num_empty; i++) {
            writel(base + A_RX_TX_DATA_PORT, data[words_txed]);
            words_txed++;
            /* We have no more to transfer, bail early. */
            if (words_txed >= len) {
                break;
            }
        }

        /* Now that the data is in the queue, we can start our transfer. */
        /*
         * CMD is ignored due to this not being a CCC, and there is no defining
         * byte, also because this isn't a CCC.
         */
        cmd.transfer_cmd = aspeed_i3c_create_xfer_cmd(0, dev_index, false,
                                                      false);
        arg.transfer_arg = aspeed_i3c_create_xfer_arg(0, len * sizeof(*data));
        /* Order to push is arg then command. */
        writel(base + A_COMMAND_QUEUE_PORT, arg.word);
        writel(base + A_COMMAND_QUEUE_PORT, cmd.word);
    }
}

static void aspeed_i3c_send_ccc(uint32_t base, uint8_t ccc_cmd)
{
    AspeedI3CCmdQueueData cmd;
    AspeedI3CCmdQueueData arg;

    cmd.transfer_cmd = aspeed_i3c_create_xfer_cmd(ccc_cmd, 0, false,
                                                  false);
    arg.transfer_arg = aspeed_i3c_create_xfer_arg(0, 0);
    /* Order to push is arg then command. */
    writel(base + A_COMMAND_QUEUE_PORT, arg.word);
    writel(base + A_COMMAND_QUEUE_PORT, cmd.word);
}

static void aspeed_i3c_recv(uint32_t base, uint8_t dev_index, uint8_t *data,
                            uint16_t len)
{
    AspeedI3CCmdQueueData cmd;
    AspeedI3CCmdQueueData arg;
    uint16_t bytes_rxed = 0;
    uint32_t *p32_data = (uint32_t *)data;

    /* Start doing the transfer. */
    while (bytes_rxed < len) {
        /* Send the RX request. */
        /*
         * CMD is ignored due to this not being a CCC, and there is no defining
         * byte, also because this isn't a CCC.
         */
        cmd.transfer_cmd = aspeed_i3c_create_xfer_cmd(0, dev_index, true,
                                                      false);
        uint16_t num_to_rx = (len - bytes_rxed) > ASPEED_I3C_RX_QUEUE_CAPACITY ?
            ASPEED_I3C_RX_QUEUE_CAPACITY : (len - bytes_rxed);
        arg.transfer_arg = aspeed_i3c_create_xfer_arg(0, num_to_rx);
        /* Order to push is arg then command. */
        writel(base + A_COMMAND_QUEUE_PORT, arg.word);
        writel(base + A_COMMAND_QUEUE_PORT, cmd.word);

        /* Read the data from the data RX queue. */
        uint32_t rx_word_num =
            FIELD_EX32(readl(base + A_DATA_BUFFER_STATUS_LEVEL),
                             DATA_BUFFER_STATUS_LEVEL, RX_BUF_BLR);
        for (uint16_t i = 0; i < rx_word_num; i++) {
            *p32_data = readl(base + A_RX_TX_DATA_PORT);
            p32_data++;
            bytes_rxed += 4;
        }
    }
}

static void assert_good_resp(uint32_t base)
{
    /* We expect a good response from this. */
    AspeedI3CResponse resp = aspeed_i3c_read_resp(base);
    g_assert(resp.err == ASPEED_I3C_RESP_QUEUE_ERR_NONE);
}

static void read_data(uint8_t *data, size_t len)
{
    ssize_t ret;
    size_t len_read = 0;

    while (len_read < len) {
        ret = read(fd, &data[len_read], len);
        g_assert(ret != -1);
        len_read += ret;
    }
}

static void remote_i3c_read_and_verify(const uint8_t *expected_data, size_t len)
{
    g_autofree uint8_t *data_read = g_new0(uint8_t, len);

    read_data(data_read, len);
    g_assert(memcmp(data_read, expected_data, len) == 0);
}

static void add_targets_to_bus(uint32_t base)
{
    /* Arbitrary large enough size. */
    uint8_t remote_target_expected_data[8];

    /* Send SATAASA to the remote target. */
    aspeed_i3c_send_ccc(base, I3C_CCC_SETAASA);
    /*
     * Verify everything is good.
     * The remote target should receive:
     * - an I3C_START event
     * - the size of the CCC packet as a LE uint32
     * - the CCC
     * - then an I3C_STOP event.
     * The controller should have a good response in the queue.
     */
    assert_good_resp(base);
    remote_target_expected_data[0] = REMOTE_I3C_START_SEND;
    remote_target_expected_data[1] = REMOTE_I3C_HANDLE_CCC_WRITE;
    uint32_t *p32 = (uint32_t *)&remote_target_expected_data[2];
    *p32 = htole32(1);
    remote_target_expected_data[6] = I3C_CCC_SETAASA;
    remote_target_expected_data[7] = REMOTE_I3C_STOP;
    remote_i3c_read_and_verify(remote_target_expected_data, 8);

    /*
     * Populate the device table. On a real system we would either:
     * - populate the table and send ENTDAA, then probe the addresses to see who
     *   exists.
     * - SETAASA and then go through a list addresses to see who exists, probe
     *   them, and add them to the table.
     * We're doing the SETAASA way, minus the probing portion, so just add the
     * known address to the table.
     */
    uint32_t val = 0;
    val = FIELD_DP32(val, DEVICE_ADDR_TABLE_LOC1, DEV_DYNAMIC_ADDR,
                     TARGET_ADDR);
    writel(base + A_DEVICE_ADDR_TABLE_LOC1, val);
}

static void send_and_verify(uint32_t i3c_base, const uint32_t *data, size_t len)
{
    /*
     * Add padding to the data_read packet, since the remote target will receive
     * extra bytes that include the I3C START and STOP events, along with the
     * length of the packet, and the data packet itself.
     */
    uint32_t data_size = len * sizeof(*data);
    size_t expected_data_len = data_size + 7;
    g_autofree uint8_t *remote_target_expected_data = g_new0(uint8_t,
                                                             expected_data_len);
    remote_target_expected_data[0] = REMOTE_I3C_START_SEND;
    remote_target_expected_data[1] = REMOTE_I3C_SEND;
    uint32_t *p32 = (uint32_t *)&remote_target_expected_data[2];
    *p32 = htole32(data_size);
    memcpy(&remote_target_expected_data[6], data, data_size);
    remote_target_expected_data[data_size + 6] = REMOTE_I3C_STOP;

    aspeed_i3c_send(i3c_base, 0, data, len);
    assert_good_resp(i3c_base);
    remote_i3c_read_and_verify(remote_target_expected_data, expected_data_len);
}

/* Remote target RX, e.g. controller -> target. */
static void test_remote_i3c_rx(gconstpointer test_data)
{
    uint32_t controller_num = *(uint32_t *)test_data;
    uint32_t i3c_base = I3C(controller_num);
    /*
     * The Aspeed controller expects data in 32-bit words, so make this 32-bits.
     */
    const uint32_t data[] = {7, 6, 5, 4, 3, 2, 1, 0};
    /* Enable the controller. */
    aspeed_i3c_enable(i3c_base);
    /*
     * Tell the target to use its static address as its dynamic address, and
     * populate the device table.
     */
    add_targets_to_bus(i3c_base);
    /* Now we can test sending data to the target. */
    send_and_verify(i3c_base, data, ARRAY_SIZE(data));
}

static void read_and_verify(uint32_t i3c_base, const uint8_t *data, size_t len)
{
    g_autofree uint8_t *data_received = g_new0(uint8_t, len);

    /* Send the I3C recv request. */
    aspeed_i3c_recv(i3c_base, 0, data_received, len);
    /*
     * Verify everything is okay. Anything on the remote I3C protocol level is
     * handled by the remote target thread. We just need to check that we
     * received what we expected.
     */
    assert_good_resp(i3c_base);
    g_assert(memcmp(data_received, data, len) == 0);
}

static void *remote_target_thread(void *arg)
{
    uint8_t byte;
    uint32_t bytes_to_send;
    uint32_t bytes_to_send_le;
    const uint8_t *data = (const uint8_t *)arg;

    /* Loop forever reading and parsing incoming data. */
    while (1) {
        /*
         * We can error out during program teardown due to the socket closing,
         * so don't assert.
         * If this has a proper error during test, the main thread will error
         * due to the target thread (this one) not sending anything.
         */
        if (read(fd, &byte, 1) != 1) {
            break;
        }

        switch (byte) {
        case REMOTE_I3C_START_RECV:
        case REMOTE_I3C_STOP:
            /* Don't care, do nothing. */
            break;
        case REMOTE_I3C_RECV:
            /* Read in the number of bytes the controller wants. */
            g_assert(read(fd, &bytes_to_send_le, sizeof(bytes_to_send_le)) ==
                          sizeof(bytes_to_send_le));
            bytes_to_send = le32toh(bytes_to_send_le);

            /*
             * Send the data. We first send the number of bytes we're sending as
             * a uint32 LE word (which is the same as the number of bytes the
             * controller is expecting), followed by the data.
             */
            g_assert(write(fd, (uint8_t *)&bytes_to_send_le,
                           sizeof(bytes_to_send_le)) ==
                     sizeof(bytes_to_send_le));
            g_assert(write(fd, data, bytes_to_send) == bytes_to_send);
            break;
        default:
            g_printerr("Remote target received unknown byte 0x%.2x\n", byte);
            g_assert_not_reached();
        }
    }

    return NULL;
}

/* Remote target TX, e.g. target -> controller. */
static void test_remote_i3c_tx(gconstpointer test_data)
{
    uint32_t controller_num = *(uint32_t *)test_data;
    uint32_t i3c_base = I3C(controller_num);
    /* Non-const since the thread prototype needs a non-const pointer. */
    uint8_t data[] = {7, 6, 5, 4, 3, 2, 1, 0};
    GThread *target_thread;
    /* Enable the controller. */
    aspeed_i3c_enable(i3c_base);
    /*
     * Tell the target to use its static address as its dynamic address, and
     * populate the device table.
     */
    add_targets_to_bus(i3c_base);

    /*
     * Now we can test receiving data from the target.
     * The target will need to respond while the controller is doing the I3C
     * receive (meaning we will be blocked on the remote target sending data to
     * us), so we need to make a separate thread for the remote target to send
     * data to the controller.
     */
    target_thread = g_thread_new("remote-target", remote_target_thread, data);
    read_and_verify(i3c_base, data, ARRAY_SIZE(data));
    g_thread_join(target_thread);
}

static void remote_i3c_ibi(const uint32_t *data, uint32_t len)
{
    /* Convert len to bytes to make math cleaner. */
    len *= sizeof(uint32_t);
    /*
     * IBI format is:
     * - 1-byte REMOTE_I3C_IBI request.
     * - 1-byte address of target sending the IBI.
     * - 1-byte RnW bit.
     * - 4-byte size of IBI payload.
     * - n-byte IBI payload.
     */
    size_t ibi_req_size = 7 + len;
    g_autofree uint8_t *ibi_req = g_new0(uint8_t, ibi_req_size);
    uint32_t len_le;
    uint8_t ibi_resp;

    ibi_req[0] = REMOTE_I3C_IBI;
    ibi_req[1] = TARGET_ADDR;
    ibi_req[2] = 0; /* RnW = 0 to make this a target interrupt request. */
    len_le = htole32(len);
    memcpy(&ibi_req[3], &len_le, sizeof(len_le));
    memcpy(&ibi_req[7], data, len);

    /* Send the request and read back the ACK. */
    g_assert(write(fd, ibi_req, ibi_req_size) == ibi_req_size);
    g_assert(read(fd, &ibi_resp, sizeof(ibi_resp)) == sizeof(ibi_resp));
    g_assert(ibi_resp == REMOTE_I3C_IBI_ACK);
}

static void aspeed_i3c_read_ibi_and_verify(uint32_t i3c_base,
                                           const uint32_t *data, size_t len)
{
    g_autofree uint32_t *ibi_data = g_new0(uint32_t, len * sizeof(uint32_t));

    /* Make sure there's actually something to read in the IBI queue. */
    uint8_t ibi_buf_lvl = FIELD_EX32(readl(i3c_base + A_QUEUE_STATUS_LEVEL),
                                     QUEUE_STATUS_LEVEL, IBI_BUF_BLR);
    /*
     * ibi_buf_level should have 1-byte for IBI status, plus data size in words.
     */
    g_assert(ibi_buf_lvl == 1 + len);
    uint32_t ibi_status = readl(i3c_base + A_IBI_QUEUE_STATUS);
    /* IBI_ID is target address << 1 | RnW bit (which is 0) */
    g_assert(FIELD_EX32(ibi_status, IBI_QUEUE_STATUS, IBI_ID) ==
             (TARGET_ADDR << 1));
    /* IBI data length in the register is stored in bytes. */
    uint32_t ibi_data_len = FIELD_EX32(ibi_status, IBI_QUEUE_STATUS,
                                       IBI_DATA_LEN);
    g_assert(ibi_data_len == len * sizeof(uint32_t));

    /*
     * Read in the IBI bytes, if they aren't word-aligned, read in an extra
     * word.
     */
    for (size_t i = 0; i < (ibi_data_len / 4) +
                           (ibi_data_len & 0x03) ? 1 : 0; i++) {
        ibi_data[i] = readl(i3c_base + A_IBI_QUEUE_DATA);
    }
    /* Make sure the data matches. */
    g_assert(memcmp(ibi_data, data, len) == 0);
}

static void ibi_and_verify(uint32_t i3c_base, const uint32_t *data, size_t len)
{
    /* Send the IBI request. */
    remote_i3c_ibi(data, len);
    /* Read it and verify it matches what we expect. */
    aspeed_i3c_read_ibi_and_verify(i3c_base, data, len);
}

/* Remote target IBI. */
static void test_remote_i3c_ibi(gconstpointer test_data)
{
    uint32_t controller_num = *(uint32_t *)test_data;
    uint32_t i3c_base = I3C(controller_num);
    uint32_t data = 0xaa55cc33;
    /* Enable the controller. */
    aspeed_i3c_enable(i3c_base);
    /*
     * Tell the target to use its static address as its dynamic address, and
     * populate the device table.
     */
    add_targets_to_bus(i3c_base);

    /*
     * To test IBIing, we will:
     * - Have the target IBI the controller by writing to the socket.
     * - The controller ACKs and enqueues the IBI request.
     * - The ACK is sent over socket, we verify it's there.
     * - We read the request from the controller IBI queue.
     */
    ibi_and_verify(i3c_base, &data, 1);
}

int main(int argc, char **argv)
{
    int ret;
    int port;
    /* Global register base address + offset of first controller. */
    uint32_t i3c_controller_num = 0;
    g_test_init(&argc, &argv, NULL);
    port = open_socket();

    global_qtest = qtest_initf("-machine ast2600-evb "
                "-chardev socket,id=remote-i3c-chr,port=%d,host=localhost "
                "-device %s,"
                "chardev=remote-i3c-chr,"
                "device-name=remote-target,"
                "bus=aspeed.i3c.device.0,"
                "pid=0xfeedf00dd00d,"
                "dcr=0xaa,"
                "bcr=0x55,"
                "static-address=%d",
                port, TYPE_REMOTE_I3C, TARGET_ADDR);
    setup_fd();

    /* Remote target RXing, i.e. controller -> target. */
    qtest_add_data_func("remote-i3c-rx", (void *)&i3c_controller_num,
                        test_remote_i3c_rx);
    /* Remote target TXing, i.e. controller -> target. */
    qtest_add_data_func("remote-i3c-tx", (void *)&i3c_controller_num,
                        test_remote_i3c_tx);
    /* Remote target IBIing. */
    qtest_add_data_func("remote-i3c-ibi", (void *)&i3c_controller_num,
                        test_remote_i3c_ibi);

    ret = g_test_run();
    qtest_end();

    return ret;
}
