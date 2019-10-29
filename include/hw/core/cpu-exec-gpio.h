/*
 *  Model Change of CPU Run-State by Wire
 *
 *  Copyright (c) 2019 Xilinx Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_CORE_CPU_EXEC_GPIO_H
#define HW_CORE_CPU_EXEC_GPIO_H

#include "hw/core/cpu.h"

void cpu_halt_gpio(void *opaque, int irq, int level);
void cpu_reset_gpio(void *opaque, int irq, int level);
void cpu_halt_update(CPUState *cpu);

#endif
