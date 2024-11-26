/*
 * Remote I3C Socket for a single device
 *
 * Copyright (c) 2023 Google LLC
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
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

#define TYPE_I3C_SOCKET "i3c-socket"
#define I3C_SOC(obj) OBJECT_CHECK(I3CSoc, (obj), TYPE_I3C_SOCKET)

/*
 * This implementation only cares about one i3c slave
 * as the master will be communicating with one
 * remote-i3c device only.
 */
typedef struct {
    DeviceState parent;
    CharBackend chr;

    Fifo8 ibi_fifo;
    uint32_t in_event;
    uint32_t data_size;
    uint8_t slave_da;
    I3CBus *bus;
} I3CSoc;


typedef enum {
    I3C_SOC_RECV_STAGE2 = 0x1001,

    I3C_SOC_SEND_STAGE2 = 0x1003,
    I3C_SOC_SEND_STAGE3 = 0x1004,

    I3C_SOC_HANDLE_CCC_READ_STAGE2 = 0x1005,

    I3C_SOC_HANDLE_CCC_WRITE_STAGE2 = 0x1007,
    I3C_SOC_HANDLE_CCC_WRITE_STAGE3 = 0x1008,
} I3CSocState;

static int i3c_soc_chr_can_receive(void *opaque)
{
    I3CSoc *s = I3C_SOC(opaque);

    return s->in_event ? s->data_size : 1;
}


static void i3c_soc_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    I3CSoc *s = I3C_SOC(opaque);
    uint32_t ret;
    uint8_t ReadBuf[128] = { 0 };

    if (!s->in_event) {
        s->in_event = buf[0];
        if (s->in_event == REMOTE_I3C_STOP) {
            i3c_end_transfer(s->bus);
            s->in_event = 0;
            return;
        }
    }

    switch (s->in_event) {
    case I3C_SOC_SEND_STAGE2:
    case I3C_SOC_HANDLE_CCC_WRITE_STAGE2:
        memcpy(&s->data_size, buf, sizeof(uint32_t));
        s->data_size = le32_to_cpu(s->data_size);
        s->in_event += 1;
        break;

    case REMOTE_I3C_START_RECV:
        if (i3c_bus_busy(s->bus)) {
            /* repeated start */
            i3c_start_recv(s->bus, s->slave_da);
        }
        s->in_event = 0;
        break;
    case REMOTE_I3C_RECV:
       if (!i3c_bus_busy(s->bus)) {
            i3c_start_recv(s->bus, s->slave_da);
        }
        s->data_size = 4;
        s->in_event = I3C_SOC_RECV_STAGE2;
        break;
    case I3C_SOC_RECV_STAGE2:
    case I3C_SOC_HANDLE_CCC_READ_STAGE2:
        memcpy(&s->data_size, buf, sizeof(uint32_t));
        s->data_size = le32_to_cpu(s->data_size);
        i3c_recv(s->bus, ReadBuf, s->data_size, &ret);
        ret = cpu_to_le32(ret);
        qemu_chr_fe_write_all(&s->chr, (uint8_t *) &ret, 4);
        qemu_chr_fe_write_all(&s->chr, ReadBuf, le32_to_cpu(ret));
        s->in_event = 0;
        break;

    case REMOTE_I3C_START_CCC_READ:
        if (i3c_bus_busy(s->bus)) {
            /* repeated start */
            i3c_start_recv(s->bus,
                    CCC_IS_DIRECT(s->bus->ccc) ? s->slave_da : 0x7e);
        }
        s->in_event = 0;
        break;
    case REMOTE_I3C_HANDLE_CCC_READ:
       if (!i3c_bus_busy(s->bus)) {
            i3c_start_recv(s->bus, 0x7e);
        }
        s->data_size = 4;
        s->in_event = I3C_SOC_HANDLE_CCC_READ_STAGE2;
        break;


    case REMOTE_I3C_START_SEND:
        if (i3c_bus_busy(s->bus)) {
            /* repeated start */
            i3c_start_send(s->bus, s->slave_da);
        }
        s->in_event = 0;
        break;
    case REMOTE_I3C_SEND:
       if (!i3c_bus_busy(s->bus)) {
            i3c_start_send(s->bus, s->slave_da);
        }
        s->data_size = 4;
        s->in_event = I3C_SOC_SEND_STAGE2;
        break;
    case I3C_SOC_SEND_STAGE3:
    case I3C_SOC_HANDLE_CCC_WRITE_STAGE3:
        if (s->bus->in_entdaa) {
            s->slave_da = buf[0];
        }
        i3c_send(s->bus, buf, s->data_size, &ret);
        s->in_event = 0;
        break;

    case REMOTE_I3C_START_CCC_WRITE:
        if (i3c_bus_busy(s->bus)) {
            /* repeated start */
            i3c_start_send(s->bus,
                    CCC_IS_DIRECT(s->bus->ccc) ? s->slave_da : 0x7e);
        }
        s->in_event = 0;
        break;
    case REMOTE_I3C_HANDLE_CCC_WRITE:
       if (!i3c_bus_busy(s->bus) &&
           !s->bus->in_entdaa) {
            i3c_start_send(s->bus, 0x7e);
        }
        s->data_size = 4;
        s->in_event = I3C_SOC_HANDLE_CCC_WRITE_STAGE2;
        break;
    default:
        break;
    }
}

