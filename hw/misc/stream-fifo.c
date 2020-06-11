/*
 * Copyright (c) 2013 Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/stream.h"
#include "qemu/fifo.h"

#ifndef STREAM_FIFO_ERR_DEBUG
#define STREAM_FIFO_ERR_DEBUG 0
#endif

#define TYPE_STREAM_FIFO "stream-fifo"

#define STREAM_FIFO(obj) \
     OBJECT_CHECK(StreamFifo, (obj), TYPE_STREAM_FIFO)

REG32(DP, 0x00)
REG32(CTL, 0x04)
    #define R_CTL_CORK      (1 << 0)
    #define R_CTL_RSVD ~1ull

#define R_MAX ((R_CTL) + 1)

typedef struct StreamFifo StreamFifo;

struct StreamFifo {
    SysBusDevice busdev;
    MemoryRegion iomem;

    Fifo fifo;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];

    StreamSlave *tx_dev;

    StreamCanPushNotifyFn notify;
    void *notify_opaque;
};

static void stream_fifo_notify(void *opaque)
{
    StreamFifo *s = STREAM_FIFO(opaque);

    while (!fifo_is_empty(&s->fifo) && !(s->regs[R_CTL] & R_CTL_CORK) &&
           stream_can_push(s->tx_dev, stream_fifo_notify, s)) {
        size_t ret;
        uint8_t buf[4];

        *((uint32_t *)buf) = cpu_to_le32(fifo_pop32(&s->fifo));
        ret = stream_push(s->tx_dev, buf, 4, false);
        assert(ret == 4);
    }

    if (s->notify) {
        StreamCanPushNotifyFn notify = s->notify;
        s->notify = NULL;
        notify(s->notify_opaque);
    }
}

static bool stream_fifo_stream_can_push(StreamSlave *obj,
                                        StreamCanPushNotifyFn notify,
                                        void *notify_opaque)
{
    StreamFifo *s = STREAM_FIFO(obj);
    bool ret = !(s->regs[R_CTL] & R_CTL_CORK) && !fifo_is_full(&s->fifo);

    if (!ret) {
        s->notify = notify;
        s->notify_opaque = notify_opaque;
    }
    return ret;
}

static size_t stream_fifo_stream_push(StreamSlave *obj, uint8_t *buf,
                                      size_t len, bool eop)
{
    StreamFifo *s = STREAM_FIFO(obj);
    size_t ret = 0;

    assert(!(len % 4));
    while (len && !(s->regs[R_CTL] & R_CTL_CORK) && !fifo_is_full(&s->fifo)) {
        fifo_push32(&s->fifo, le32_to_cpu(*(uint32_t *)buf));
        buf += (sizeof(uint32_t));
        len -= 4;
        ret += 4;
    }
    return ret;
}


static void stream_fifo_update(RegisterInfo *reg, uint64_t val)
{
    StreamFifo *s = STREAM_FIFO(reg->opaque);

    stream_fifo_notify(s);
}

static void stream_fifo_dp_post_write(RegisterInfo *reg, uint64_t val)
{
    StreamFifo *s = STREAM_FIFO(reg->opaque);

    if (fifo_is_full(&s->fifo)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Write to full fifo\n");
    } else {
        fifo_push32(&s->fifo, val);
    }
    stream_fifo_update(reg, val);
}

static uint64_t stream_fifo_dp_post_read(RegisterInfo *reg, uint64_t val)
{
    StreamFifo *s = STREAM_FIFO(reg->opaque);

    if (fifo_is_empty(&s->fifo)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Write to full fifo\n");
    } else {
        return fifo_pop32(&s->fifo);
    }
    return 0;
}

/* TODO: Define register definitions. One entry for each register */

static const RegisterAccessInfo stream_fifo_regs_info[] = {
    {   .name = "data port",                .addr = A_DP,
            .post_write = stream_fifo_dp_post_write,
            .post_read = stream_fifo_dp_post_read,
    },{ .name = "control",                  .addr = A_CTL,
            .rsvd = R_CTL_RSVD,
            .reset = R_CTL_CORK,
    }
};

static void stream_fifo_reset(DeviceState *dev)
{
    StreamFifo *s = STREAM_FIFO(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }

    fifo_reset(&s->fifo);
}

static const MemoryRegionOps stream_fifo_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void stream_fifo_realize(DeviceState *dev, Error **errp)
{
    StreamFifo *s = STREAM_FIFO(dev);

#define STREAM_FIFO_DEPTH 64
    fifo_create32(&s->fifo, STREAM_FIFO_DEPTH);
}

static void stream_fifo_init(Object *obj)
{
    StreamFifo *s = STREAM_FIFO(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, "MMIO", R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), stream_fifo_regs_info,
                              ARRAY_SIZE(stream_fifo_regs_info),
                              s->regs_info, s->regs,
                              &stream_fifo_ops,
                              STREAM_FIFO_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    object_property_add_link(obj, "stream-connected", TYPE_STREAM_SLAVE,
                             (Object **)&s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);

}

static const VMStateDescription vmstate_stream_fifo = {
    .name = "stream_fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, StreamFifo, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void stream_fifo_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    dc->reset = stream_fifo_reset;
    dc->realize = stream_fifo_realize;
    dc->vmsd = &vmstate_stream_fifo;

    ssc->push = stream_fifo_stream_push;
    ssc->can_push = stream_fifo_stream_can_push;
}

static const TypeInfo stream_fifo_info = {
    .name          = TYPE_STREAM_FIFO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(StreamFifo),
    .class_init    = stream_fifo_class_init,
    .instance_init = stream_fifo_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { },
    }
};

static void stream_fifo_register_types(void)
{
    type_register_static(&stream_fifo_info);
}

type_init(stream_fifo_register_types)
