/*
 * xlnx_scu_gic.h
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

#ifndef XLNX_SCU_GIC_H
#define XLNX_SCU_GIC_H

#include "arm_gic.h"

#define TYPE_XLNX_SCU_GIC "xlnx,zynqmp-scugic"

#define XLNX_SCU_GIC(obj) \
     OBJECT_CHECK(XlnxSCUGICState, (obj), TYPE_XLNX_SCU_GIC)
#define XLNX_SCU_GIC_CLASS(klass) \
     OBJECT_CLASS_CHECK(XlnxSCUGICClass, (klass), TYPE_XLNX_SCU_GIC)
#define XLNX_SCU_GIC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(XlnxSCUGICClass, (obj), TYPE_XLNX_SCU_GIC)

#define XLNX_SCU_GIC_MAX_INJECTOR 2
#define XLNX_SCU_GIC_IRQ_REG 5

struct XlnxSCUGICState {
    /*< private >*/
    GICState parent;

    /*< public >*/
    uint32_t intr_inj[XLNX_SCU_GIC_MAX_INJECTOR][XLNX_SCU_GIC_IRQ_REG];
    uint32_t ext_level[XLNX_SCU_GIC_IRQ_REG];
};

typedef struct XlnxSCUGICState XlnxSCUGICState;

struct XlnxSCUGICClass {
    /*< private >*/
    ARMGICClass parent_class;

    /*< public >*/
    qemu_irq_handler parent_irq_handler;
};

typedef struct XlnxSCUGICClass XlnxSCUGICClass;

void xlnx_scu_gic_set_intr(XlnxSCUGICState *s, unsigned int reg, uint32_t val,
                           uint8_t injector);

#endif /* XLNX_SCU_GIC_H */
