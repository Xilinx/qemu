/*
 * QEMU model of Xilinx I/O Module Interrupt Controller
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
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/fdt_generic_util.h"

#ifndef XILINX_IO_MODULE_INTC_ERR_DEBUG
#define XILINX_IO_MODULE_INTC_ERR_DEBUG 0
#endif

#define TYPE_XILINX_IO_MODULE_INTC "xlnx.io_intc"

#define XILINX_IO_MODULE_INTC(obj) \
     OBJECT_CHECK(XilinxIntC, (obj), TYPE_XILINX_IO_MODULE_INTC)

#define DB_PRINT_L(lvl, fmt, args...) do {\
    if (XILINX_IO_MODULE_INTC_ERR_DEBUG >= (lvl)) {\
        qemu_log(TYPE_XILINX_IO_MODULE_INTC ": %s: " fmt, __func__, ## args);\
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)


REG32(IOM_IRQ_MODE, 0x0)

#define R_MAX_0                     (1)

REG32(IOM_IRQ_STATUS, 0x0)
REG32(IOM_IRQ_PENDING, 0x4)
REG32(IOM_IRQ_ENABLE, 0x8)
REG32(IOM_IRQ_ACK, 0xC)

#define IOM_IRQF_PIT1_SHIFT         3
#define IOM_IRQF_PIT2_SHIFT         4
#define IOM_IRQF_PIT3_SHIFT         5
#define IOM_IRQF_PIT4_SHIFT         6

#define IOM_IRQF_UART_ERR           (1 << 0)
#define IOM_IRQF_UART_TX            (1 << 1)
#define IOM_IRQF_UART_RX            (1 << 2)
#define IOM_IRQF_PIT1               (1 << IOM_IRQF_PIT1_SHIFT)
#define IOM_IRQF_PIT2               (1 << IOM_IRQF_PIT2_SHIFT)
#define IOM_IRQF_PIT3               (1 << IOM_IRQF_PIT3_SHIFT)
#define IOM_IRQF_PIT4               (1 << IOM_IRQF_PIT4_SHIFT)
#define IOM_IRQF_FIT1               (1 << 7)
#define IOM_IRQF_FIT2               (1 << 8)
#define IOM_IRQF_FIT3               (1 << 9)
#define IOM_IRQF_FIT4               (1 << 10)
#define IOM_IRQF_GPI1               (1 << 11)
#define IOM_IRQF_GPI2               (1 << 12)
#define IOM_IRQF_GPI3               (1 << 13)
#define IOM_IRQF_GPI4               (1 << 14)
#define IOM_IRQF_EXT0               (1 << 16)

#define R_MAX_1                     (R_IOM_IRQ_ACK + 1)

#define R_MAX_2                     (0x80 / 4)

typedef struct XilinxIntC {
    SysBusDevice parent_obj;
    MemoryRegion iomem[3];
    qemu_irq parent_irq;

    struct {
        bool use_ext_intr;
        uint32_t intr_size;
        uint32_t level_edge;
        uint32_t positive;
        bool has_fast;
        uint32_t addr_width;
        uint32_t base_vectors;
    } cfg;

    uint32_t irq_raw;
    uint32_t irq_mode;
    uint32_t regs[R_MAX_1];
    uint32_t vectors[R_MAX_1];
    RegisterInfo regs_info0[R_MAX_0];
    RegisterInfo regs_info1[R_MAX_1];
    RegisterInfo regs_info2[R_MAX_2];
    RegisterInfo *regs_infos[3];
    const char *prefix;
    /* Debug only */
    bool irq_output;
} XilinxIntC;

