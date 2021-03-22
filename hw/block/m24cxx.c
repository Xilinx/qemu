/*
 * ST m24Cxx I2C EEPROMs
 *
 * Copyright (c) 2012 Xilinx Inc.
 * Copyright (c) 2012 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/hw.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/block/m24cxx.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#ifndef M24CXX_DEBUG
#define M24CXX_DEBUG 0
#endif
#define DB_PRINT(fmt, args...) do {\
    if (M24CXX_DEBUG) {\
        fprintf(stderr, "M24CXX: %s:" fmt, __func__, ## args);\
    } \
} while (0);

const char *m24cxx_state_names[] = {
    [STOPPED] = "STOPPED",
    [ADDRESSING] = "ADDRESSING",
    [READING] = "READING",
    [WRITING] = "WRITING",
};

static void m24cxx_sync_complete(void *opaque, int ret)
{
    QEMUIOVector *iov = opaque;

    qemu_iovec_destroy(iov);
    g_free(iov);
    /* do nothing. Masters do not directly interact with the backing store,
     * only the working copy so no mutexing required.
     */
}

static inline bool m24cxx_uses_i2c_addr(M24CXXState *s)
{
    return (s->size >> 8) && !(s->size >> 11);
}

static void m24cxx_sync(I2CSlave *i2c)
{
    M24CXXState *s = M24CXX(i2c);
    int64_t nb_sectors;
    QEMUIOVector *iov;

    if (!s->blk) {
        return;
    }

    iov = g_new(QEMUIOVector, 1);
    nb_sectors = DIV_ROUND_UP(s->size, BDRV_SECTOR_SIZE);

    /* the device is so small, just sync the whole thing */
    qemu_iovec_init(iov, 1);
    qemu_iovec_add(iov, s->storage, nb_sectors * BDRV_SECTOR_SIZE);
    blk_aio_pwritev(s->blk, nb_sectors * BDRV_SECTOR_SIZE, iov, 0, m24cxx_sync_complete, iov);
}

static void m24cxx_reset(DeviceState *dev)
{
    M24CXXState *s = M24CXX(dev);

    m24cxx_sync(I2C_SLAVE(s));
    s->state = STOPPED;
    s->cur_addr = 0;
}

static uint8_t m24cxx_recv(I2CSlave *i2c)
{
    M24CXXState *s = M24CXX(i2c);
    int ret = 0;

    if (s->state == READING) {
        ret = s->storage[s->cur_addr++];
        DB_PRINT("storage %x <-> %x\n", s->cur_addr - 1, ret);
        s->cur_addr %= s->size;
    } else {
        /* should be impossible even with a degenerate guest */
        qemu_log_mask(LOG_GUEST_ERROR, "read from m24cxx not in read state");
    }
    DB_PRINT("data: %02x\n", ret);
    return ret;
}

static int m24cxx_send(I2CSlave *i2c, uint8_t data)
{
    M24CXXState *s = M24CXX(i2c);

    switch (s->state) {
    case (ADDRESSING):
        if (!s->addr_count) {
            s->cur_addr = 0;
        }
        s->cur_addr = deposit32(s->cur_addr,
                                (s->num_addr_bytes - s->addr_count - 1) * 8,
                                8, data);
        s->addr_count++;
        if (s->addr_count == s->num_addr_bytes) {
            s->state = WRITING;
            s->addr_count = 0;
        }
        return 0;
    case (WRITING):
        DB_PRINT("storage %x <-> %x\n", s->cur_addr, data);
        s->storage[s->cur_addr++] = data;
        s->cur_addr %= s->size;
        return 0;
    default:
        DB_PRINT("write to m24cxx not in writable state\n");
        qemu_log_mask(LOG_GUEST_ERROR, "write to m24cxx not in writable state");
        return 1;
    }
}

static int m24cxx_event(I2CSlave *i2c, enum i2c_event event)
{
    M24CXXState *s = M24CXX(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->state = ADDRESSING;
        break;
    case I2C_START_RECV:
        s->state = READING;
        break;
    case I2C_FINISH:
        m24cxx_sync(i2c);
        s->state = STOPPED;
        break;
    case I2C_NACK:
        DB_PRINT("NACKED\n");
        break;
    }

    DB_PRINT("transitioning to state %s\n", m24cxx_state_names[s->state]);

    return 0;
}

static int m24cxx_decode_address(I2CSlave *i2c, uint8_t address)
{
    M24CXXState *s = M24CXX(i2c);

    if (m24cxx_uses_i2c_addr(s)) {
        s->cur_addr &= ~(0x0700);
        deposit32(s->cur_addr, 0, 3, ((s->size >> 8) - 1) & address);
    }
    return 0;
}

static void m24cxx_realize(DeviceState *dev, Error **errp)
{
    M24CXXState *s = M24CXX(dev);
    I2CSlave *i2c = I2C_SLAVE(dev);

    i2c->address_range = m24cxx_uses_i2c_addr(s) ? s->size >> 8 : 1;
    s->num_addr_bytes = s->size >> 11 ? 2 : 1;
    s->storage = g_new0(uint8_t, DIV_ROUND_UP(s->size, BDRV_SECTOR_SIZE) *
                                              BDRV_SECTOR_SIZE);

    if (s->blk) {
        /* FIXME: Move to late init */
        if (blk_pread(s->blk, 0, s->storage,
                      s->size) < 0) {
            error_setg(errp, "Failed to initialize I2C EEPROM!\n");
            return;
        }
    } else {
        memset(s->storage, 0xFF, s->size);
    }
}

static int m24cxx_pre_save(void *opaque)
{
    m24cxx_sync((I2CSlave *)opaque);

    return 0;
}

static const VMStateDescription vmstate_m24cxx = {
    .name = "m24cxx",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = m24cxx_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(i2c, M24CXXState),
        VMSTATE_UINT8(state, M24CXXState),
        VMSTATE_UINT16(cur_addr, M24CXXState),
        VMSTATE_END_OF_LIST()
    }
};

static Property m24cxx_properties[] = {
    DEFINE_PROP_UINT16("size", M24CXXState, size, 1024),
    DEFINE_PROP_DRIVE("drive", M24CXXState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void m24cxx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = m24cxx_event;
    k->recv = m24cxx_recv;
    k->send = m24cxx_send;
    k->decode_address = m24cxx_decode_address;

    dc->realize = m24cxx_realize;
    dc->reset = m24cxx_reset;
    dc->vmsd = &vmstate_m24cxx;
    device_class_set_props(dc, m24cxx_properties);
}

static TypeInfo m24cxx_info = {
    .name          = TYPE_M24CXX,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(M24CXXState),
    .class_init    = m24cxx_class_init,
};

static const TypeInfo m24cxx_qom_aliases[] = {
    {   .name = "at.24c08",                 .parent = TYPE_M24CXX           },
    {   .name = "at.24c16",                 .parent = TYPE_M24CXX           },
    {   .name = "at.24c32",                 .parent = TYPE_M24CXX           },
    {   .name = "at.24c64",                 .parent = TYPE_M24CXX           },
};

static void m24cxx_register_types(void)
{
    int i;

    type_register_static(&m24cxx_info);
    for (i = 0; i < ARRAY_SIZE(m24cxx_qom_aliases); ++i) {
        type_register_static(&m24cxx_qom_aliases[i]);
    }
}

type_init(m24cxx_register_types)
