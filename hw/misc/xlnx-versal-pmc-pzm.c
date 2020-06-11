/*
 * QEMU model of PRAM Zeroization Module
 *
 * Copyright (c) 2018 Xilinx Inc
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
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "hw/stream.h"
#include "hw/register.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#define TYPE_PMC_STREAM_ZERO "xlnx,pmc-stream-zero"

#ifndef PMC_STREAM_ZERO_ERR_DEBUG
#define PMC_STREAM_ZERO_ERR_DEBUG 0
#endif

#define DPRINT(fmt, args...) do {\
    if (PMC_STREAM_ZERO_ERR_DEBUG) {\
        qemu_log("%s: " fmt, __func__, ## args);\
    } \
} while (0);

#define PMC_STREAM_ZERO(obj) \
    OBJECT_CHECK(PmcStreamZero, (obj), TYPE_PMC_STREAM_ZERO)

REG32(PRAM_ZEROIZE_SIZE, 0x0)
    FIELD(PRAM_ZEROIZE_SIZE, VALUE, 0, 32)

#define R_MAX (R_PRAM_ZEROIZE_SIZE + 1)

/* PZM_BEAT_SIZE must be a multiple of 4.  */
#define PZM_BEAT_SIZE 16
QEMU_BUILD_BUG_ON((PZM_BEAT_SIZE % 4) != 0);

typedef struct PmcStreamZero {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    StreamSlave *tx_dev;

    StreamCanPushNotifyFn notify;
    void *notify_opaque;

    uint32_t data[PZM_BEAT_SIZE / 4];
    uint32_t regs[R_MAX];
} PmcStreamZero;

static void pmc_stream_zero_notify(void *opaque)
{
    PmcStreamZero *s = PMC_STREAM_ZERO(opaque);

    while (s->regs[R_PRAM_ZEROIZE_SIZE] &&
          stream_can_push(s->tx_dev, pmc_stream_zero_notify, s)) {
        if (stream_push(s->tx_dev, (uint8_t *)s->data, PZM_BEAT_SIZE, 0) !=
            PZM_BEAT_SIZE) {
            qemu_log("pmc_zero_pump: transfer size < %d\n", PZM_BEAT_SIZE);
        }
        s->regs[R_PRAM_ZEROIZE_SIZE] -= 1;
    }
}

static void pmc_stream_zero_reset(DeviceState *dev)
{
    PmcStreamZero *s = PMC_STREAM_ZERO(dev);

    s->regs[R_PRAM_ZEROIZE_SIZE] = 0;
}

static void pmc_stream_zero_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc =  DEVICE_CLASS(klass);

    dc->reset = pmc_stream_zero_reset;
}

static uint64_t pmc_stream_zero_read_reg(void *opaque, hwaddr addr,
                     unsigned size)
{
    PmcStreamZero *s = PMC_STREAM_ZERO(opaque);
    uint32_t ret;
    if (addr >= (R_MAX * 4)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s :decode addr 0x%x invalid",
                       __func__, (unsigned int)addr);
        return 0;
    }

    ret = s->regs[addr / 4];
    DPRINT("addr: 0x%x data:0x%x\n", (uint32_t)addr, (uint32_t)ret);
    return ret;
}

static void pmc_stream_zero_write_reg(void *opaque, hwaddr addr,
                  uint64_t data, unsigned size)
{
    PmcStreamZero *s = PMC_STREAM_ZERO(opaque);

    if (addr >= (R_MAX * 4)) {
        qemu_log_mask(LOG_GUEST_ERROR, " %s :decode addr 0x%x invalid",
                       __func__, (unsigned int) addr);
        return;
    }

    switch (addr / 4) {
    case R_PRAM_ZEROIZE_SIZE:
        s->regs[R_PRAM_ZEROIZE_SIZE] = data;
        pmc_stream_zero_notify(s);
        break;
    default:
        break;
    };
    DPRINT("addr: 0x%x data:0x%x\n", (uint32_t)addr, (uint32_t)data);
}


static const MemoryRegionOps pmc_stream_zero_mem_ops = {
    .read = pmc_stream_zero_read_reg,
    .write = pmc_stream_zero_write_reg,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void pmc_stream_zero_init(Object *obj)
{
    PmcStreamZero *s = PMC_STREAM_ZERO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;

    memory_region_init_io(&s->iomem, obj, &pmc_stream_zero_mem_ops, obj,
                          TYPE_PMC_STREAM_ZERO, R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
    object_property_add_link(obj, "stream-connected-pzm", TYPE_STREAM_SLAVE,
                             (Object **)&s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    for (i = 0; (i * 4) < PZM_BEAT_SIZE; i++) {
        s->data[i] = 0xDEADBEEF;
    }
}

static const TypeInfo pmc_stream_zero_info = {
    .name = TYPE_PMC_STREAM_ZERO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PmcStreamZero),
    .class_init = pmc_stream_zero_class_init,
    .instance_init = pmc_stream_zero_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void pmc_stream_zero_register_types(void)
{
    type_register_static(&pmc_stream_zero_info);
}

type_init(pmc_stream_zero_register_types)
