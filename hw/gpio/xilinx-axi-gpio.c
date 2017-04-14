/*
 * QEMU model of the Xilinx AXI GPIO Registers
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Written by Alistair Francis <alistair.francis@xilinx.com>
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
#include "hw/register-dep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"

#ifndef XLNX_AXI_GPIO_ERR_DEBUG
#define XLNX_AXI_GPIO_ERR_DEBUG 0
#endif

#define TYPE_XLNX_AXI_GPIO "xlnx.axi-gpio"

#define XLNX_AXI_GPIO(obj) \
     OBJECT_CHECK(XlnxAXIGPIO, (obj), TYPE_XLNX_AXI_GPIO)

DEP_REG32(GPIO_DATA, 0x00)
DEP_REG32(GPIO_TRI, 0x04)
DEP_REG32(GPIO2_DATA, 0x08)
DEP_REG32(GPIO2_TRI, 0x0C)
DEP_REG32(GIER, 0x11C)
    DEP_FIELD(GIER, GIE, 1, 31)
DEP_REG32(IP_ISR, 0x120)
    DEP_FIELD(IP_ISR, CHANNEL1_ST, 1, 0)
    DEP_FIELD(IP_ISR, CHANNEL2_ST, 1, 1)
DEP_REG32(IP_IER, 0x128)
    DEP_FIELD(IP_IER, CHANNEL1_EN, 1, 0)
    DEP_FIELD(IP_IER, CHANNEL2_EN, 1, 1)

#define R_MAX (R_IP_IER + 1)

typedef struct XlnxAXIGPIO {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq parent_irq;
    qemu_irq outputs1[32], outputs2[32];

    uint32_t regs[R_MAX];
    DepRegisterInfo regs_info[R_MAX];
} XlnxAXIGPIO;

/* The interrupts should be triggered when a change arrives on the GPIO pins */
static void irq_update(XlnxAXIGPIO *s)
{
    bool general_enable = DEP_AF_EX32(s->regs, GIER, GIE);
    bool pending = !!(s->regs[R_IP_ISR] & s->regs[R_IP_IER]);

    qemu_set_irq(s->parent_irq, general_enable & pending);
}

static void data_handler(void *opaque, int irq, int level, int channel)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(opaque);
    unsigned int data_regnr, tri_regnr;

    assert(channel > 0 && channel < 3);
    data_regnr = channel == 1 ? R_GPIO_DATA : R_GPIO2_DATA;
    tri_regnr = channel == 1 ? R_GPIO_TRI : R_GPIO2_TRI;

    if (!extract32(s->regs[tri_regnr], irq, 1) ||
        extract32(s->regs[data_regnr], irq, 1) == level) {
        /* GPIO is configured as output, or there is no change */
        return;
    }

    s->regs[data_regnr] = deposit32(s->regs[data_regnr], irq, 1, level);

    switch (channel) {
    case 1:
        DEP_AF_DP32(s->regs, IP_ISR, CHANNEL1_ST, 1);
        break;
    case 2:
        DEP_AF_DP32(s->regs, IP_ISR, CHANNEL2_ST, 1);
        break;
    }

    irq_update(s);
}

static void data_handler1(void *opaque, int irq, int level)
{
    data_handler(opaque, irq, level, 1);
}

static void data_handler2(void *opaque, int irq, int level)
{
    data_handler(opaque, irq, level, 2);
}

static void xlnx_axi_gpio_data_post_write(XlnxAXIGPIO *s, uint64_t val,
                                          int channel)
{
    unsigned int tri_regnr;
    bool gpio_set;
    int i;

    assert(channel > 0 && channel < 3);
    tri_regnr = channel == 1 ? R_GPIO_TRI : R_GPIO2_TRI;

    for (i = 0; i < 32; i++) {
        if (extract32(s->regs[tri_regnr], i, 1)) {
            /* GPIO is configured as input, don't change anything */
            continue;
        }

        gpio_set = extract32(val, i, 1);

        switch (channel) {
        case 1:
            qemu_set_irq(s->outputs1[i], gpio_set);
            break;
        case 2:
            qemu_set_irq(s->outputs2[i], gpio_set);
            break;
        }
    }
}

static void xlnx_axi_gpio_data_post_write1(DepRegisterInfo *reg, uint64_t val)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    xlnx_axi_gpio_data_post_write(s, val, 1);
}

static void xlnx_axi_gpio_data_post_write2(DepRegisterInfo *reg, uint64_t val)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    xlnx_axi_gpio_data_post_write(s, val, 2);
}

