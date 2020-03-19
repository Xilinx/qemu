/*
 * This file contains implementation of GIC Proxy component.
 *
 * 2014 Aggios, Inc.
 *
 * Written by Strahinja Jankovic <strahinja.jankovic@aggios.com>
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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "qemu/bitops.h"
#include "qemu/log.h"

#include "hw/fdt_generic_util.h"

#ifndef GIC_PROXY_ERR_DEBUG
#define GIC_PROXY_ERR_DEBUG 0
#endif

#define TYPE_XILINX_GIC_PROXY "xlnx.zynqmp-gicp"

#define XILINX_GIC_PROXY(obj) \
     OBJECT_CHECK(GICProxy, (obj), TYPE_XILINX_GIC_PROXY)

#define MAX_INTS            160
#define GICP_GROUPS         5
#define GICP_GROUP_STRIDE   0x14

REG32(GICP0_IRQ_STATUS, 0x0)
REG32(GICP0_IRQ_MASK, 0x4)
REG32(GICP0_IRQ_ENABLE, 0x8)
REG32(GICP0_IRQ_DISABLE, 0xc)
REG32(GICP0_IRQ_TRIGGER, 0x10)
    #define R_GICP0_RSVD    0x000000ff
    #define R_GICP1_RSVD    0
    #define R_GICP2_RSVD    0
    #define R_GICP3_RSVD    0x000000ff
    #define R_GICP4_RSVD    0xf0000000
REG32(GICP_PMU_IRQ_STATUS, 0xa0)
REG32(GICP_PMU_IRQ_MASK, 0xa4)
REG32(GICP_PMU_IRQ_ENABLE, 0xa8)
REG32(GICP_PMU_IRQ_DISABLE, 0xac)
REG32(GICP_PMU_IRQ_TRIGGER, 0xb0)

#define R_MAX (R_GICP_PMU_IRQ_TRIGGER + 1)

typedef struct GICProxy {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t pinState[GICP_GROUPS];
    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} GICProxy;

/* Mask and status registers are needed for checking if interrupt
 * needs to be triggered.
 */
#define GICPN_STATUS_REG(n) ((A_GICP0_IRQ_STATUS + \
                             (n) * GICP_GROUP_STRIDE) >> 2)
#define GICPN_MASK_REG(n)   ((A_GICP0_IRQ_MASK + \
                             (n) * GICP_GROUP_STRIDE) >> 2)

static void gicp_update_irq(void *opaque)
{
    GICProxy *s = XILINX_GIC_PROXY(opaque);
    bool pending = s->regs[R_GICP_PMU_IRQ_STATUS] &
                   ~s->regs[R_GICP_PMU_IRQ_MASK];

    qemu_set_irq(s->irq, pending);
}

/* Functions for handling changes to top level interrupt registers. */

static void gicp_update(void *opaque, uint8_t nr)
{
    GICProxy *s = XILINX_GIC_PROXY(opaque);

    if (s->regs[GICPN_STATUS_REG(nr)] & ~s->regs[GICPN_MASK_REG(nr)]) {
        s->regs[R_GICP_PMU_IRQ_STATUS] |= 1 << nr;
    } else {
        s->regs[R_GICP_PMU_IRQ_STATUS] &= ~(1 << nr);
    }
    gicp_update_irq(s);
}

static void gicp_status_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    unsigned int i;

    for (i = 0; i < GICP_GROUPS; i++) {
        gicp_update(s, i);
    }
}

static void gicp_enable_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint32_t val = val64;

    s->regs[R_GICP_PMU_IRQ_MASK] &= ~val;
    gicp_update_irq(s);
}

static void gicp_disable_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint32_t val = val64;

    s->regs[R_GICP_PMU_IRQ_MASK] |= val;
    gicp_update_irq(s);
}

static void gicp_trigger_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint32_t val = val64;

    s->regs[R_GICP_PMU_IRQ_STATUS] |= val;
    gicp_update_irq(s);
}

/* Functions for handling changes to each interrupt register. */

static void gicpn_status_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint64_t nr = reg->access->addr / GICP_GROUP_STRIDE;

    s->regs[GICPN_STATUS_REG(nr)] |= s->pinState[nr];
    gicp_update(s, nr);
}

