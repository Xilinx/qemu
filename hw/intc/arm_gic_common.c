/*
 * ARM GIC support - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "gic_internal.h"
#include "qapi/error.h"
#include "hw/fdt_generic_util.h"

#include "hw/fdt_generic_devices.h"

static void gic_pre_save(void *opaque)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);

    if (c->pre_save) {
        c->pre_save(s);
    }
}

static int gic_post_load(void *opaque, int version_id)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);

    if (c->post_load) {
        c->post_load(s);
    }
    return 0;
}

static const VMStateDescription vmstate_gic_irq_state = {
    .name = "arm_gic_irq_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(enabled, gic_irq_state),
        VMSTATE_UINT8(pending, gic_irq_state),
        VMSTATE_UINT8(active, gic_irq_state),
        VMSTATE_UINT8(level, gic_irq_state),
        VMSTATE_BOOL(model, gic_irq_state),
        VMSTATE_BOOL(edge_trigger, gic_irq_state),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gic = {
    .name = "arm_gic",
    .version_id = 8,
    .minimum_version_id = 8,
    .pre_save = gic_pre_save,
    .post_load = gic_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(enabled, GICState),
        VMSTATE_UINT32_ARRAY(ctrl, GICState, GIC_NCPU),
        VMSTATE_STRUCT_ARRAY(irq_state, GICState, GIC_MAXIRQ, 1,
                             vmstate_gic_irq_state, gic_irq_state),
        VMSTATE_UINT8_ARRAY(irq_target, GICState, GIC_MAXIRQ),
        VMSTATE_UINT8_2DARRAY(priority1, GICState, GIC_INTERNAL, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(priority2, GICState, GIC_MAXIRQ - GIC_INTERNAL),
        VMSTATE_UINT16_2DARRAY(last_active, GICState, GIC_MAXIRQ, GIC_NCPU),
        VMSTATE_UINT8_2DARRAY(sgi_pending, GICState, GIC_NR_SGIS, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(priority_mask, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(running_irq, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(running_priority, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(current_pending, GICState, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(bpr, GICState, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(abpr, GICState, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(apr, GICState, GIC_NR_APRS, GIC_NCPU),
        VMSTATE_UINT32_ARRAY(gich.hcr, GICState, GIC_N_REALCPU),
        VMSTATE_UINT32_ARRAY(gich.vtr, GICState, GIC_N_REALCPU),
        VMSTATE_UINT32_ARRAY(gich.misr, GICState, GIC_N_REALCPU),
        VMSTATE_UINT64_ARRAY(gich.eisr, GICState, GIC_N_REALCPU),
        VMSTATE_UINT64_ARRAY(gich.elrsr, GICState, GIC_N_REALCPU),
        VMSTATE_UINT32_ARRAY(gich.apr, GICState, GIC_N_REALCPU),
        VMSTATE_UINT32_2DARRAY(gich.lr, GICState, GIC_N_REALCPU, GICV_NR_LR),
        VMSTATE_UINT32_ARRAY(gich.pending_prio, GICState, GIC_N_REALCPU),
        VMSTATE_UINT8_ARRAY(gich.pending_lrn, GICState, GIC_N_REALCPU),
        VMSTATE_END_OF_LIST()
    }
};

void gic_init_irqs_and_mmio(GICState *s, qemu_irq_handler handler,
                            const MemoryRegionOps *ops)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    int i = s->num_irq - GIC_INTERNAL;

    /* For the GIC, also expose incoming GPIO lines for PPIs for each CPU.
     * GPIO array layout is thus:
     *  [0..N-1] SPIs
     *  [N..N+31] PPIs for CPU 0
     *  [N+32..N+63] PPIs for CPU 1
     *   ...
     */
    i += (GIC_INTERNAL * s->num_cpu);
    qdev_init_gpio_in(DEVICE(s), handler, i);

    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_virq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_vfiq[i]);
    }

    /* Distributor */
    memory_region_init_io(&s->iomem, OBJECT(s), ops, s, "gic_dist", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* This is the main CPU interface "for this core". It is always
     * present because it is required by both software emulation and KVM.
     */
    memory_region_init_io(&s->cpuiomem[0], OBJECT(s), ops ? &ops[1] : NULL,
                          s, "gic_cpu", s->revision == 2 ? 0x2000 : 0x100);
    sysbus_init_mmio(sbd, &s->cpuiomem[0]);
}

