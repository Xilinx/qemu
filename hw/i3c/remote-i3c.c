/*
 * Remote I3C Device
 *
 * Copyright (c) 2023 Google LLC
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
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/fifo8.h"
#include "chardev/char-fe.h"
#include "trace.h"
#include "hw/i3c/i3c.h"
#include "hw/i3c/remote-i3c.h"
#include "hw/qdev-properties-system.h"

typedef enum {
    IBI_RX_STATE_DONE = 0,
    IBI_RX_STATE_READ_ADDR = 1,
    IBI_RX_STATE_READ_RNW = 2,
    IBI_RX_STATE_READ_SIZE = 3,
    IBI_RX_STATE_READ_DATA = 4,
} IBIRXState;

typedef struct {
    uint8_t addr;
    bool is_recv;
    uint32_t num_bytes;
    uint8_t *data;
} IBIData;

typedef struct {
    I3CTarget parent_obj;
    CharBackend chr;
    /* For ease of debugging. */

    struct {
        char *name;
        uint32_t buf_size;
    } cfg;

    /* Intermediate buffer to store IBI data received over socket. */
    IBIData ibi_data;
    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
    uint8_t current_cmd;
    IBIRXState ibi_rx_state;
    /*
     * To keep track of where we are in reading in data that's longer than
     * 1-byte.
     */
    uint32_t ibi_bytes_rxed;
} RemoteI3C;

static uint32_t remote_i3c_recv(I3CTarget *t, uint8_t *data,
                                uint32_t num_to_read)
{
    RemoteI3C *i3c = REMOTE_I3C(t);
    uint8_t type = REMOTE_I3C_RECV;
    uint32_t num_read;

    qemu_chr_fe_write_all(&i3c->chr, &type, 1);
    uint32_t num_to_read_le = cpu_to_le32(num_to_read);
    qemu_chr_fe_write_all(&i3c->chr, (uint8_t *)&num_to_read_le,
                          sizeof(num_to_read_le));
    /*
     * The response will first contain the size of the packet, as a LE uint32.
     */
    qemu_chr_fe_read_all(&i3c->chr, (uint8_t *)&num_read, sizeof(num_read));

    num_read = le32_to_cpu(num_read);
    qemu_chr_fe_read_all(&i3c->chr, data, num_read);
    trace_remote_i3c_recv(i3c->cfg.name, num_read, num_to_read);
    return num_read;
}

static inline bool remote_i3c_tx_in_progress(RemoteI3C *i3c)
{
    return !fifo8_is_empty(&i3c->tx_fifo);
}

static int remote_i3c_chr_send_bytes(RemoteI3C *i3c)
{
    uint32_t i;
    uint32_t num_bytes = fifo8_num_used(&i3c->tx_fifo);
    g_autofree uint8_t *buf = g_new0(uint8_t, num_bytes);

    qemu_chr_fe_write_all(&i3c->chr, &i3c->current_cmd,
                          sizeof(i3c->current_cmd));

    /* The FIFO data is stored in a ring buffer, move it into a linear one. */
    for (i = 0; i < num_bytes; i++) {
        buf[i] = fifo8_pop(&i3c->tx_fifo);
    }

    uint32_t num_bytes_le = cpu_to_le32(num_bytes);
    qemu_chr_fe_write_all(&i3c->chr, (uint8_t *)&num_bytes_le, 4);
    qemu_chr_fe_write_all(&i3c->chr, buf, num_bytes);
    trace_remote_i3c_send(i3c->cfg.name, num_bytes, i3c->current_cmd ==
                                                   REMOTE_I3C_HANDLE_CCC_WRITE);

    return 0;
}

static bool remote_i3c_tx_fifo_push(RemoteI3C *i3c, const uint8_t *data,
                                    uint32_t num_to_send, uint32_t *num_sent)
{
    uint32_t num_to_push = num_to_send;
    bool ack = true;

    /*
     * For performance reasons, we buffer data being sent from the controller to
     * us.
     * If this FIFO has data in it, we transmit it when we receive an I3C
     * STOP or START.
     */
    if (fifo8_num_free(&i3c->tx_fifo) < num_to_send) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: TX FIFO buffer full.\n",
                      object_get_canonical_path(OBJECT(i3c)), i3c->cfg.name);
        num_to_push = fifo8_num_free(&i3c->tx_fifo);
        ack = false;
    }

    *num_sent = num_to_push;
    for (uint32_t i = 0; i < num_to_push; i++) {
        fifo8_push(&i3c->tx_fifo, data[i]);
    }

    return ack;
}