static Property xlx_iom_properties[] = {
    DEFINE_PROP_BOOL("intc-use-ext-intr", XilinxIntC, cfg.use_ext_intr, 0),
    DEFINE_PROP_UINT32("intc-intr-size", XilinxIntC, cfg.intr_size, 0),
    DEFINE_PROP_UINT32("intc-level-edge", XilinxIntC, cfg.level_edge, 0),
    DEFINE_PROP_UINT32("intc-positive", XilinxIntC, cfg.positive, 0),
    DEFINE_PROP_BOOL("intc-has-fast", XilinxIntC, cfg.has_fast, 0),
    DEFINE_PROP_UINT32("intc-addr-width", XilinxIntC, cfg.addr_width, 32),
    DEFINE_PROP_UINT32("intc-base-vectors", XilinxIntC, cfg.base_vectors, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void iom_intc_irq_ack(RegisterInfo *reg, uint64_t val64);
static void iom_intc_update(RegisterInfo *reg, uint64_t val64);

static void xlx_iom_irq_update(XilinxIntC *s)
{
    bool old_state = s->irq_output;

    s->regs[R_IOM_IRQ_PENDING] = s->regs[R_IOM_IRQ_STATUS];
    s->regs[R_IOM_IRQ_PENDING] &= s->regs[R_IOM_IRQ_ENABLE];
    s->irq_output = s->regs[R_IOM_IRQ_PENDING];
    DB_PRINT_L(s->irq_output != old_state ? 1 : 2, "Setting IRQ output = %d\n",
               (int)s->irq_output);
    qemu_set_irq(s->parent_irq, s->irq_output);
}

static void iom_intc_irq_ack(RegisterInfo *reg, uint64_t val64)
{
    XilinxIntC *s = XILINX_IO_MODULE_INTC(reg->opaque);
    uint32_t val = val64;
    /* Only clear.  */
    val &= s->regs[R_IOM_IRQ_STATUS];
    s->regs[R_IOM_IRQ_STATUS] ^= val;

    /* Active level triggered interrupts stay high.  */
    s->regs[R_IOM_IRQ_STATUS] |= s->irq_raw & ~s->cfg.level_edge;

    xlx_iom_irq_update(s);
}

static void iom_intc_update(RegisterInfo *reg, uint64_t val64)
{
    XilinxIntC *s = XILINX_IO_MODULE_INTC(reg->opaque);
    xlx_iom_irq_update(s);
}

static const MemoryRegionOps iom_intc_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void irq_handler(void *opaque, int irq, int level)
{
    XilinxIntC *s = XILINX_IO_MODULE_INTC(opaque);
    uint32_t mask = 1 << irq;
    uint32_t flip = (~s->cfg.positive) & mask;
    uint32_t prev = s->irq_raw;
    uint32_t p;

    s->irq_raw &= ~(1 << irq);
    s->irq_raw |= (!!level) << irq;

    /* Turn active-low into active-high.  */
    s->irq_raw ^= flip;

    DB_PRINT_L(prev ^ s->irq_raw ? 1 : 2, "Input irq %d = %d\n", irq, level);

    if (s->cfg.level_edge & (1 << irq)) {
        /* Edge triggered.  */
        p = (prev ^ s->irq_raw) & s->irq_raw & mask;
    } else {
        /* Level triggered.  */
        p = s->irq_raw & mask;
    }
    s->regs[R_IOM_IRQ_STATUS] |= p;
    xlx_iom_irq_update(s);
}

static const RegisterAccessInfo intc_regs_info0[] = {
    {  .name = "IRQ_MODE",  .addr = A_IOM_IRQ_MODE }
};

static const RegisterAccessInfo intc_regs_info1[] = {
    {  .name = "IRQ_STATUS",  .addr = A_IOM_IRQ_STATUS,  .ro = ~0 },
    {  .name = "IRQ_PENDING",  .addr = A_IOM_IRQ_PENDING,  .ro = ~0 },
    {  .name = "IRQ_ENABLE",  .addr = A_IOM_IRQ_ENABLE,
       .post_write = iom_intc_update,
    },
    {  .name = "IRQ_ACK",  .addr = A_IOM_IRQ_ACK,
       .post_write = iom_intc_irq_ack,
    },
};


static const RegisterAccessInfo intc_regs_info2[] = {
#define REG_VECTOR(n)  \
    { .name  = "IRQ_VECTOR" #n, .addr = n * 4 }
    REG_VECTOR(0),
    REG_VECTOR(1),
    REG_VECTOR(2),
    REG_VECTOR(3),
    REG_VECTOR(4),
    REG_VECTOR(5),
    REG_VECTOR(6),
    REG_VECTOR(7),
    REG_VECTOR(8),
    REG_VECTOR(9),
    REG_VECTOR(10),
    REG_VECTOR(11),
    REG_VECTOR(12),
    REG_VECTOR(13),
    REG_VECTOR(14),
    REG_VECTOR(15),
    REG_VECTOR(16),
    REG_VECTOR(17),
    REG_VECTOR(18),
    REG_VECTOR(19),
    REG_VECTOR(20),
    REG_VECTOR(21),
    REG_VECTOR(22),
    REG_VECTOR(23),
    REG_VECTOR(24),
    REG_VECTOR(25),
    REG_VECTOR(26),
    REG_VECTOR(27),
    REG_VECTOR(28),
    REG_VECTOR(29),
    REG_VECTOR(30),
    REG_VECTOR(31),
};

static const RegisterAccessInfo *intc_reginfos[] = {
    &intc_regs_info0[0], &intc_regs_info1[0], &intc_regs_info2[0]
};

static const unsigned int intc_reginfo_sizes[] = {
    ARRAY_SIZE(intc_regs_info0),
    ARRAY_SIZE(intc_regs_info1),
    ARRAY_SIZE(intc_regs_info2),
};


static void iom_intc_reset(DeviceState *dev)
{
    XilinxIntC *s = XILINX_IO_MODULE_INTC(dev);
    unsigned int i;
    unsigned int rmap;

    for (rmap = 0; rmap < ARRAY_SIZE(intc_reginfos); rmap++) {
        for (i = 0; i < intc_reginfo_sizes[rmap]; ++i) {
            register_reset(&s->regs_infos[rmap][i]);
        }
    }
}

static void xlx_iom_realize(DeviceState *dev, Error **errp)
{
    XilinxIntC *s = XILINX_IO_MODULE_INTC(dev);

    s->prefix = object_get_canonical_path(OBJECT(dev));
    /* Internal interrupts are edge triggered?  */
    s->cfg.level_edge <<= 16;
    s->cfg.level_edge |= 0xffff;
    /* Internal interrupts are postitive.  */
    s->cfg.positive <<= 16;
    s->cfg.positive |= 0xffff;
    /* Max 16 external interrupts.  */
    assert(s->cfg.intr_size <= 16);

    qdev_init_gpio_in(dev, irq_handler, 16 + s->cfg.intr_size);
}

static void xlx_iom_init(Object *obj)
{
    XilinxIntC *s = XILINX_IO_MODULE_INTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;

    s->regs_infos[0] = s->regs_info0;
    s->regs_infos[1] = s->regs_info1;
    s->regs_infos[2] = s->regs_info2;

    for (i = 0; i < ARRAY_SIZE(s->iomem); i++) {
        RegisterInfoArray *reg_array;
        char *region_name = g_strdup_printf("%s-%d", TYPE_XILINX_IO_MODULE_INTC,
                                            i);
        memory_region_init(&s->iomem[i], obj,
                           region_name, intc_reginfo_sizes[i] * 4);
        g_free(region_name);

        reg_array =
             register_init_block32(DEVICE(obj), intc_reginfos[i],
                              intc_reginfo_sizes[i],
                              s->regs_infos[i], s->regs,
                              &iom_intc_ops,
                              XILINX_IO_MODULE_INTC_ERR_DEBUG,
                              intc_reginfo_sizes[i] * 4);
        memory_region_add_subregion(&s->iomem[i],
                                    0x0,
                                    &reg_array->mem);
        sysbus_init_mmio(sbd, &s->iomem[i]);
    }
    qdev_init_gpio_out(DEVICE(obj), &s->parent_irq, 1);
}

static int xilinx_iom_fdt_get_irq(FDTGenericIntc *obj, qemu_irq *irqs,
                                  uint32_t *cells, int ncells, int max,
                                  Error **errp)
{
    /* FIXME: Add Error checking */
    (*irqs) = qdev_get_gpio_in(DEVICE(obj), cells[0]);
    return 1;
};

static const VMStateDescription vmstate_xlx_iom = {
    .name = TYPE_XILINX_IO_MODULE_INTC,
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
    FDTGenericIntcClass *fgic = FDT_GENERIC_INTC_CLASS(klass);

    dc->reset = iom_intc_reset;
    dc->realize = xlx_iom_realize;
    device_class_set_props(dc, xlx_iom_properties);
    dc->vmsd = &vmstate_xlx_iom;
    fgic->get_irq = xilinx_iom_fdt_get_irq;
}

static const TypeInfo xlx_iom_info = {
    .name          = TYPE_XILINX_IO_MODULE_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxIntC),
    .class_init    = xlx_iom_class_init,
    .instance_init = xlx_iom_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_INTC },
        { }
    },
};

static void xlx_iom_register_types(void)
{
    type_register_static(&xlx_iom_info);
}

type_init(xlx_iom_register_types)