static void gicpn_enable_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint32_t val = val64;
    uint64_t nr = reg->access->addr / GICP_GROUP_STRIDE;

    s->regs[GICPN_MASK_REG(nr)] &= ~val;
    gicp_update(s, nr);
}

static void gicpn_disable_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint32_t val = val64;
    uint64_t nr = reg->access->addr / GICP_GROUP_STRIDE;

    s->regs[GICPN_MASK_REG(nr)] |= val;
    gicp_update(s, nr);
}

static void gicpn_trigger_postw(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    uint32_t val = val64;
    uint64_t nr = reg->access->addr / GICP_GROUP_STRIDE;

    s->regs[GICPN_STATUS_REG(nr)] |= val;
    gicp_update(s, nr);
}

/* Return 0 and log if reading from write-only register. */
static uint64_t gicp_wo_postr(RegisterInfo *reg, uint64_t val64)
{
    GICProxy *s = XILINX_GIC_PROXY(reg->opaque);
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Reading from wo register at %" HWADDR_PRIx "\n",
                  object_get_canonical_path(OBJECT(s)),
                  reg->access->addr);
    return 0;
}

/* Read-as-zero high 24 bits. */
static uint64_t gicp_raz_hi24_postr(RegisterInfo *reg, uint64_t val64)
{
    return val64 & 0xff;
}

static RegisterAccessInfo gic_proxy_regs_info[] = {
#define GICPN_REG_DEFS(n) \
    {   .name = "GICP" #n "_IRQ_STATUS",                            \
        .addr = A_GICP0_IRQ_STATUS + n * GICP_GROUP_STRIDE,  \
        .w1c = 0xffffffff,                                          \
        .rsvd = R_GICP ## n ## _RSVD,                               \
        .post_write = gicpn_status_postw,                           \
    },{ .name = "GICP" #n "_IRQ_MASK",                              \
        .addr = A_GICP0_IRQ_MASK + n * GICP_GROUP_STRIDE,    \
        .ro = 0xffffffff,                                           \
        .rsvd = R_GICP ## n ## _RSVD,                               \
        .reset = 0xffffffff,                                        \
    },{ .name = "GICP" #n "_IRQ_ENABLE",                            \
        .addr = A_GICP0_IRQ_ENABLE + n * GICP_GROUP_STRIDE,  \
        .rsvd = R_GICP ## n ## _RSVD,                               \
        .post_read = gicp_wo_postr,                                 \
        .post_write = gicpn_enable_postw,                           \
    },{ .name = "GICP" #n "_IRQ_DISABLE",                           \
        .addr = A_GICP0_IRQ_DISABLE + n * GICP_GROUP_STRIDE, \
        .rsvd = R_GICP ## n ## _RSVD,                               \
        .post_read = gicp_wo_postr,                                 \
        .post_write = gicpn_disable_postw,                          \
    },{ .name = "GICP" #n "_IRQ_TRIGGER",                           \
        .addr = A_GICP0_IRQ_TRIGGER + n * GICP_GROUP_STRIDE, \
        .rsvd = R_GICP ## n ## _RSVD,                               \
        .post_read = gicp_wo_postr,                                 \
        .post_write = gicpn_trigger_postw,                          \
    }
    GICPN_REG_DEFS(0),
    GICPN_REG_DEFS(1),
    GICPN_REG_DEFS(2),
    GICPN_REG_DEFS(3),
    GICPN_REG_DEFS(4),
      { .name = "GICP_PMU_IRQ_STATUS",  .addr = A_GICP_PMU_IRQ_STATUS,
        .w1c = 0x000000ff,
        .rsvd = 0xffffffe0,
        .post_read = gicp_raz_hi24_postr,
        .post_write = gicp_status_postw,
    },{ .name = "GICP_PMU_IRQ_MASK",  .addr = A_GICP_PMU_IRQ_MASK,
        .ro = 0x000000ff,
        .rsvd = 0xffffffe0,
        .reset = 0x000000ff,
        .post_read = gicp_raz_hi24_postr,
    },{ .name = "GICP_PMU_IRQ_ENABLE",  .addr = A_GICP_PMU_IRQ_ENABLE,
        .rsvd = 0xffffffe0,
        .post_read = gicp_wo_postr,
        .post_write = gicp_enable_postw,
    },{ .name = "GICP_PMU_IRQ_DISABLE",  .addr = A_GICP_PMU_IRQ_DISABLE,
        .rsvd = 0xffffffe0,
        .post_read = gicp_wo_postr,
        .post_write = gicp_disable_postw,
    },{ .name = "GICP_PMU_IRQ_TRIGGER",  .addr = A_GICP_PMU_IRQ_TRIGGER,
        .rsvd = 0xffffffe0,
        .post_read = gicp_wo_postr,
        .post_write = gicp_trigger_postw,
    }
};