static int remote_i3c_send(I3CTarget *t, const uint8_t *data,
                           uint32_t num_to_send, uint32_t *num_sent)
{
    RemoteI3C *i3c = REMOTE_I3C(t);
    i3c->current_cmd = REMOTE_I3C_SEND;
    if (!remote_i3c_tx_fifo_push(i3c, data, num_to_send, num_sent)) {
        return -1;
    }

    return 0;
}

static int remote_i3c_handle_ccc_read(I3CTarget *t, uint8_t *data,
                                      uint32_t num_to_read, uint32_t *num_read)
{
    RemoteI3C *i3c = REMOTE_I3C(t);
    uint8_t type = REMOTE_I3C_HANDLE_CCC_READ;

    qemu_chr_fe_write_all(&i3c->chr, &type, 1);
    /*
     * The response will first contain the size of the packet, as a LE uint32.
     */
    qemu_chr_fe_read_all(&i3c->chr, (uint8_t *)num_read, 4);
    *num_read = le32_to_cpu(*num_read);
    qemu_chr_fe_read_all(&i3c->chr, data, *num_read);
    trace_remote_i3c_ccc_read(i3c->cfg.name, *num_read, num_to_read);

    return 0;
}

static int remote_i3c_handle_ccc_write(I3CTarget *t, const uint8_t *data,
                                       uint32_t num_to_send, uint32_t *num_sent)
{
    RemoteI3C *i3c = REMOTE_I3C(t);
    i3c->current_cmd = REMOTE_I3C_HANDLE_CCC_WRITE;
    if (!remote_i3c_tx_fifo_push(i3c, data, num_to_send, num_sent)) {
        return -1;
    }

    return 0;
}

static int remote_i3c_event(I3CTarget *t, enum I3CEvent event)
{
    RemoteI3C *i3c = REMOTE_I3C(t);
    uint8_t type;
    trace_remote_i3c_event(i3c->cfg.name, event);
    switch (event) {
    case I3C_START_RECV:
        type = REMOTE_I3C_START_RECV;
        break;
    case I3C_START_SEND:
        type = REMOTE_I3C_START_SEND;
        break;
    case I3C_STOP:
        type = REMOTE_I3C_STOP;
        break;
    case I3C_NACK:
        type = REMOTE_I3C_NACK;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: Unknown I3C event %d\n",
                      object_get_canonical_path(OBJECT(i3c)), i3c->cfg.name,
                                                event);
        return -1;
    }

    /*
     * If we have a transfer buffered, send it out before we tell the remote
     * target about the next event.
     */
    if (remote_i3c_tx_in_progress(i3c)) {
        remote_i3c_chr_send_bytes(i3c);
    }

    qemu_chr_fe_write_all(&i3c->chr, &type, 1);
    return 0;
}

static void remote_i3c_chr_event(void *opaque, QEMUChrEvent evt)
{
    switch (evt) {
    case CHR_EVENT_OPENED:
    case CHR_EVENT_CLOSED:
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /*
         * Ignore events.
         * Our behavior stays the same regardless of what happens.
         */
        break;
    default:
        g_assert_not_reached();
    }
}