static void xlnx_axi_gpio_post_write(DepRegisterInfo *reg, uint64_t val)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    irq_update(s);
}

static uint64_t xlnx_axi_gpi_data_read(DepRegisterInfo *reg, uint64_t val,
                                       uint8_t channel)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    switch (channel) {
    case 1:
        return val & s->regs[R_GPIO_TRI];
    case 2:
        return val & s->regs[R_GPIO2_TRI];
    default:
        return val;
    }
}

static uint64_t xlnx_axi_gpio_data_post_read(DepRegisterInfo *reg, uint64_t val)
{
    return xlnx_axi_gpi_data_read(reg, val, 1);
}

static uint64_t xlnx_axi_gpio2_data_post_read(DepRegisterInfo *reg, uint64_t val)
{
    return xlnx_axi_gpi_data_read(reg, val, 2);
}

static DepRegisterAccessInfo xlnx_axi_gpio_regs_info[] = {
    {   .name = "GPIO_DATA",  .decode.addr = A_GPIO_DATA,
        .post_read = xlnx_axi_gpio_data_post_read,
        .post_write = xlnx_axi_gpio_data_post_write1,
    },{ .name = "GPIO_TRI",  .decode.addr = A_GPIO_TRI,
    },{ .name = "GPIO2_DATA",  .decode.addr = A_GPIO2_DATA,
        .post_read = xlnx_axi_gpio2_data_post_read,
        .post_write = xlnx_axi_gpio_data_post_write2,
    },{ .name = "GPIO2_TRI",  .decode.addr = A_GPIO2_TRI,
    },{ .name = "GIER",  .decode.addr = A_GIER,
        .post_write = xlnx_axi_gpio_post_write,
    },{ .name = "IP_IER",  .decode.addr = A_IP_IER,
        .post_write = xlnx_axi_gpio_post_write,
    },{ .name = "IP_ISR",  .decode.addr = A_IP_ISR,
        .post_write = xlnx_axi_gpio_post_write,
    }
};

static void xlnx_axi_gpio_reset(DeviceState *dev)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        dep_register_reset(&s->regs_info[i]);
    }

    irq_update(s);
}

static uint64_t xlnx_axi_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(opaque);
    DepRegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: read from %" HWADDR_PRIx "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr);
        return 0;
    }
    return dep_register_read(r);
}

static void xlnx_axi_gpio_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(opaque);
    DepRegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: write to %" HWADDR_PRIx "=%" PRIx64 "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr, value);
        return;
    }
    dep_register_write(r, value, ~0);
}

static const MemoryRegionOps xlnx_axi_gpio_ops = {
    .read = xlnx_axi_gpio_read,
    .write = xlnx_axi_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlnx_axi_gpio_realize(DeviceState *dev, Error **errp)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(xlnx_axi_gpio_regs_info); ++i) {
        DepRegisterInfo *r =
                    &s->regs_info[xlnx_axi_gpio_regs_info[i].decode.addr/4];

        *r = (DepRegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    xlnx_axi_gpio_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &xlnx_axi_gpio_regs_info[i],
            .debug = XLNX_AXI_GPIO_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
    }

    /* Create two GPIO in banks that QTest can use */
    qdev_init_gpio_in(dev, data_handler1, 32);
    qdev_init_gpio_in(dev, data_handler2, 32);

    /* Create GPIO banks as well */
    qdev_init_gpio_out(dev, s->outputs1, 32);
    qdev_init_gpio_out(dev, s->outputs2, 32);
}

static void xlnx_axi_gpio_init(Object *obj)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &xlnx_axi_gpio_ops, s,
                          TYPE_XLNX_AXI_GPIO, R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->parent_irq);
}

static const VMStateDescription vmstate_gpio = {
    .name = TYPE_XLNX_AXI_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxAXIGPIO, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void xlnx_axi_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xlnx_axi_gpio_reset;
    dc->realize = xlnx_axi_gpio_realize;
    dc->vmsd = &vmstate_gpio;
}

static const TypeInfo xlnx_axi_gpio_info = {
    .name          = TYPE_XLNX_AXI_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxAXIGPIO),
    .class_init    = xlnx_axi_gpio_class_init,
    .instance_init = xlnx_axi_gpio_init,
};

static void xlnx_axi_gpio_register_types(void)
{
    type_register_static(&xlnx_axi_gpio_info);
}

type_init(xlnx_axi_gpio_register_types)
