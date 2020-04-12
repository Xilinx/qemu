/*
 * QEMU model of Xilinx I/O Module GPI
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
#include "hw/register.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/fdt_generic_util.h"

#ifndef XILINX_IO_MODULE_GPI_ERR_DEBUG
#define XILINX_IO_MODULE_GPI_ERR_DEBUG 0
#endif

#define TYPE_XILINX_IO_MODULE_GPI "xlnx.io_gpi"

#define XILINX_IO_MODULE_GPI(obj) \
     OBJECT_CHECK(XilinxGPI, (obj), TYPE_XILINX_IO_MODULE_GPI)

REG32(IOM_GPI, 0x00)
#define R_MAX                       (R_IOM_GPI + 1)

typedef struct XilinxGPI {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq parent_irq;
    /* Interrupt Enable */
    uint32_t ien;

    struct {
        bool use;
        bool interrupt;
        uint32_t size;
    } cfg;
    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
    const char *prefix;
} XilinxGPI;

static Property xlx_iom_properties[] = {
    DEFINE_PROP_BOOL("use-gpi", XilinxGPI, cfg.use, 0),
    DEFINE_PROP_BOOL("gpi-interrupt", XilinxGPI, cfg.interrupt, 0),
    DEFINE_PROP_UINT32("gpi-size", XilinxGPI, cfg.size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void update_irq(XilinxGPI *s)
{
    if (s->ien & s->regs[R_IOM_GPI]) {
        qemu_irq_pulse(s->parent_irq);
    }
}

static void irq_handler(void *opaque, int irq, int level)
{
    XilinxGPI *s = XILINX_IO_MODULE_GPI(opaque);
    uint32_t old = s->regs[R_IOM_GPI];

    /* If enable is set for @irq pin, update @irq pin in GPI and
     * trigger interrupt if transition is 0->1 */
    s->regs[R_IOM_GPI] &= ~(1 << irq);
    s->regs[R_IOM_GPI] |= (!!level) << irq;
    if (old != s->regs[R_IOM_GPI]) {
        update_irq(s);
    }
}


static void named_irq_handler(void *opaque, int n, int level)
{
    XilinxGPI *s  = XILINX_IO_MODULE_GPI(opaque);
    irq_handler(s, n, level);
}

/* Called when someone writes into LOCAL GPIx_ENABLE */
static void ien_handler(void *opaque, int n, int level)
{
    XilinxGPI *s = XILINX_IO_MODULE_GPI(opaque);

    if (level != s->ien) {
        s->ien = level;
        update_irq(s);
    }
}

static const RegisterAccessInfo gpi_regs_info[] = {
    { .name = "GPI", .addr = A_IOM_GPI, .ro = ~0 },
};

static void iom_gpi_reset(DeviceState *dev)
{
    XilinxGPI *s = XILINX_IO_MODULE_GPI(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
    /* Disable all interrupts initially. */
    s->ien = 0;
}

static const MemoryRegionOps iom_gpi_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlx_iom_realize(DeviceState *dev, Error **errp)
{
    XilinxGPI *s = XILINX_IO_MODULE_GPI(dev);

    assert(s->cfg.size <= 32);
    /* FIXME: Leave the std ones for qtest. Add a qtest way to name
       the GPIO namespace.  */
    qdev_init_gpio_in(dev, irq_handler, s->cfg.size);
    qdev_init_gpio_in_named(dev, named_irq_handler, "GPI", 32);
    qdev_init_gpio_in_named(dev, ien_handler, "IEN", 32);
}

static void xlx_iom_init(Object *obj)
{
    XilinxGPI *s = XILINX_IO_MODULE_GPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_IO_MODULE_GPI, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), gpi_regs_info,
                              ARRAY_SIZE(gpi_regs_info),
                              s->regs_info, s->regs,
                              &iom_gpi_ops,
                              XILINX_IO_MODULE_GPI_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->parent_irq);
}

static const VMStateDescription vmstate_xlx_iom = {
    .name = TYPE_XILINX_IO_MODULE_GPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};


static const FDTGenericGPIOSet gpio_sets [] = {
    {
      .names = &fdt_generic_gpio_name_set_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "GPI", .fdt_index = 0, .range = 32 },
        { },
      },
    },
    { },
};

static const FDTGenericGPIOSet gpio_client_sets[] = {
    {
      .names = &fdt_generic_gpio_name_set_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "IEN", .fdt_index = 0 },
        { },
      },
    },
    { },
};

static void xlx_iom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);

    dc->reset = iom_gpi_reset;
    dc->realize = xlx_iom_realize;
    device_class_set_props(dc, xlx_iom_properties);
    dc->vmsd = &vmstate_xlx_iom;
    fggc->controller_gpios = gpio_sets;
    fggc->client_gpios = gpio_client_sets;
}

static const TypeInfo xlx_iom_info = {
    .name          = TYPE_XILINX_IO_MODULE_GPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxGPI),
    .class_init    = xlx_iom_class_init,
    .instance_init = xlx_iom_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
};

static void xlx_iom_register_types(void)
{
    type_register_static(&xlx_iom_info);
}

type_init(xlx_iom_register_types)