static void remote_i3c_rx_ibi(RemoteI3C *i3c, const uint8_t *buf, int size)
{
    uint32_t p_buf = 0;
    while (p_buf < size) {
        switch (i3c->ibi_rx_state) {
        /* This is the start of a new IBI request. */
        case IBI_RX_STATE_DONE:
            i3c->ibi_rx_state = IBI_RX_STATE_READ_ADDR;
            p_buf++;
            break;
        case IBI_RX_STATE_READ_ADDR:
            i3c->ibi_data.addr = buf[p_buf];
            p_buf++;
            i3c->ibi_rx_state = IBI_RX_STATE_READ_RNW;
            break;
        case IBI_RX_STATE_READ_RNW:
            i3c->ibi_data.is_recv = buf[p_buf];
            p_buf++;
            i3c->ibi_rx_state = IBI_RX_STATE_READ_SIZE;
            break;
        case IBI_RX_STATE_READ_SIZE:
            i3c->ibi_data.num_bytes |= ((uint32_t)buf[p_buf] <<
                                        (8 * i3c->ibi_bytes_rxed));
            i3c->ibi_bytes_rxed++;
            p_buf++;
            /*
             * If we're done reading the num_bytes portion, move on to reading
             * data.
             */
            if (i3c->ibi_bytes_rxed == sizeof(i3c->ibi_data.num_bytes)) {
                i3c->ibi_data.num_bytes = le32_to_cpu(i3c->ibi_data.num_bytes);
                i3c->ibi_bytes_rxed = 0;
                i3c->ibi_rx_state = IBI_RX_STATE_READ_DATA;
                /* If there's no data to read, we're done. */
                if (i3c->ibi_data.num_bytes == 0) {
                    /*
                     * Sanity check to see if the remote target is doing
                     * something wonky. This would only happen if it sends
                     * another IBI before the first one has been ACKed/NACKed
                     * by the controller.
                     * We'll try to recover by just exiting early and discarding
                     * the leftover bytes.
                     */
                    if (p_buf < size) {
                        qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: Remote target "
                                      "sent trailing bytes at the end of the "
                                      "IBI request.",
                            object_get_canonical_path(OBJECT(i3c)),
                                                      i3c->cfg.name);
                        return;
                    }
                    i3c->ibi_rx_state = IBI_RX_STATE_DONE;
                } else {
                    /*
                     * We have IBI bytes to read, allocate memory for it.
                     * Freed when we're done sending the IBI to the controller.
                     */
                    i3c->ibi_data.data = g_new0(uint8_t,
                                                i3c->ibi_data.num_bytes);
                }
            }
            break;
        case IBI_RX_STATE_READ_DATA:
            i3c->ibi_data.data[i3c->ibi_bytes_rxed] = buf[p_buf];
            i3c->ibi_bytes_rxed++;
            p_buf++;
            if (i3c->ibi_bytes_rxed == i3c->ibi_data.num_bytes) {
                /*
                 * Sanity check to see if the remote target is doing something
                 * wonky. This would only happen if it sends another IBI before
                 * the first one has been ACKed/NACKed by the controller.
                 * We'll try to recover by just exiting early and discarding the
                 * leftover bytes.
                 */
                if (p_buf < size) {
                    qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: Remote target "
                                  "sent trailing bytes at the end of the "
                                  "IBI request.",
                        object_get_canonical_path(OBJECT(i3c)), i3c->cfg.name);
                    return;
                }
                i3c->ibi_rx_state = IBI_RX_STATE_DONE;
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: Remote target IBI state "
                          "machine reached unknown state 0x%x\n",
                          object_get_canonical_path(OBJECT(i3c)), i3c->cfg.name,
                          i3c->ibi_rx_state);
            g_assert_not_reached();
        }
    }
}

static void remote_i3c_ibi_rx_state_reset(RemoteI3C *i3c)
{
    if (i3c->ibi_data.num_bytes) {
        free(i3c->ibi_data.data);
    }
    i3c->ibi_data.addr = 0;
    i3c->ibi_data.is_recv = 0;
    i3c->ibi_data.num_bytes = 0;
    i3c->ibi_bytes_rxed = 0;
    i3c->ibi_rx_state = IBI_RX_STATE_DONE;
}

