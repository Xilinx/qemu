/*
 * QEMU model of Xilinx I/O Module GPO
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
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/irq.h"
#include "qemu/log.h"

#ifndef XILINX_IO_MODULE_GPO_ERR_DEBUG
#define XILINX_IO_MODULE_GPO_ERR_DEBUG 0
#endif

#define TYPE_XILINX_IO_MODULE_GPO "xlnx.io_gpo"

#define XILINX_IO_MODULE_GPO(obj) \
     OBJECT_CHECK(XilinxGPO, (obj), TYPE_XILINX_IO_MODULE_GPO)

REG32(IOM_GPO, 0x00)
#define R_MAX                       (R_IOM_GPO + 1)

typedef struct XilinxGPO {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    struct {
        bool use;
        uint32_t size;
        uint32_t init;
    } cfg;
    RegisterInfo regs_info[R_MAX];
    uint32_t regs[R_MAX];

    qemu_irq outputs[32];
    const char *prefix;
} XilinxGPO;

static Property xlx_iom_properties[] = {
    DEFINE_PROP_BOOL("use-gpo", XilinxGPO, cfg.use, 0),
    DEFINE_PROP_UINT32("gpo-size", XilinxGPO, cfg.size, 0),
    DEFINE_PROP_UINT32("gpo-init", XilinxGPO, cfg.init, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void gpo_pw(RegisterInfo *reg, uint64_t value)
{
    XilinxGPO *s = XILINX_IO_MODULE_GPO(reg->opaque);
    unsigned int i;

    for (i = 0; i < s->cfg.size; i++) {
        bool flag = !!(value & (1 << i));
        qemu_set_irq(s->outputs[i], flag);
    }
}

static uint64_t gpo_pr(RegisterInfo *reg, uint64_t value)
{
    return 0;
}

static const RegisterAccessInfo gpo_regs_info[] = {
    {   .name = "GPO", .addr = A_IOM_GPO,
        .post_write = gpo_pw, .post_read = gpo_pr
    },
};

static void iom_gpo_reset(DeviceState *dev)
{
    XilinxGPO *s = XILINX_IO_MODULE_GPO(dev);

    gpo_pw(&s->regs_info[R_IOM_GPO], s->cfg.init);
}

static const MemoryRegionOps iom_gpo_ops = {
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
    XilinxGPO *s = XILINX_IO_MODULE_GPO(dev);

    assert(s->cfg.size <= 32);
    qdev_init_gpio_out(dev, s->outputs, s->cfg.size);
}

static void xlx_iom_init(Object *obj)
{
    XilinxGPO *s = XILINX_IO_MODULE_GPO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_IO_MODULE_GPO, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), gpo_regs_info,
                              ARRAY_SIZE(gpo_regs_info),
                              s->regs_info, s->regs,
                              &iom_gpo_ops,
                              XILINX_IO_MODULE_GPO_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_xlx_iom = {
    .name = TYPE_XILINX_IO_MODULE_GPO,
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

    dc->reset = iom_gpo_reset;
    dc->realize = xlx_iom_realize;
    device_class_set_props(dc, xlx_iom_properties);
    dc->vmsd = &vmstate_xlx_iom;
}

static const TypeInfo xlx_iom_info = {
    .name          = TYPE_XILINX_IO_MODULE_GPO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxGPO),
    .class_init    = xlx_iom_class_init,
    .instance_init = xlx_iom_init,
};

static void xlx_iom_register_types(void)
{
    type_register_static(&xlx_iom_info);
}

type_init(xlx_iom_register_types)
