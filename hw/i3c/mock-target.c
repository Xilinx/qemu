/*
 * Mock I3C Device
 *
 * Copyright (c) 2023 Google LLC
 *
 * The mock I3C device can be thought of as a simple EEPROM. It has a buffer,
 * and the pointer in the buffer is reset to 0 on an I3C STOP.
 * To write to the buffer, issue a private write and send data.
 * To read from the buffer, issue a private read.
 *
 * The mock target also supports sending target interrupt IBIs.
 * To issue an IBI, set the 'ibi-magic-num' property to a non-zero number, and
 * send that number in a private transaction. The mock target will issue an IBI
 * after 1 second.
 *
 * It also supports a handful of CCCs that are typically used when probing I3C
 * devices.
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
#include "qemu/log.h"
#include "trace.h"
#include "hw/i3c/i3c.h"
#include "hw/i3c/mock-target.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"

#ifndef MOCK_TARGET_DEBUG
#define MOCK_TARGET_DEBUG 0
#endif

#define DB_PRINTF(...) do { \
        if (MOCK_TARGET_DEBUG) { \
            qemu_log("%s: ", __func__); \
            qemu_log(__VA_ARGS__); \
        } \
    } while (0)

#define IBI_DELAY_NS (1 * 1000 * 1000)

static uint32_t mock_target_rx(I3CTarget *i3c, uint8_t *data,
                               uint32_t num_to_read)
{
    MockTargetState *s = MOCK_TARGET(i3c);
    uint32_t i;

    /* Bounds check. */
    if (s->p_buf == s->cfg.buf_size) {
        return 0;
    }

    for (i = 0; i < num_to_read; i++) {
        data[i] = s->buf[s->p_buf];
        trace_mock_target_rx(data[i]);
        s->p_buf++;
        if (s->p_buf == s->cfg.buf_size) {
            break;
        }
    }

    /* Return the number of bytes we're sending to the controller. */
    return i;
}

static void mock_target_ibi_timer_start(MockTargetState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod(&s->qtimer, now + IBI_DELAY_NS);
}

static int mock_target_tx(I3CTarget *i3c, const uint8_t *data,
                          uint32_t num_to_send, uint32_t *num_sent)
{
    MockTargetState *s = MOCK_TARGET(i3c);
    int ret;
    uint32_t to_write;

    if (s->cfg.ibi_magic && num_to_send == 1 && s->cfg.ibi_magic == *data) {
        mock_target_ibi_timer_start(s);
        return 0;
    }

    /* Bounds check. */
    if (num_to_send + s->p_buf > s->cfg.buf_size) {
        to_write = s->cfg.buf_size - s->p_buf;
        ret = -1;
    } else {
        to_write = num_to_send;
        ret = 0;
    }
    for (uint32_t i = 0; i < to_write; i++) {
        trace_mock_target_tx(data[i]);
        s->buf[s->p_buf] = data[i];
        s->p_buf++;
    }
    return ret;
}

static int mock_target_event(I3CTarget *i3c, enum I3CEvent event)
{
    MockTargetState *s = MOCK_TARGET(i3c);

    trace_mock_target_event(event);
    if (event == I3C_STOP) {
        s->in_ccc = false;
        s->curr_ccc = 0;
        s->ccc_byte_offset = 0;
        s->p_buf = 0;
    }

    return 0;
}