static int i3c_soc_ibi_handle(I3CBus *bus, I3CTarget *target,
                              uint8_t addr, bool is_recv)
{
    I3CSoc *s = I3C_SOC(bus->qbus.parent);
    int ret = -1;
    uint8_t tmp;

    if ((addr == I3C_HJ_ADDR) ||
        (addr == target->address && !is_recv)) {
        /*
         *  Not handling HJ, MR
         */
        ret = -1;
    } else if (addr == target->address && is_recv) {
        tmp = REMOTE_I3C_IBI;
        qemu_chr_fe_write_all(&s->chr, &tmp, 1);
        qemu_chr_fe_write_all(&s->chr, &target->address, 1);
        tmp = is_recv ? 1 : 0;
        qemu_chr_fe_write_all(&s->chr, &tmp, 1);
        ret = 0;
    }
    return ret;
}

static int i3c_soc_ibi_recv(I3CBus *bus, uint8_t data)
{
    I3CSoc *s = I3C_SOC(bus->qbus.parent);

    fifo8_push(&s->ibi_fifo, data);
    return 0;
}

static int i3c_soc_ibi_finish(I3CBus *bus)
{
    I3CSoc *s = I3C_SOC(bus->qbus.parent);
    uint32_t size, pop_size;
    const uint8_t *buf;
    uint8_t resp;

    size = fifo8_num_used(&s->ibi_fifo);
    size = cpu_to_le32(size);
    qemu_chr_fe_write_all(&s->chr, (uint8_t *)&size, 4);
    if (size > 0) {
        buf = fifo8_pop_buf(&s->ibi_fifo, size, &pop_size);
        if (pop_size) {
            qemu_chr_fe_write_all(&s->chr, buf, pop_size);
        }
    }
    qemu_chr_fe_read_all(&s->chr, &resp, 1);
    return (resp == REMOTE_I3C_IBI_ACK ? 0 : -1);
}

static void i3c_soc_realize(DeviceState *dev, Error **errp)
{
    I3CSoc *s = I3C_SOC(dev);

    s->bus = i3c_init_bus(dev, dev->id);
    s->bus->ibi_handle = i3c_soc_ibi_handle;
    s->bus->ibi_recv = i3c_soc_ibi_recv;
    s->bus->ibi_finish = i3c_soc_ibi_finish;

    qemu_chr_fe_set_handlers(&s->chr, i3c_soc_chr_can_receive,
                             i3c_soc_chr_receive, NULL,
                             NULL, OBJECT(s), NULL, true);
    fifo8_create(&s->ibi_fifo, 64);
}

static void i3c_soc_unrealize(DeviceState *dev)
{
    I3CSoc *s = I3C_SOC(dev);

    fifo8_destroy(&s->ibi_fifo);
}

static Property i3c_soc_props[] = {
    DEFINE_PROP_CHR("chardev", I3CSoc, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void i3c_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, i3c_soc_props);
    dc->realize = i3c_soc_realize;
    dc->unrealize = i3c_soc_unrealize;
}

static const TypeInfo remote_i3c_type = {
    .name = TYPE_I3C_SOCKET,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(I3CSoc),
    .class_init = i3c_soc_class_init,
};

static void remote_i3c_register(void)
{
    type_register_static(&remote_i3c_type);
}

type_init(remote_i3c_register)
