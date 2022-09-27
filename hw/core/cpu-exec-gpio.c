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
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "sysemu/tcg.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "hw/irq.h"

#include "cpu.h"
#include "hw/core/cpu-exec-gpio.h"

static void cpu_set_off(CPUState *cpu, run_on_cpu_data data)
{
    assert(qemu_mutex_iothread_locked());

    cpu->halted = 1;
    cpu->exception_index = EXCP_HLT;

#ifdef TARGET_ARM
    ARM_CPU(cpu)->power_state = PSCI_OFF;
#endif
}

static void cpu_set_on(CPUState *cpu, run_on_cpu_data data)
{
    assert(qemu_mutex_iothread_locked());

    cpu->halted = 0;

#ifdef TARGET_ARM
    ARM_CPU(cpu)->power_state = PSCI_ON;
#endif
}

static void cpu_reset_enter(CPUState *cpu, run_on_cpu_data data)
{
#ifdef TARGET_ARM
    ARMCPU *arm_cpu = ARM_CPU(cpu);

    assert(qemu_mutex_iothread_locked());
    arm_cpu->is_in_wfi = false;
    qemu_set_irq(arm_cpu->wfi, 0);
#endif
}

static void cpu_reset_exit(CPUState *cpu, run_on_cpu_data data)
{
    /* Initialize the cpu we are turning on */
    cpu_reset(cpu);
}

static void cpu_exec_pin_update(CPUState *cpu, bool reset_pin)
{
    bool val = reset_pin || cpu->halt_pin || cpu->arch_halt_pin;
    bool async = runstate_is_running();

    /*
     * When the machine is running, we always queue the reset/halt actions
     * to run on the per-cpu thread.
     *
     * When the machine hasn't started yet, we can't do that because we'll
     * end up overriding settings done by the machine e.g device loader style
     * resets and start-powered-off.
     */
    if (val) {
        if (async) {
            async_run_on_cpu(cpu, cpu_set_off, RUN_ON_CPU_NULL);
        } else {
            cpu_interrupt(cpu, CPU_INTERRUPT_HALT);
        }
    } else {
        if (async) {
            async_run_on_cpu(cpu, cpu_set_on, RUN_ON_CPU_NULL);
        } else {
              /* Enabling the core here will override start-powered-off.  */
             cpu_reset_interrupt(cpu, CPU_INTERRUPT_HALT);
             cpu_interrupt(cpu, CPU_INTERRUPT_EXITTB);
        }
    }
}

void cpu_reset_gpio(void *opaque, int irq, int level)
{
    CPUState *cpu = CPU(opaque);
    static int lock_count;
    bool async = runstate_is_running();

    assert(qemu_mutex_iothread_locked());

    g_assert(lock_count == 0);
    lock_count++;
    if (level == cpu->reset_pin) {
        goto done;
    }

    /*
     * On hardware when the reset pin is asserted the CPU resets and stays
     * in reset until the pin is lowered. As we don't have a reset state, we
     * do it a little differently. If the reset_pin is being set high then
     * cpu_halt_update() will halt the CPU, but it isn't reset. Once the pin
     * is lowered we reset the CPU and then let it run, as long as no halt pin
     * is set. This avoids us having to double reset, which can cause issues
     * with MTTCG.
     *
     * On reset assert, must update, to io-domain, all those outputs
     * derived from the vCPU state, to satisfy assumption of CPU I/O
     * devices' assumption.
     *
     * Also, order of pin-state update is asymmetrical, depending on
     * assert of deassert.
     */

    cpu->reset_pin = level;
    if (level) {
        if (async) {
            async_run_on_cpu(cpu, cpu_reset_enter, RUN_ON_CPU_NULL);
        } else {
            cpu_reset_enter(cpu, RUN_ON_CPU_NULL);
        }
        cpu_exec_pin_update(cpu, cpu->reset_pin);
    } else {
        if (async) {
            async_run_on_cpu(cpu, cpu_reset_exit, RUN_ON_CPU_NULL);
        } else {
            cpu_reset_exit(cpu, RUN_ON_CPU_NULL);
        }
        cpu_exec_pin_update(cpu, cpu->reset_pin);
    }

done:
    lock_count--;
}

void cpu_halt_gpio(void *opaque, int irq, int level)
{
    CPUState *cpu = CPU(opaque);

    assert(qemu_mutex_iothread_locked());
    cpu->halt_pin = level;
    cpu_exec_pin_update(cpu, cpu->reset_pin); /* TBD: _sync not working */
}

void cpu_halt_update(CPUState *cpu)
{
    assert(qemu_mutex_iothread_locked());
    cpu_exec_pin_update(cpu, cpu->reset_pin); /* TBD: _sync not working */
}