static int mock_target_handle_ccc_read(I3CTarget *i3c, uint8_t *data,
                                       uint32_t num_to_read, uint32_t *num_read)
{
    MockTargetState *s = MOCK_TARGET(i3c);

    switch (s->curr_ccc) {
    case I3C_CCCD_GETMXDS:
        /* Default data rate for I3C. */
        while (s->ccc_byte_offset < num_to_read) {
            if (s->ccc_byte_offset >= 2) {
                break;
            }
            data[s->ccc_byte_offset] = 0;
            *num_read = s->ccc_byte_offset;
            s->ccc_byte_offset++;
        }
        break;
    case I3C_CCCD_GETCAPS:
        /* Support I3C version 1.1.x, no other features. */
        while (s->ccc_byte_offset < num_to_read) {
            if (s->ccc_byte_offset >= 2) {
                break;
            }
            if (s->ccc_byte_offset == 0) {
                data[s->ccc_byte_offset] = 0;
            } else {
                data[s->ccc_byte_offset] = 0x01;
            }
            *num_read = s->ccc_byte_offset;
            s->ccc_byte_offset++;
        }
        break;
    case I3C_CCCD_GETMWL:
    case I3C_CCCD_GETMRL:
        /* MWL/MRL is MSB first. */
        while (s->ccc_byte_offset < num_to_read) {
            if (s->ccc_byte_offset >= 2) {
                break;
            }
            data[s->ccc_byte_offset] = (s->cfg.buf_size &
                                        (0xff00 >> (s->ccc_byte_offset * 8))) >>
                                        (8 - (s->ccc_byte_offset * 8));
            s->ccc_byte_offset++;
            *num_read = num_to_read;
        }
        break;
    case I3C_CCC_ENTDAA:
    case I3C_CCCD_GETPID:
    case I3C_CCCD_GETBCR:
    case I3C_CCCD_GETDCR:
        /* Nothing to do. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Unhandled CCC 0x%.2x\n", s->curr_ccc);
        return -1;
    }

    trace_mock_target_handle_ccc_read(*num_read, num_to_read);
    return 0;
}

static int mock_target_handle_ccc_write(I3CTarget *i3c, const uint8_t *data,
                                        uint32_t num_to_send,
                                        uint32_t *num_sent)
{
    MockTargetState *s = MOCK_TARGET(i3c);

    if (!s->curr_ccc) {
        s->in_ccc = true;
        s->curr_ccc = *data;
        trace_mock_target_new_ccc(s->curr_ccc);
    }

    *num_sent = 1;
    switch (s->curr_ccc) {
    case I3C_CCC_ENEC:
        s->can_ibi = true;
        break;
    case I3C_CCC_DISEC:
        s->can_ibi = false;
        break;
    case I3C_CCC_ENTDAA:
    case I3C_CCC_SETAASA:
    case I3C_CCC_RSTDAA:
    case I3C_CCCD_SETDASA:
    case I3C_CCCD_GETPID:
    case I3C_CCCD_GETBCR:
    case I3C_CCCD_GETDCR:
    case I3C_CCCD_GETMWL:
    case I3C_CCCD_GETMRL:
    case I3C_CCCD_GETMXDS:
    case I3C_CCCD_GETCAPS:
        /* Nothing to do. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Unhandled CCC 0x%.2x\n", s->curr_ccc);
        return -1;
    }

    trace_mock_target_handle_ccc_read(*num_sent, num_to_send);
    return 0;
}

static void mock_target_do_ibi(MockTargetState *s)
{
    if (!s->can_ibi) {
        DB_PRINTF("IBIs disabled by controller");
        return;
    }

    trace_mock_target_do_ibi(s->i3c.address, true);
    int nack = i3c_target_send_ibi(&s->i3c, s->i3c.address, /*is_recv=*/true);
    /* Getting NACKed isn't necessarily an error, just print it out. */
    if (nack) {
        DB_PRINTF("NACKed from controller when sending target interrupt.\n");
    }
}

static void mock_target_timer_elapsed(void *opaque)
{
    MockTargetState *s = MOCK_TARGET(opaque);
    timer_del(&s->qtimer);
    mock_target_do_ibi(s);
}

static void mock_target_reset(I3CTarget *i3c)
{
    MockTargetState *s = MOCK_TARGET(i3c);
    s->can_ibi = false;
}

static void mock_target_realize(DeviceState *dev, Error **errp)
{
    MockTargetState *s = MOCK_TARGET(dev);
    s->buf = g_new0(uint8_t, s->cfg.buf_size);
    mock_target_reset(&s->i3c);
}

static void mock_target_init(Object *obj)
{
    MockTargetState *s = MOCK_TARGET(obj);
    s->can_ibi = false;

    /* For IBIs. */
    timer_init_ns(&s->qtimer, QEMU_CLOCK_VIRTUAL, mock_target_timer_elapsed, s);
}

static Property remote_i3c_props[] = {
    /* The size of the internal buffer. */
    DEFINE_PROP_UINT32("buf-size", MockTargetState, cfg.buf_size, 0x100),
    /*
     * If the mock target receives this number, it will issue an IBI after
     * 1 second. Disabled if the IBI magic number is 0.
     */
    DEFINE_PROP_UINT8("ibi-magic-num", MockTargetState, cfg.ibi_magic, 0x00),
    DEFINE_PROP_END_OF_LIST(),
};

static void mock_target_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I3CTargetClass *k = I3C_TARGET_CLASS(klass);

    dc->realize = mock_target_realize;
    k->event = mock_target_event;
    k->recv = mock_target_rx;
    k->send = mock_target_tx;
    k->handle_ccc_read = mock_target_handle_ccc_read;
    k->handle_ccc_write = mock_target_handle_ccc_write;

    device_class_set_props(dc, remote_i3c_props);
}

static const TypeInfo mock_target_info = {
    .name          = TYPE_MOCK_TARGET,
    .parent        = TYPE_I3C_TARGET,
    .instance_size = sizeof(MockTargetState),
    .instance_init = mock_target_init,
    .class_init    = mock_target_class_init,
};

static void mock_target_register_types(void)
{
    type_register_static(&mock_target_info);
}

type_init(mock_target_register_types)
