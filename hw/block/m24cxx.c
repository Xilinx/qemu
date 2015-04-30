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

#include "hw/i2c/i2c.h"
#include "hw/hw.h"
#include "sysemu/blockdev.h"

#ifndef M24CXX_DEBUG
#define M24CXX_DEBUG 0
#endif
#define DB_PRINT(fmt, args...) do {\
    if (M24CXX_DEBUG) {\
        fprintf(stderr, "M24CXX: %s:" fmt, __func__, ## args);\
    } \
} while (0);

typedef enum {
    STOPPED,
    ADDRESSING,
    READING,
    WRITING,
} M24CXXXferState;

const char *m24cxx_state_names[] = {
    [STOPPED] = "STOPPED",
    [ADDRESSING] = "ADDRESSING",
    [READING] = "READING",
    [WRITING] = "WRITING",
};

typedef struct {
    I2CSlave i2c;
    uint16_t cur_addr;
    uint8_t state;

    BlockDriverState *bdrv;
    uint16_t size;

    uint8_t *storage;
} M24CXXState;

#define TYPE_M24CXX "at.24c08"

#define M24CXX(obj) \
     OBJECT_CHECK(M24CXXState, (obj), TYPE_M24CXX)

static void m24cxx_sync_complete(void *opaque, int ret)
{
    /* do nothing. Masters do not directly interact with the backing store,
     * only the working copy so no mutexing required.
     */
}

static void m24cxx_sync(I2CSlave *i2c)
{
    M24CXXState *s = M24CXX(i2c);
    int64_t nb_sectors;
    QEMUIOVector iov;

    if (!s->bdrv) {
        return;
    }

    /* the device is so small, just sync the whole thing */
    nb_sectors = DIV_ROUND_UP(s->size, BDRV_SECTOR_SIZE);
    qemu_iovec_init(&iov, 1);
    qemu_iovec_add(&iov, s->storage, nb_sectors * BDRV_SECTOR_SIZE);
    bdrv_aio_writev(s->bdrv, 0, &iov, nb_sectors, m24cxx_sync_complete, NULL);
}

static void m24cxx_reset(DeviceState *dev)
{
    M24CXXState *s = M24CXX(dev);

    m24cxx_sync(I2C_SLAVE(s));
    s->state = STOPPED;
    s->cur_addr = 0;
}

static int m24cxx_recv(I2CSlave *i2c)
{
    M24CXXState *s = M24CXX(i2c);
    int ret = 0;

    if (s->state == READING) {
        ret = s->storage[s->cur_addr++];
        DB_PRINT("storage %x <-> %x\n", s->cur_addr-1, ret);
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
        s->cur_addr &= ~0xFF;
        s->cur_addr |= data;
        DB_PRINT("setting address to %x\n", s->cur_addr);
        s->state = WRITING;
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

static void m24cxx_event(I2CSlave *i2c, enum i2c_event event)
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
}

static void m24cxx_decode_address(I2CSlave *i2c, uint8_t address)
{
    M24CXXState *s = M24CXX(i2c);

    s->cur_addr &= ~(0x0300);
    s->cur_addr |= (address & ((s->size - 1) >> 8)) << 8;
}

static void m24cxx_realize(DeviceState *dev, Error **errp)
{
    M24CXXState *s = M24CXX(dev);
    I2CSlave *i2c = I2C_SLAVE(dev);
    DriveInfo *dinfo = drive_get_next(IF_MTD);

    i2c->address_range = s->size >> 8 ? s->size >> 8 : 1;
    s->storage = g_new0(uint8_t, DIV_ROUND_UP(s->size, BDRV_SECTOR_SIZE) *
                                              BDRV_SECTOR_SIZE);

    if (dinfo && dinfo->bdrv) {
        s->bdrv = dinfo->bdrv;
        /* FIXME: Move to late init */
        if (bdrv_read(s->bdrv, 0, s->storage, DIV_ROUND_UP(s->size,
                                                    BDRV_SECTOR_SIZE))) {
            error_setg(errp, "Failed to initialize I2C EEPROM!\n");
            return;
        }
    } else {
        memset(s->storage, 0xFF, s->size);
    }
}

static void m24cxx_pre_save(void *opaque)
{
    m24cxx_sync((I2CSlave *)opaque);
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
    dc->props = m24cxx_properties;
}

static TypeInfo m24cxx_info = {
    .name          = TYPE_M24CXX,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(M24CXXState),
    .class_init    = m24cxx_class_init,
};

static void m24cxx_register_types(void)
{
    type_register_static(&m24cxx_info);
}

type_init(m24cxx_register_types)
