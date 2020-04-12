/*
 * QEMU model of Xilinx I/O Module UART
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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
#include "hw/ptimer.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/register.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"

#ifndef XILINX_IO_MODULE_UART_ERR_DEBUG
#define XILINX_IO_MODULE_UART_ERR_DEBUG 0
#endif

#define TYPE_XILINX_IO_MODULE_UART "xlnx.io_uart"

#define XILINX_IO_MODULE_UART(obj) \
     OBJECT_CHECK(XilinxUART, (obj), TYPE_XILINX_IO_MODULE_UART)

REG32(IOM_UART_RX, 0x0)
REG32(IOM_UART_TX, 0x4)
REG32(IOM_UART_STATUS, 0x8)
    FIELD(IOM_UART_STATUS, PARITY_ERR, 7, 1)
    FIELD(IOM_UART_STATUS, FRAME_ERR, 6, 1)
    FIELD(IOM_UART_STATUS, OVERRUN, 5, 1)
    FIELD(IOM_UART_STATUS, TX_USED, 3, 1)
    FIELD(IOM_UART_STATUS, RX_VALID, 0, 1)

REG32(IOM_UART_BAUD, 0x0)

#define R_MAX_0 (R_IOM_UART_STATUS + 1)
#define R_MAX_1 (R_IOM_UART_BAUD + 1)

typedef struct XilinxUART {
    SysBusDevice parent_obj;
    MemoryRegion iomem[2];
    qemu_irq irq_rx;
    qemu_irq irq_tx;
    qemu_irq irq_err;

    struct {
        bool use_rx;
        bool use_tx;
        bool rx_interrupt;
        bool tx_interrupt;
        bool err_interrupt;
    } cfg;
    CharBackend chr;
    uint32_t regs[R_MAX_0];
    uint32_t baud;
    RegisterInfo regs_info0[R_MAX_0];
    RegisterInfo regs_info1[R_MAX_1];
    RegisterInfo *regs_infos[2];
    const char *prefix;
} XilinxUART;

static Property xlx_iom_properties[] = {
    DEFINE_PROP_BOOL("use-uart-rx", XilinxUART, cfg.use_rx, 0),
    DEFINE_PROP_BOOL("use-uart-tx", XilinxUART, cfg.use_tx, 0),
    DEFINE_PROP_BOOL("uart-rx-interrupt", XilinxUART, cfg.rx_interrupt, 0),
    DEFINE_PROP_BOOL("uart-tx-interrupt", XilinxUART, cfg.tx_interrupt, 0),
    DEFINE_PROP_BOOL("uart-error-interrupt", XilinxUART, cfg.err_interrupt, 0),
    DEFINE_PROP_CHR("chardev", XilinxUART, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void uart_rx(void *opaque, const uint8_t *buf, int size)
{
    XilinxUART *s = opaque;

    if (!s->cfg.use_rx) {
        return;
    }

    if (s->regs[R_IOM_UART_STATUS] & R_IOM_UART_STATUS_RX_VALID_MASK) {
        s->regs[R_IOM_UART_STATUS] |= R_IOM_UART_STATUS_OVERRUN_MASK;
        if (s->cfg.err_interrupt) {
            qemu_irq_pulse(s->irq_err);
        }
        return;
    }

    s->regs[R_IOM_UART_RX] = *buf;
    s->regs[R_IOM_UART_STATUS] |= R_IOM_UART_STATUS_RX_VALID_MASK;
    if (s->cfg.rx_interrupt) {
        qemu_irq_pulse(s->irq_rx);
    }
}

static int uart_can_rx(void *opaque)
{
    XilinxUART *s = opaque;
    return s->cfg.use_rx;
}

static void uart_event(void *opaque, QEMUChrEvent event)
{
}

static uint64_t uart_rx_pr(RegisterInfo *reg, uint64_t val)
{
    XilinxUART *s = XILINX_IO_MODULE_UART(reg->opaque);
    s->regs[R_IOM_UART_STATUS] &= ~R_IOM_UART_STATUS_OVERRUN_MASK;
    s->regs[R_IOM_UART_STATUS] &= ~R_IOM_UART_STATUS_RX_VALID_MASK;
    return s->regs[R_IOM_UART_RX];
}

static uint64_t uart_sts_pr(RegisterInfo *reg, uint64_t val)
{
    XilinxUART *s = XILINX_IO_MODULE_UART(reg->opaque);
    s->regs[R_IOM_UART_STATUS] &= ~R_IOM_UART_STATUS_OVERRUN_MASK;
    return val;
}

static void uart_tx_pw(RegisterInfo *reg, uint64_t value)
{
    XilinxUART *s = XILINX_IO_MODULE_UART(reg->opaque);
    if (s->cfg.use_tx) {
        unsigned char ch = value;
        qemu_chr_fe_write(&s->chr, &ch, 1);
        if (s->cfg.tx_interrupt) {
            qemu_irq_pulse(s->irq_tx);
        }
    }
}

static const RegisterAccessInfo uart_regs_info0[] = {
    [R_IOM_UART_RX] = { .name = "UART_RX", .addr = A_IOM_UART_RX,
                        .post_read = uart_rx_pr },
    [R_IOM_UART_TX] = { .name = "UART_TX", .addr = A_IOM_UART_TX,
                        .post_write = uart_tx_pw },
    [R_IOM_UART_STATUS] = { .name = "UART_STATUS", .addr = A_IOM_UART_STATUS,
                        .post_read = uart_sts_pr },
};

static const RegisterAccessInfo uart_regs_info1[] = {
    [R_IOM_UART_BAUD] = { .name = "UART_BAUD", .addr = A_IOM_UART_BAUD},
};
static const RegisterAccessInfo *uart_reginfos[] = {
    &uart_regs_info0[0], &uart_regs_info1[0]
};

static const unsigned int uart_reginfo_sizes[] = {
    ARRAY_SIZE(uart_regs_info0),
    ARRAY_SIZE(uart_regs_info1),
};

static const unsigned int uart_rmax[] = {
    R_MAX_0,
    R_MAX_1
};

static const MemoryRegionOps iom_uart_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void iom_uart_reset(DeviceState *dev)
{
    XilinxUART *s = XILINX_IO_MODULE_UART(dev);
    unsigned int i;
    unsigned int rmap;

    for (rmap = 0; rmap < ARRAY_SIZE(uart_reginfos); rmap++) {
        for (i = 0; i < uart_reginfo_sizes[rmap]; ++i) {
            register_reset(&s->regs_infos[rmap][i]);
        }
    }
}

static void xlx_iom_realize(DeviceState *dev, Error **errp)
{
    XilinxUART *s = XILINX_IO_MODULE_UART(dev);

    if (s->cfg.use_rx || s->cfg.use_tx) {
        qemu_chr_fe_set_handlers(&s->chr, uart_can_rx, uart_rx, uart_event,
                                 NULL, s, NULL, true);
    }
}

static void xlx_iom_init(Object *obj)
{
    XilinxUART *s = XILINX_IO_MODULE_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;
    RegisterInfoArray *reg_array;
    uint32_t *regmaps[] = { s->regs, &s->baud };

    s->regs_infos[0] = s->regs_info0;
    s->regs_infos[1] = s->regs_info1;

    for (i = 0; i < ARRAY_SIZE(s->iomem); i++) {
        char *region_name = g_strdup_printf("%s-%d", TYPE_XILINX_IO_MODULE_UART,
                                            i);
        memory_region_init(&s->iomem[i], obj,
                              region_name, uart_rmax[i] * 4);
        reg_array =
            register_init_block32(DEVICE(obj), uart_reginfos[i],
                            uart_reginfo_sizes[i],
                            s->regs_infos[i], regmaps[i],
                            &iom_uart_ops,
                            XILINX_IO_MODULE_UART_ERR_DEBUG,
                            uart_rmax[i] * 4);
        memory_region_add_subregion(&s->iomem[i], 0, &reg_array->mem);
        g_free(region_name);
        sysbus_init_mmio(sbd, &s->iomem[i]);
    }
    sysbus_init_irq(sbd, &s->irq_err);
    sysbus_init_irq(sbd, &s->irq_tx);
    sysbus_init_irq(sbd, &s->irq_rx);
}

static const VMStateDescription vmstate_xlx_iom = {
    .name = TYPE_XILINX_IO_MODULE_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static void xlx_iom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = iom_uart_reset;
    dc->realize = xlx_iom_realize;
    device_class_set_props(dc, xlx_iom_properties);
    dc->vmsd = &vmstate_xlx_iom;
}

static const TypeInfo xlx_iom_info = {
    .name          = TYPE_XILINX_IO_MODULE_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxUART),
    .class_init    = xlx_iom_class_init,
    .instance_init = xlx_iom_init,
};

static void xlx_iom_register_types(void)
{
    type_register_static(&xlx_iom_info);
}

type_init(xlx_iom_register_types)