static void gic_proxy_reset(DeviceState *dev)
{
    GICProxy *s = XILINX_GIC_PROXY(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static const MemoryRegionOps gic_proxy_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void gic_proxy_set_irq(void *opaque, int irq, int level)
{
    GICProxy *s = XILINX_GIC_PROXY(opaque);
    int group = irq / 32;
    int bit = irq % 32;

    if (level) {
        s->pinState[group] |= 1 << bit;
    } else {
        s->pinState[group] &= ~(1 << bit);
    }
    s->regs[GICPN_STATUS_REG(group)] |= s->pinState[group];
    gicp_update(s, group);
}

static void gic_proxy_init(Object *obj)
{
    GICProxy *s = XILINX_GIC_PROXY(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(s);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_GIC_PROXY, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), gic_proxy_regs_info,
                              ARRAY_SIZE(gic_proxy_regs_info),
                              s->regs_info, s->regs,
                              &gic_proxy_ops,
                              GIC_PROXY_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);

    /* IRQ grouping:
     * [0..31] - GICP0
     * [32..63] - GICP1
     * [64..95] - GICP2
     * [96..127] - GICP3
     * [128..159] - GICP4
     */
    qdev_init_gpio_in(dev, gic_proxy_set_irq, MAX_INTS);
    qdev_init_gpio_out_named(dev, &s->irq, "gicp-irq", 1);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static int gic_proxy_get_irq(FDTGenericIntc *obj, qemu_irq *irqs,
                          uint32_t *cells, int ncells, int max,
                          Error **errp)
{
    int idx;

    if (ncells != 3) {
        error_setg(errp, "Xilinx GIC Proxy requires 3 interrupt cells, "
                   "%d cells given", ncells);
        return 0;
    }
    idx = cells[1];

    switch (cells[0]) {
    case 0:
        if (idx >= MAX_INTS) {
            error_setg(errp, "Xilinx GIC Proxy has maximum index of %" PRId32
                       ", index %" PRId32 " given", MAX_INTS - 1, idx);
            return 0;
        }
        (*irqs) = qdev_get_gpio_in(DEVICE(obj), cells[1]);
        return 1;
    default:
        error_setg(errp, "Invalid cell 0 value in interrupt binding: %d",
                   cells[0]);
        return 0;
    }
};

static const VMStateDescription vmstate_gic_proxy = {
    .name = TYPE_XILINX_GIC_PROXY,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, GICProxy, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const FDTGenericGPIOSet gic_proxy_client_gpios[] = {
    {
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection[]) {
            { .name = "gicp-irq",         .fdt_index = 0 },
            { },
        },
    },
    { },
};

static void gic_proxy_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    FDTGenericIntcClass *fgic = FDT_GENERIC_INTC_CLASS(oc);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(oc);

    dc->reset = gic_proxy_reset;
    dc->vmsd = &vmstate_gic_proxy;
    fgic->get_irq = gic_proxy_get_irq;
    fggc->client_gpios = gic_proxy_client_gpios;
}

static const TypeInfo gic_proxy_info = {
    .name          = TYPE_XILINX_GIC_PROXY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICProxy),
    .instance_init = gic_proxy_init,
    .class_init    = gic_proxy_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_INTC },
        { TYPE_FDT_GENERIC_GPIO },
        { },
    },
};

static void gic_proxy_register_types(void)
{
    type_register_static(&gic_proxy_info);
}

type_init(gic_proxy_register_types)
