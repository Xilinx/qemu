/*
 * ARM gdb server stub: AArch64 specific functions.
 *
 * Copyright (c) 2013 SUSE LINUX Products GmbH
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
#include "cpu.h"
#include "exec/gdbstub.h"
#include "internals.h"

#ifndef CONFIG_USER_ONLY

/* FIXME: This should be generalized and moved into helper.c */
static void map_a32_to_a64_regs(CPUARMState *env)
{
    unsigned int i;

    for (i = 0; i < 13; i++) {
        env->xregs[i] = env->regs[i];
    }
    env->xregs[13] = env->banked_r13[bank_number(ARM_CPU_MODE_USR)];
    env->xregs[14] = env->banked_r14[bank_number(ARM_CPU_MODE_USR)];

    for (i = 0; i < ARRAY_SIZE(env->fiq_regs); i++) {
        env->xregs[i + 24] = env->fiq_regs[i];
    }
    env->xregs[29] = env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)];
    env->xregs[30] = env->banked_r14[bank_number(ARM_CPU_MODE_FIQ)];

    /* HAX!  */
    env->xregs[31] = env->regs[13];

    env->pc = env->regs[15];
    pstate_write(env, env->spsr | (1 << 4));
}

static void map_a64_to_a32_regs(CPUARMState *env)
{
    unsigned int i = 0;

    for (i = 0; i < 13; i++) {
        env->regs[i] = env->xregs[i];
    }
    env->banked_r13[bank_number(ARM_CPU_MODE_USR)] = env->xregs[13];
    env->banked_r14[bank_number(ARM_CPU_MODE_USR)] = env->xregs[14];

    for (i = 0; i < ARRAY_SIZE(env->usr_regs); i++) {
        env->fiq_regs[i] = env->xregs[i + 24];
    }
    env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[29];
    env->banked_r14[bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[30];

    env->regs[15] = env->pc;
}

#endif

int aarch64_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

#ifndef CONFIG_USER_ONLY
    if (!is_a64(env)) {
        map_a32_to_a64_regs(env);
    }
#endif

    if (n < 31) {
        /* Core integer register.  */
        return gdb_get_reg64(mem_buf, env->xregs[n]);
    }
    switch (n) {
    case 31:
    {
        unsigned int cur_el = arm_current_el(env);
        uint64_t sp;

        aarch64_save_sp(env, cur_el);
        switch (env->debug_ctx) {
            case DEBUG_EL0:
                sp = env->sp_el[0];
                break;
            case DEBUG_EL1:
                sp = env->sp_el[1];
                break;
            case DEBUG_EL2:
                sp = env->sp_el[2];
                break;
            case DEBUG_EL3:
                sp = env->sp_el[3];
                break;
            default:
                sp = env->xregs[31];
                break;
        }
        return gdb_get_reg64(mem_buf, sp);
    }
    case 32:
        return gdb_get_reg64(mem_buf, env->pc);
    case 33:
        return gdb_get_reg32(mem_buf, pstate_read(env));
    }
    /* Unknown register.  */
    return 0;
}

int aarch64_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint64_t tmp;
    int rlen = 0;

#ifndef CONFIG_USER_ONLY
    if (!is_a64(env)) {
        map_a32_to_a64_regs(env);
    }
#endif

    tmp = ldq_p(mem_buf);

    if (n < 31) {
        /* Core integer register.  */
        env->xregs[n] = tmp;
        rlen = 8;
    }
    switch (n) {
    case 31: {
        unsigned int cur_el = arm_current_el(env);

        aarch64_save_sp(env, cur_el);
        switch (env->debug_ctx) {
            case DEBUG_EL0:
                env->sp_el[0] = tmp;
                break;
            case DEBUG_EL1:
                env->sp_el[1] = tmp;
                break;
            case DEBUG_EL2:
                env->sp_el[2] = tmp;
                break;
            case DEBUG_EL3:
                env->sp_el[3] = tmp;
                break;
            default:
                env->xregs[31] = tmp;
                break;
        }
        aarch64_restore_sp(env, cur_el);
        rlen = 8;
        break;
    }
    case 32:
        env->pc = tmp;
        rlen = 8;
        break;
    case 33:
        /* CPSR */
        pstate_write(env, tmp);
        rlen = 4;
        break;
    }

#ifndef CONFIG_USER_ONLY
    if (!is_a64(env)) {
        map_a64_to_a32_regs(env);
    }
#endif

    /* Unknown register.  */
    return rlen;
}
