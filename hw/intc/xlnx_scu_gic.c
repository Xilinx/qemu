/*
 * xlnx_scu_gic.c
 *
 *  Copyright (C) 2016 : GreenSocs
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
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
 *
 */

#include "qemu/osdep.h"
#include "hw/intc/xlnx_scu_gic.h"
#include "hw/fdt_generic_util.h"

/* This is an arm_gic with some error injection features. */

#define MAX_SPI (32 * 5)

static int xlnx_scu_gic_inject_error(XlnxSCUGICState *s, int irq, int level)
{
    bool inject;
    uint8_t i;

    assert(irq < MAX_SPI);
    inject = 0;
    for (i = 0; i < XLNX_SCU_GIC_MAX_INJECTOR; i++) {
        inject |= s->intr_inj[i][irq / 32] & (1 << irq % 32);
    }

    return ((level != 0) || inject);
}

static void xlnx_scu_gic_set_irq(void *opaque, int irq, int level)
{
    XlnxSCUGICState *s = XLNX_SCU_GIC(opaque);
    XlnxSCUGICClass *agc = XLNX_SCU_GIC_GET_CLASS(opaque);

    if (irq < MAX_SPI) {
        /* Just remember the level of this IRQ so we can compute later the new
         * level when we inject irq.
         */
        s->ext_level[irq / 32] = deposit32(s->ext_level[irq / 32], irq % 32, 1,
                                           level);

        level = xlnx_scu_gic_inject_error(s, irq, level);
    }

    agc->parent_irq_handler(opaque, irq, level);
}

static void xlnx_scu_gic_update_irq(XlnxSCUGICState *s, unsigned int reg)
{
    XlnxSCUGICClass *agc = XLNX_SCU_GIC_GET_CLASS(s);
    int irq, level;

    for (irq = reg * 32; irq < (reg + 1) * 32; irq++) {
        level = xlnx_scu_gic_inject_error(s, irq,
                                          s->ext_level[irq / 32] &
                                              (1 << irq % 32));
        agc->parent_irq_handler(s, irq, level);
    }
}

void xlnx_scu_gic_set_intr(XlnxSCUGICState *s, unsigned int reg, uint32_t val,
                           uint8_t injector)
{
    assert(reg < XLNX_SCU_GIC_IRQ_REG);

    s->intr_inj[injector][reg] = val;
    xlnx_scu_gic_update_irq(s, reg);
}

static void xlnx_scu_gic_class_init(ObjectClass *klass, void *data)
{
    ARMGICClass *agc = ARM_GIC_CLASS(klass);
    XlnxSCUGICClass *xsgc = XLNX_SCU_GIC_CLASS(klass);

    xsgc->parent_irq_handler = agc->irq_handler;
    agc->irq_handler = xlnx_scu_gic_set_irq;
}

static const TypeInfo xlnx_scu_gic_info = {
    .name = TYPE_XLNX_SCU_GIC,
    .parent = TYPE_ARM_GIC,
    .instance_size = sizeof(XlnxSCUGICState),
    .class_init = xlnx_scu_gic_class_init,
    .class_size = sizeof(XlnxSCUGICClass),
};

static void xlnx_scu_gic_register_types(void)
{
    type_register_static(&xlnx_scu_gic_info);
}

type_init(xlnx_scu_gic_register_types)
