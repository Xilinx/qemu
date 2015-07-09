/*
 * QEMU MicroBlaze CPU
 *
 * Copyright (c) 2009 Edgar E. Iglesias
 * Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 * Copyright (c) 2009 Edgar E. Iglesias, Axis Communications AB.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "qemu-common.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/sysemu.h"

#ifndef CONFIG_USER_ONLY
#include "hw/fdt_generic_util.h"
#endif

static void mb_cpu_set_pc(CPUState *cs, vaddr value)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);

    cpu->env.sregs[SR_PC] = value;
}

static vaddr mb_cpu_get_pc(CPUState *cs)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);

    return cpu->env.sregs[SR_PC];
}

static bool mb_cpu_has_work(CPUState *cs)
{
    CPUMBState *env = cs->env_ptr;
    bool r;

    r = (cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI))
           || env->wakeup;

    return r;
}

#ifndef CONFIG_USER_ONLY
static void microblaze_cpu_set_irq(void *opaque, int irq, int level)
{
    MicroBlazeCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    int type = irq ? CPU_INTERRUPT_NMI : CPU_INTERRUPT_HARD;

    if (level) {
        cpu_interrupt(cs, type);
    } else {
        cpu_reset_interrupt(cs, type);
    }
}

static void microblaze_set_wakeup(void *opaque, int irq, int level)
{
    MicroBlazeCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUMBState *env = &cpu->env;

    env->wakeup &= ~(1 << irq);
    if (level) {
        qemu_set_irq(cpu->mb_sleep, false);
        env->wakeup |= 1 << irq;
        cs->halted = 0;
        qemu_cpu_kick(cs);
    }
}
#endif

/* CPUClass::reset() */
static void mb_cpu_reset(CPUState *s)
{
    CPUClass *cc = CPU_GET_CLASS(s);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(s);
    CPUClass *cc_parent = CPU_CLASS(MICROBLAZE_CPU_PARENT_CLASS);
    CPUMBState *env = &cpu->env;
    vaddr old_pc = cc->get_pc(s);

    cc_parent->reset(s);

    /* FIXME: FIX the dtb options for resets.  */
    if (env->pvr.regs[0] == 0) {
    memset(env, 0, sizeof(CPUMBState));
    }
    env->res_addr = RES_ADDR_NONE;
    tlb_flush(s, 1);

    /* Disable stack protector.  */
    env->shr = ~0;

    /* FIXME: FIX the dtb options for resets.  */
    if (env->pvr.regs[0] == 0) {
    env->pvr.regs[0] = PVR0_PVR_FULL_MASK \
                       | PVR0_USE_BARREL_MASK \
                       | PVR0_USE_DIV_MASK \
                       | PVR0_USE_HW_MUL_MASK \
                       | PVR0_USE_EXC_MASK \
                       | PVR0_USE_ICACHE_MASK \
                       | PVR0_USE_DCACHE_MASK \
                       | PVR0_USE_MMU \
                       | (0xb << 8);
    env->pvr.regs[2] = PVR2_D_OPB_MASK \
                        | PVR2_D_LMB_MASK \
                        | PVR2_I_OPB_MASK \
                        | PVR2_I_LMB_MASK \
                        | PVR2_USE_MSR_INSTR \
                        | PVR2_USE_PCMP_INSTR \
                        | PVR2_USE_BARREL_MASK \
                        | PVR2_USE_DIV_MASK \
                        | PVR2_USE_HW_MUL_MASK \
                        | PVR2_USE_MUL64_MASK \
                        | PVR2_USE_FPU_MASK \
                        | PVR2_USE_FPU2_MASK \
                        | PVR2_FPU_EXC_MASK \
                        | 0;
    env->pvr.regs[10] = 0x0c000000; /* Default to spartan 3a dsp family.  */
    env->pvr.regs[11] = PVR11_USE_MMU | (16 << 17);

    env->pvr.regs[0] |= (cpu->cfg.stackproc ? PVR0_SPROT_MASK : 0);
    }

    env->sregs[SR_PC] = cpu->base_vectors;

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->sregs[SR_MSR] = MSR_EE | MSR_IE | MSR_VM | MSR_UM;
    env->pvr.regs[10] = 0x0c000000; /* Spartan 3a dsp.  */
#else
    env->sregs[SR_MSR] = 0;
    mmu_init(&env->mmu);
    env->mmu.c_mmu = 3;
    env->mmu.c_mmu_tlb_access = 3;
    env->mmu.c_mmu_zones = 16;
#endif
    if (!runstate_is_running()) {
        cc->set_pc(s, old_pc);
    }
}

static void mb_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    DeviceClass *dc_parent = DEVICE_CLASS(MICROBLAZE_CPU_PARENT_CLASS);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    dc_parent->realize(dev, errp);
}

static void mb_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(obj);
    CPUMBState *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(env);

    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);

#ifndef CONFIG_USER_ONLY
    /* Inbound IRQ and FIR lines */
    qdev_init_gpio_in(DEVICE(cpu), microblaze_cpu_set_irq, 2);
    qdev_init_gpio_in_named(DEVICE(cpu), microblaze_set_wakeup, "wakeup", 2);

    qdev_init_gpio_out_named(DEVICE(cpu), &cpu->mb_sleep, "mb_sleep", 1);
#endif

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        mb_tcg_init();
    }
}

static const VMStateDescription vmstate_mb_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static Property mb_properties[] = {
    DEFINE_PROP_UINT32("base-vectors", MicroBlazeCPU, base_vectors, 0),
    DEFINE_PROP_BOOL("use-stack-protection", MicroBlazeCPU, cfg.stackproc,
                     false),
    DEFINE_PROP_END_OF_LIST(),
};

static const FDTGenericGPIOSet mb_ctrl_gpios[] = {
    {
      .names = &fdt_generic_gpio_name_set_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "wakeup", .fdt_index = 0, .range = 2 },
        { .name = "mb_sleep", .fdt_index = 2 },
        { },
      },
    },
    { },
};

static void mb_cpu_class_init(ObjectClass *oc, void *data)
{
#ifndef CONFIG_USER_ONLY
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(oc);
#endif
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    dc->realize = mb_cpu_realizefn;

    cc->reset = mb_cpu_reset;

    cc->has_work = mb_cpu_has_work;
    cc->do_interrupt = mb_cpu_do_interrupt;
    cc->cpu_exec_interrupt = mb_cpu_exec_interrupt;
    cc->dump_state = mb_cpu_dump_state;
    cc->set_pc = mb_cpu_set_pc;
    cc->get_pc = mb_cpu_get_pc;
    cc->gdb_read_register = mb_cpu_gdb_read_register;
    cc->gdb_write_register = mb_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = mb_cpu_handle_mmu_fault;
#else
    cc->do_unassigned_access = mb_cpu_unassigned_access;
    cc->get_phys_page_debug = mb_cpu_get_phys_page_debug;
#endif
    dc->vmsd = &vmstate_mb_cpu;
    dc->props = mb_properties;
    cc->gdb_num_core_regs = 32 + 5;

#ifndef CONFIG_USER_ONLY
    fggc->controller_gpios = mb_ctrl_gpios;
#endif
}

static const TypeInfo mb_cpu_type_info = {
    .name = TYPE_MICROBLAZE_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(MicroBlazeCPU),
    .instance_init = mb_cpu_initfn,
    .class_init = mb_cpu_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { }
    },
};

static void mb_cpu_register_types(void)
{
    type_register_static(&mb_cpu_type_info);
}

type_init(mb_cpu_register_types)