static void arm_gic_common_realize(DeviceState *dev, Error **errp)
{
    GICState *s = ARM_GIC_COMMON(dev);
    int num_irq = s->num_irq;

    if (!s->num_cpu) {
        s->num_cpu = fdt_generic_num_cpus;
    }

    if (s->num_cpu > GIC_NCPU) {
        error_setg(errp, "requested %u CPUs exceeds GIC maximum %d",
                   s->num_cpu, GIC_NCPU);
        return;
    }
    s->num_irq += GIC_BASE_IRQ;
    if (s->num_irq > GIC_MAXIRQ) {
        error_setg(errp,
                   "requested %u interrupt lines exceeds GIC maximum %d",
                   num_irq, GIC_MAXIRQ);
        return;
    }
    /* ITLinesNumber is represented as (N / 32) - 1 (see
     * gic_dist_readb) so this is an implementation imposed
     * restriction, not an architectural one:
     */
    if (s->num_irq < 32 || (s->num_irq % 32)) {
        error_setg(errp,
                   "%d interrupt lines unsupported: not divisible by 32",
                   num_irq);
        return;
    }
}

static void arm_gic_common_reset(DeviceState *dev)
{
    GICState *s = ARM_GIC_COMMON(dev);
    int i;
    memset(s->irq_state, 0, GIC_MAXIRQ * sizeof(gic_irq_state));
    for (i = 0 ; i < GIC_NCPU; i++) {
        if (s->revision == REV_11MPCORE) {
            s->priority_mask[i] = 0xf0;
        } else {
            s->priority_mask[i] = 0;
        }
        s->current_pending[i] = 1023;
        s->running_irq[i] = 1023;
        s->running_priority[i] = 0x100;
        s->ctrl[i] = 0;
    }
    for (i = 0; i < GIC_NR_SGIS; i++) {
        GIC_SET_ENABLED(i, ALL_CPU_MASK);
        GIC_SET_EDGE_TRIGGER(i);
    }
    if (s->num_cpu == 1) {
        /* For uniprocessor GICs all interrupts always target the sole CPU */
        for (i = 0; i < GIC_MAXIRQ; i++) {
            s->irq_target[i] = 1;
        }
    }
    if (!s->c_iidr) {
        s->c_iidr |= s->revision << 16;
        s->c_iidr |= 0x43B;
    }
    s->enabled = false;
}

static int arm_gic_common_fdt_get_irq(FDTGenericIntc *obj, qemu_irq *irqs,
                                      uint32_t *cells, int ncells, int max,
                                      Error **errp)
{
    GICState *gs = ARM_GIC_COMMON(obj);
    int cpu = 0;
    uint32_t idx;

    if (ncells != 3) {
        error_setg(errp, "ARM GIC requires 3 interrupt cells, %d cells given",
                   ncells);
        return 0;
    }
    idx = cells[1];

    switch (cells[0]) {
    case 0:
        if (idx >= gs->num_irq) {
            error_setg(errp, "ARM GIC SPI has maximum index of %" PRId32 ", "
                       "index %" PRId32 " given", gs->num_irq - 1, idx);
            return 0;
        }
        (*irqs) = qdev_get_gpio_in(DEVICE(obj), cells[1]);
        return 1;
    case 1: /* PPI */
        if (idx >= 16) {
            error_setg(errp, "ARM GIC PPI has maximum index of 15, "
                       "index %" PRId32 " given", idx);
            return 0;
        }
        for (cpu = 0; cpu < max && cpu < gs->num_cpu; cpu++) {
            if (cells[2] & 1 << (cpu + 8)) {
                *irqs = qdev_get_gpio_in(DEVICE(obj),
                                         gs->num_irq - 16 + idx + cpu * 32);
            }
            irqs++;
        }
        return cpu;
    default:
        error_setg(errp, "Invalid cell 0 value in interrupt binding: %d",
                   cells[0]);
        return 0;
    }
}

static Property arm_gic_common_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", GICState, num_cpu, 0),
    DEFINE_PROP_UINT32("num-irq", GICState, num_irq, 96),
    /* Revision can be 1 or 2 for GIC architecture specification
     * versions 1 or 2, or 0 to indicate the legacy 11MPCore GIC.
     */
    DEFINE_PROP_UINT32("revision", GICState, revision, 1),
    DEFINE_PROP_BOOL("disable-linux-gic-init", GICState,
                     disable_linux_gic_init, false),
    DEFINE_PROP_UINT32("map-stride", GICState, map_stride, 0x1000),
    /* We set this later if it isn't set */
    DEFINE_PROP_UINT32("int-id", GICState, c_iidr, 0),
    /* Xilinx: This is here for compatibility, but we never actually use it */
    DEFINE_PROP_BOOL("has-security-extensions", GICState, security_extn, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_gic_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericIntcClass *fgic = FDT_GENERIC_INTC_CLASS(klass);

    dc->reset = arm_gic_common_reset;
    dc->realize = arm_gic_common_realize;
    dc->props = arm_gic_common_properties;
    dc->vmsd = &vmstate_gic;
    fgic->get_irq = arm_gic_common_fdt_get_irq;
}

static const TypeInfo arm_gic_common_type = {
    .name = TYPE_ARM_GIC_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICState),
    .class_size = sizeof(ARMGICCommonClass),
    .class_init = arm_gic_common_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_INTC },
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&arm_gic_common_type);
}

type_init(register_types)