static void remote_i3c_do_ibi(RemoteI3C *i3c)
{
    uint32_t i;
    uint8_t resp = REMOTE_I3C_IBI_ACK;

    trace_remote_i3c_do_ibi(i3c->cfg.name, i3c->ibi_data.addr,
                            i3c->ibi_data.is_recv);
    if (i3c_target_send_ibi(&i3c->parent_obj, i3c->ibi_data.addr,
                        i3c->ibi_data.is_recv)) {
        resp = REMOTE_I3C_IBI_NACK;
    } else {
        for (i = 0; i < i3c->ibi_data.num_bytes; i++) {
            if (i3c_target_send_ibi_bytes(&i3c->parent_obj,
                                          i3c->ibi_data.data[i])) {
                resp = REMOTE_I3C_IBI_DATA_NACK;
                break;
            }
        }
    }

    if (i3c_target_ibi_finish(&i3c->parent_obj, 0x00)) {
        resp = REMOTE_I3C_IBI_NACK;
    }
    qemu_chr_fe_write_all(&i3c->chr, &resp, sizeof(resp));
    remote_i3c_ibi_rx_state_reset(i3c);
}

static int remote_i3c_chr_can_receive(void *opaque)
{
    return 1;
}

static void remote_i3c_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    RemoteI3C *i3c = REMOTE_I3C(opaque);

    /*
     * The only things we expect to receive unprompted are:
     * - An ACK of a previous transfer
     * - A NACK of a previous transfer
     * - An IBI requested by the remote target.
     * - Bytes for an IBI request.
     */
    /* If we're in the middle of handling an IBI request, parse it here. */
    if (i3c->ibi_rx_state != IBI_RX_STATE_DONE) {
        remote_i3c_rx_ibi(i3c, buf, size);
        /* If we finished reading the IBI, do it. */
        if (i3c->ibi_rx_state == IBI_RX_STATE_DONE) {
            remote_i3c_do_ibi(i3c);
         }
         return;
    }

    switch (buf[0]) {
    case REMOTE_I3C_RX_ACK:
        break;
    case REMOTE_I3C_RX_NACK:
        qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: Received NACK from remote "
                      "target\n", object_get_canonical_path(OBJECT(i3c)),
                      i3c->cfg.name);
        break;
    case REMOTE_I3C_IBI:
        remote_i3c_rx_ibi(i3c, buf, size);
        /* If we finished reading the IBI, do it. */
        if (i3c->ibi_rx_state == IBI_RX_STATE_DONE) {
            remote_i3c_do_ibi(i3c);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s-%s: Unknown response 0x%x\n",
                      object_get_canonical_path(OBJECT(i3c)), i3c->cfg.name,
                      buf[0]);
        break;
    }
}

static void remote_i3c_realize(DeviceState *dev, Error **errp)
{
    RemoteI3C *i3c = REMOTE_I3C(dev);

    fifo8_create(&i3c->tx_fifo, i3c->cfg.buf_size);
    fifo8_create(&i3c->rx_fifo, i3c->cfg.buf_size);
    i3c->ibi_data.data = g_new0(uint8_t, i3c->cfg.buf_size);
    remote_i3c_ibi_rx_state_reset(i3c);

    qemu_chr_fe_set_handlers(&i3c->chr, remote_i3c_chr_can_receive,
                             remote_i3c_chr_receive, remote_i3c_chr_event,
                             NULL, OBJECT(i3c), NULL, true);
}

static Property remote_i3c_props[] = {
    DEFINE_PROP_CHR("chardev", RemoteI3C, chr),
    DEFINE_PROP_UINT32("buf-size", RemoteI3C, cfg.buf_size, 0x10000),
    DEFINE_PROP_STRING("device-name", RemoteI3C, cfg.name),
    DEFINE_PROP_END_OF_LIST(),
};

static void remote_i3c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I3CTargetClass *k = I3C_TARGET_CLASS(klass);

    k->recv = &remote_i3c_recv;
    k->send = &remote_i3c_send;
    k->event = &remote_i3c_event;
    k->handle_ccc_read = &remote_i3c_handle_ccc_read;
    k->handle_ccc_write = &remote_i3c_handle_ccc_write;
    device_class_set_props(dc, remote_i3c_props);
    dc->realize = remote_i3c_realize;
}

static const TypeInfo remote_i3c_type = {
    .name = TYPE_REMOTE_I3C,
    .parent = TYPE_I3C_TARGET,
    .instance_size = sizeof(RemoteI3C),
    .class_size = sizeof(I3CTargetClass),
    .class_init = remote_i3c_class_init,
};

static void remote_i3c_register(void)
{
    type_register_static(&remote_i3c_type);
}

type_init(remote_i3c_register)
