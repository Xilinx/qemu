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
#include "hw/register.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "hw/irq.h"

#ifndef XLNX_AXI_GPIO_ERR_DEBUG
#define XLNX_AXI_GPIO_ERR_DEBUG 0
#endif

#define TYPE_XLNX_AXI_GPIO "xlnx.axi-gpio"

#define XLNX_AXI_GPIO(obj) \
     OBJECT_CHECK(XlnxAXIGPIO, (obj), TYPE_XLNX_AXI_GPIO)

REG32(GPIO_DATA, 0x00)
REG32(GPIO_TRI, 0x04)
REG32(GPIO2_DATA, 0x08)
REG32(GPIO2_TRI, 0x0C)
REG32(GIER, 0x11C)
    FIELD(GIER, GIE, 31, 1)
REG32(IP_ISR, 0x120)
    FIELD(IP_ISR, CHANNEL2_ST, 1, 1)
    FIELD(IP_ISR, CHANNEL1_ST, 0, 1)
REG32(IP_IER, 0x128)
    FIELD(IP_IER, CHANNEL2_EN, 1, 1)
    FIELD(IP_IER, CHANNEL1_EN, 0, 1)

#define R_MAX (R_IP_IER + 1)

typedef struct XlnxAXIGPIO {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq parent_irq;
    qemu_irq outputs1[32], outputs2[32];

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} XlnxAXIGPIO;

/* The interrupts should be triggered when a change arrives on the GPIO pins */
static void irq_update(XlnxAXIGPIO *s)
{
    bool general_enable = ARRAY_FIELD_EX32(s->regs, GIER, GIE);
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
        ARRAY_FIELD_DP32(s->regs, IP_ISR, CHANNEL1_ST, 1);
        break;
    case 2:
        ARRAY_FIELD_DP32(s->regs, IP_ISR, CHANNEL2_ST, 1);
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

static void xlnx_axi_gpio_data_post_write1(RegisterInfo  *reg, uint64_t val)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    xlnx_axi_gpio_data_post_write(s, val, 1);
}

static void xlnx_axi_gpio_data_post_write2(RegisterInfo  *reg, uint64_t val)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    xlnx_axi_gpio_data_post_write(s, val, 2);
}

static void xlnx_axi_gpio_post_write(RegisterInfo  *reg, uint64_t val)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(reg->opaque);

    irq_update(s);
}

static uint64_t xlnx_axi_gpi_data_read(RegisterInfo  *reg, uint64_t val,
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

static uint64_t xlnx_axi_gpio_data_post_read(RegisterInfo  *reg, uint64_t val)
{
    return xlnx_axi_gpi_data_read(reg, val, 1);
}

static uint64_t xlnx_axi_gpio2_data_post_read(RegisterInfo  *reg, uint64_t val)
{
    return xlnx_axi_gpi_data_read(reg, val, 2);
}

static RegisterAccessInfo  xlnx_axi_gpio_regs_info[] = {
    {   .name = "GPIO_DATA",  .addr = A_GPIO_DATA,
        .post_read = xlnx_axi_gpio_data_post_read,
        .post_write = xlnx_axi_gpio_data_post_write1,
    },{ .name = "GPIO_TRI",  .addr = A_GPIO_TRI,
    },{ .name = "GPIO2_DATA",  .addr = A_GPIO2_DATA,
        .post_read = xlnx_axi_gpio2_data_post_read,
        .post_write = xlnx_axi_gpio_data_post_write2,
    },{ .name = "GPIO2_TRI",  .addr = A_GPIO2_TRI,
    },{ .name = "GIER",  .addr = A_GIER,
        .post_write = xlnx_axi_gpio_post_write,
    },{ .name = "IP_IER",  .addr = A_IP_IER,
        .post_write = xlnx_axi_gpio_post_write,
    },{ .name = "IP_ISR",  .addr = A_IP_ISR,
        .post_write = xlnx_axi_gpio_post_write,
    }
};

static void xlnx_axi_gpio_reset(DeviceState *dev)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    irq_update(s);
}

static const MemoryRegionOps xlnx_axi_gpio_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlnx_axi_gpio_init(Object *obj)
{
    XlnxAXIGPIO *s = XLNX_AXI_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj,
                          TYPE_XLNX_AXI_GPIO, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), xlnx_axi_gpio_regs_info,
                              ARRAY_SIZE(xlnx_axi_gpio_regs_info),
                              s->regs_info, s->regs,
                              &xlnx_axi_gpio_ops,
                              XLNX_AXI_GPIO_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->parent_irq);

    /* Create two GPIO in banks that QTest can use */
    qdev_init_gpio_in(DEVICE(obj), data_handler1, 32);
    qdev_init_gpio_in(DEVICE(obj), data_handler2, 32);

    /* Create GPIO banks as well */
    qdev_init_gpio_out(DEVICE(obj), s->outputs1, 32);
    qdev_init_gpio_out(DEVICE(obj), s->outputs2, 32);
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
