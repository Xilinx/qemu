/*
 * QEMU AArch64 CPU
 *
 * Copyright (c) 2013 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */
#include "qemu/module.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"


#ifndef CONFIG_USER_ONLY
static uint64_t dsu_clustercfr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t r;

    r = cpu->core_count - 1;

    /* Have L3, SCU L3, ACP and Periph port.  */
    r |= 1 << 12;
    r |= 1 << 11;
    r |= 1 << 8;
    r |= 1 << 4;

    /* Split mode only.  */
    r |= 1 << 30;

    /* One thread per core.  */
    r |= (cpu->core_count - 1) << 24;

    return r;
}

static uint64_t dsu_clusterpwrstat_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    /* FIXME: Do we need to wire these to power controller?  */
    return env->cp15.dsu.clusterpwrdn | 0xf0;
}

static void dsu_clusterectrl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    env->cp15.dsu.clusterectrl = value & 0x479f;
}

static void dsu_clusterpwrctrl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    env->cp15.dsu.clusterpwrctrl = value & 0xf7;
}

static void dsu_clusterpwrdn_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    env->cp15.dsu.clusterpwrdn = value & 0x3;
}

static void dsu_clusterthreadsidovr_write(CPUARMState *env,
                                          const ARMCPRegInfo *ri,
                                          uint64_t value)
{
    env->cp15.dsu.clusterthreadsidovr = value & 0x70007;
}
#endif

static const ARMCPRegInfo dsu_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "CLUSTERCFR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_R, .readfn = dsu_clustercfr_read },
    { .name = "CLUSTERIDR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 1,
      .type = ARM_CP_CONST, .access = PL1_R, .resetvalue = 0x11 }, /* r1p1 */
    { .name = "CLUSTERREVIDR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 2,
      .type = ARM_CP_CONST, .access = PL1_R },
    { .name = "CLUSTERACTRL_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 3,
      .type = ARM_CP_NO_RAW, .access = PL1_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "CLUSTERECTRL_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 4,
      .access = PL1_RW, .writefn = dsu_clusterectrl_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dsu.clusterectrl) },
    { .name = "CLUSTERPWRCTRL_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 5,
      .access = PL1_RW, .writefn = dsu_clusterpwrctrl_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dsu.clusterpwrctrl) },
    { .name = "CLUSTERPWRDN_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 6,
      .access = PL1_RW, .writefn = dsu_clusterpwrdn_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dsu.clusterpwrdn) },
    { .name = "CLUSTERPWRSTAT_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 3, .opc2 = 7,
      .type = ARM_CP_NO_RAW, .access = PL1_R,
      .readfn = dsu_clusterpwrstat_read },

    { .name = "CLUSTERTHREADSID_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "CLUSTERACPSID_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "CLUSTERSTASHSID_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "CLUSTERPARTCR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 3,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.dsu.clusterpartcr) },
    { .name = "CLUSTERBUSQOS_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 4,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.dsu.clusterbusqos) },
    { .name = "CLUSTERL3HIT_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL1_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "CLUSTERL3MISS_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 6,
      .type = ARM_CP_NO_RAW, .access = PL1_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "CLUSTERTHREADSIDOVR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 4, .opc2 = 7,
      .access = PL1_RW, .writefn = dsu_clusterthreadsidovr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dsu.clusterthreadsidovr) },
#endif
    REGINFO_SENTINEL
};

#ifndef CONFIG_USER_ONLY
static uint64_t a57_a53_l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /* Number of cores is in [25:24]; otherwise we RAZ */
    return (cpu->core_count - 1) << 24;
}
#endif

static const ARMCPRegInfo cortex_a72_a57_a53_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "L2CTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
    { .name = "L2CTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
#endif
    { .name = "L2ECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ECTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ACTLR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR",
      .cp = 15, .opc1 = 0, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUECTLR",
      .cp = 15, .opc1 = 1, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUMERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUMERRSR",
      .cp = 15, .opc1 = 2, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "L2MERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2MERRSR",
      .cp = 15, .opc1 = 3, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void aarch64_a57_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a57";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_MPIDR);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_AUXCR);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A57;
    cpu->midr = 0x411fd070;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10101105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

static void aarch64_a53_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a53";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_MPIDR);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_AUXCR);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A53;
    cpu->midr = 0x410fd034;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->reset_sctlr = 0x00c50838;
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10101105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001122; /* 40 bit physical addr */
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1024KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);

    /* Xilinx FIXUPs.  */
    /* These indicate the BP hardening and KPTI aren't needed.  */
    cpu->isar.id_aa64pfr0 |= (uint64_t)1 << 56; /* BP.  */
    cpu->isar.id_aa64pfr0 |= (uint64_t)1 << 60; /* KPTI.  */
}

static void aarch64_a72_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a72";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fd083;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034080;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1MB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);

    /* Xilinx FIXUPs.  */
    /* These indicate the BP hardening and KPTI aren't needed.  */
    cpu->isar.id_aa64pfr0 |= (uint64_t)1 << 56; /* BP.  */
    cpu->isar.id_aa64pfr0 |= (uint64_t)1 << 60; /* KPTI.  */
}

static void aarch64_a78_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint64_t t;

    cpu->dtb_compatible = "arm,cortex-a78";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fd421;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034080;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;

    /* Xilinx: Overrides since some of the new stuff does not work.  */
    cpu->isar.id_pfr0 = 0x00000131;
    t = cpu->isar.id_aa64pfr0;
    t = FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, FP, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1);
    cpu->isar.id_aa64pfr0 = t;

    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02122211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00010142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0x00000010;
    /* TOP Bit zero until we implement RAS.  */
    cpu->isar.id_aa64pfr0 = 0x01111112;
    cpu->isar.id_aa64pfr1 = 0x00000010;
    /* cpu->isar.id_aa64dfr0 = 0x110305408ULL; Unsupported PMcnt features */
    cpu->isar.id_aa64dfr0 = 0x10305408ULL;
    cpu->isar.id_aa64isar0 = 0x0010100010211120ULL;

    /* Xilinx: Overrides since some of the new stuff does not work.  */
    cpu->isar.id_aa64isar1 = 0x01200031;
    t = cpu->isar.id_aa64isar0;
    t = FIELD_DP64(t, ID_AA64ISAR0, AES, 2); /* AES + PMULL */
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2); /* SHA512 */
    t = FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 0);
    t = FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, DP, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);
    t = FIELD_DP64(t, ID_AA64ISAR0, TS, 2); /* v8.5-CondM */
    t = FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);
    cpu->isar.id_aa64isar0 = t;

    t = cpu->isar.id_aa64isar1;
    t = FIELD_DP64(t, ID_AA64ISAR1, DPB, 2);
    t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 0);
    t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 0);
    t = FIELD_DP64(t, ID_AA64ISAR1, APA, 0); /* PAuth, architected only */
    t = FIELD_DP64(t, ID_AA64ISAR1, API, 0);
    t = FIELD_DP64(t, ID_AA64ISAR1, GPA, 0);
    t = FIELD_DP64(t, ID_AA64ISAR1, GPI, 0);
    t = FIELD_DP64(t, ID_AA64ISAR1, SB, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, LRCPC, 0); /* ARMv8.4-RCPC */
    cpu->isar.id_aa64isar1 = t;

    cpu->isar.id_aa64mmfr0 = 0x000101125;
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x10400023;
    cpu->ccsidr[0] = 0x701fe01a; /* 64KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe01a; /* 64KB L1 icache */
    cpu->ccsidr[2] = 0x707fe03a; /* 512K L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
    define_arm_cp_regs(cpu, dsu_cp_reginfo);

    /* Xilinx FIXUPs.  */
    /* These indicate the BP hardening and KPTI aren't needed.  */
    cpu->isar.id_aa64pfr0 |= (uint64_t)1 << 56; /* BP.  */
    cpu->isar.id_aa64pfr0 |= (uint64_t)1 << 60; /* KPTI.  */
}

void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp)
{
    /*
     * If any vector lengths are explicitly enabled with sve<N> properties,
     * then all other lengths are implicitly disabled.  If sve-max-vq is
     * specified then it is the same as explicitly enabling all lengths
     * up to and including the specified maximum, which means all larger
     * lengths will be implicitly disabled.  If no sve<N> properties
     * are enabled and sve-max-vq is not specified, then all lengths not
     * explicitly disabled will be enabled.  Additionally, all power-of-two
     * vector lengths less than the maximum enabled length will be
     * automatically enabled and all vector lengths larger than the largest
     * disabled power-of-two vector length will be automatically disabled.
     * Errors are generated if the user provided input that interferes with
     * any of the above.  Finally, if SVE is not disabled, then at least one
     * vector length must be enabled.
     */
    DECLARE_BITMAP(kvm_supported, ARM_MAX_VQ);
    DECLARE_BITMAP(tmp, ARM_MAX_VQ);
    uint32_t vq, max_vq = 0;

    /* Collect the set of vector lengths supported by KVM. */
    bitmap_zero(kvm_supported, ARM_MAX_VQ);
    if (kvm_enabled() && kvm_arm_sve_supported()) {
        kvm_arm_sve_get_vls(CPU(cpu), kvm_supported);
    } else if (kvm_enabled()) {
        assert(!cpu_isar_feature(aa64_sve, cpu));
    }

    /*
     * Process explicit sve<N> properties.
     * From the properties, sve_vq_map<N> implies sve_vq_init<N>.
     * Check first for any sve<N> enabled.
     */
    if (!bitmap_empty(cpu->sve_vq_map, ARM_MAX_VQ)) {
        max_vq = find_last_bit(cpu->sve_vq_map, ARM_MAX_VQ) + 1;

        if (cpu->sve_max_vq && max_vq > cpu->sve_max_vq) {
            error_setg(errp, "cannot enable sve%d", max_vq * 128);
            error_append_hint(errp, "sve%d is larger than the maximum vector "
                              "length, sve-max-vq=%d (%d bits)\n",
                              max_vq * 128, cpu->sve_max_vq,
                              cpu->sve_max_vq * 128);
            return;
        }

        if (kvm_enabled()) {
            /*
             * For KVM we have to automatically enable all supported unitialized
             * lengths, even when the smaller lengths are not all powers-of-two.
             */
            bitmap_andnot(tmp, kvm_supported, cpu->sve_vq_init, max_vq);
            bitmap_or(cpu->sve_vq_map, cpu->sve_vq_map, tmp, max_vq);
        } else {
            /* Propagate enabled bits down through required powers-of-two. */
            for (vq = pow2floor(max_vq); vq >= 1; vq >>= 1) {
                if (!test_bit(vq - 1, cpu->sve_vq_init)) {
                    set_bit(vq - 1, cpu->sve_vq_map);
                }
            }
        }
    } else if (cpu->sve_max_vq == 0) {
        /*
         * No explicit bits enabled, and no implicit bits from sve-max-vq.
         */
        if (!cpu_isar_feature(aa64_sve, cpu)) {
            /* SVE is disabled and so are all vector lengths.  Good. */
            return;
        }

        if (kvm_enabled()) {
            /* Disabling a supported length disables all larger lengths. */
            for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
                if (test_bit(vq - 1, cpu->sve_vq_init) &&
                    test_bit(vq - 1, kvm_supported)) {
                    break;
                }
            }
            max_vq = vq <= ARM_MAX_VQ ? vq - 1 : ARM_MAX_VQ;
            bitmap_andnot(cpu->sve_vq_map, kvm_supported,
                          cpu->sve_vq_init, max_vq);
            if (max_vq == 0 || bitmap_empty(cpu->sve_vq_map, max_vq)) {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "Disabling sve%d results in all "
                                  "vector lengths being disabled.\n",
                                  vq * 128);
                error_append_hint(errp, "With SVE enabled, at least one "
                                  "vector length must be enabled.\n");
                return;
            }
        } else {
            /* Disabling a power-of-two disables all larger lengths. */
            if (test_bit(0, cpu->sve_vq_init)) {
                error_setg(errp, "cannot disable sve128");
                error_append_hint(errp, "Disabling sve128 results in all "
                                  "vector lengths being disabled.\n");
                error_append_hint(errp, "With SVE enabled, at least one "
                                  "vector length must be enabled.\n");
                return;
            }
            for (vq = 2; vq <= ARM_MAX_VQ; vq <<= 1) {
                if (test_bit(vq - 1, cpu->sve_vq_init)) {
                    break;
                }
            }
            max_vq = vq <= ARM_MAX_VQ ? vq - 1 : ARM_MAX_VQ;
            bitmap_complement(cpu->sve_vq_map, cpu->sve_vq_init, max_vq);
        }

        max_vq = find_last_bit(cpu->sve_vq_map, max_vq) + 1;
    }

    /*
     * Process the sve-max-vq property.
     * Note that we know from the above that no bit above
     * sve-max-vq is currently set.
     */
    if (cpu->sve_max_vq != 0) {
        max_vq = cpu->sve_max_vq;

        if (!test_bit(max_vq - 1, cpu->sve_vq_map) &&
            test_bit(max_vq - 1, cpu->sve_vq_init)) {
            error_setg(errp, "cannot disable sve%d", max_vq * 128);
            error_append_hint(errp, "The maximum vector length must be "
                              "enabled, sve-max-vq=%d (%d bits)\n",
                              max_vq, max_vq * 128);
            return;
        }

        /* Set all bits not explicitly set within sve-max-vq. */
        bitmap_complement(tmp, cpu->sve_vq_init, max_vq);
        bitmap_or(cpu->sve_vq_map, cpu->sve_vq_map, tmp, max_vq);
    }

    /*
     * We should know what max-vq is now.  Also, as we're done
     * manipulating sve-vq-map, we ensure any bits above max-vq
     * are clear, just in case anybody looks.
     */
    assert(max_vq != 0);
    bitmap_clear(cpu->sve_vq_map, max_vq, ARM_MAX_VQ - max_vq);

    if (kvm_enabled()) {
        /* Ensure the set of lengths matches what KVM supports. */
        bitmap_xor(tmp, cpu->sve_vq_map, kvm_supported, max_vq);
        if (!bitmap_empty(tmp, max_vq)) {
            vq = find_last_bit(tmp, max_vq) + 1;
            if (test_bit(vq - 1, cpu->sve_vq_map)) {
                if (cpu->sve_max_vq) {
                    error_setg(errp, "cannot set sve-max-vq=%d",
                               cpu->sve_max_vq);
                    error_append_hint(errp, "This KVM host does not support "
                                      "the vector length %d-bits.\n",
                                      vq * 128);
                    error_append_hint(errp, "It may not be possible to use "
                                      "sve-max-vq with this KVM host. Try "
                                      "using only sve<N> properties.\n");
                } else {
                    error_setg(errp, "cannot enable sve%d", vq * 128);
                    error_append_hint(errp, "This KVM host does not support "
                                      "the vector length %d-bits.\n",
                                      vq * 128);
                }
            } else {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "The KVM host requires all "
                                  "supported vector lengths smaller "
                                  "than %d bits to also be enabled.\n",
                                  max_vq * 128);
            }
            return;
        }
    } else {
        /* Ensure all required powers-of-two are enabled. */
        for (vq = pow2floor(max_vq); vq >= 1; vq >>= 1) {
            if (!test_bit(vq - 1, cpu->sve_vq_map)) {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "sve%d is required as it "
                                  "is a power-of-two length smaller than "
                                  "the maximum, sve%d\n",
                                  vq * 128, max_vq * 128);
                return;
            }
        }
    }

    /*
     * Now that we validated all our vector lengths, the only question
     * left to answer is if we even want SVE at all.
     */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        error_setg(errp, "cannot enable sve%d", max_vq * 128);
        error_append_hint(errp, "SVE must be enabled to enable vector "
                          "lengths.\n");
        error_append_hint(errp, "Add sve=on to the CPU property list.\n");
        return;
    }

    /* From now on sve_max_vq is the actual maximum supported length. */
    cpu->sve_max_vq = max_vq;
}

static void cpu_max_get_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value;

    /* All vector lengths are disabled when SVE is off. */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        value = 0;
    } else {
        value = cpu->sve_max_vq;
    }
    visit_type_uint32(v, name, &value, errp);
}

static void cpu_max_set_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t max_vq;

    if (!visit_type_uint32(v, name, &max_vq, errp)) {
        return;
    }

    if (kvm_enabled() && !kvm_arm_sve_supported()) {
        error_setg(errp, "cannot set sve-max-vq");
        error_append_hint(errp, "SVE not supported by KVM on this host\n");
        return;
    }

    if (max_vq == 0 || max_vq > ARM_MAX_VQ) {
        error_setg(errp, "unsupported SVE vector length");
        error_append_hint(errp, "Valid sve-max-vq in range [1-%d]\n",
                          ARM_MAX_VQ);
        return;
    }

    cpu->sve_max_vq = max_vq;
}

/*
 * Note that cpu_arm_get/set_sve_vq cannot use the simpler
 * object_property_add_bool interface because they make use
 * of the contents of "name" to determine which bit on which
 * to operate.
 */
static void cpu_arm_get_sve_vq(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq = atoi(&name[3]) / 128;
    bool value;

    /* All vector lengths are disabled when SVE is off. */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        value = false;
    } else {
        value = test_bit(vq - 1, cpu->sve_vq_map);
    }
    visit_type_bool(v, name, &value, errp);
}

static void cpu_arm_set_sve_vq(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq = atoi(&name[3]) / 128;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    if (value && kvm_enabled() && !kvm_arm_sve_supported()) {
        error_setg(errp, "cannot enable %s", name);
        error_append_hint(errp, "SVE not supported by KVM on this host\n");
        return;
    }

    if (value) {
        set_bit(vq - 1, cpu->sve_vq_map);
    } else {
        clear_bit(vq - 1, cpu->sve_vq_map);
    }
    set_bit(vq - 1, cpu->sve_vq_init);
}

static bool cpu_arm_get_sve(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_sve, cpu);
}

static void cpu_arm_set_sve(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint64_t t;

    if (value && kvm_enabled() && !kvm_arm_sve_supported()) {
        error_setg(errp, "'sve' feature not supported by KVM on this host");
        return;
    }

    t = cpu->isar.id_aa64pfr0;
    t = FIELD_DP64(t, ID_AA64PFR0, SVE, value);
    cpu->isar.id_aa64pfr0 = t;
}

#ifdef CONFIG_USER_ONLY
/* Mirror linux /proc/sys/abi/sve_default_vector_length. */
static void cpu_arm_set_sve_default_vec_len(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    int32_t default_len, default_vq, remainder;

    if (!visit_type_int32(v, name, &default_len, errp)) {
        return;
    }

    /* Undocumented, but the kernel allows -1 to indicate "maximum". */
    if (default_len == -1) {
        cpu->sve_default_vq = ARM_MAX_VQ;
        return;
    }

    default_vq = default_len / 16;
    remainder = default_len % 16;

    /*
     * Note that the 512 max comes from include/uapi/asm/sve_context.h
     * and is the maximum architectural width of ZCR_ELx.LEN.
     */
    if (remainder || default_vq < 1 || default_vq > 512) {
        error_setg(errp, "cannot set sve-default-vector-length");
        if (remainder) {
            error_append_hint(errp, "Vector length not a multiple of 16\n");
        } else if (default_vq < 1) {
            error_append_hint(errp, "Vector length smaller than 16\n");
        } else {
            error_append_hint(errp, "Vector length larger than %d\n",
                              512 * 16);
        }
        return;
    }

    cpu->sve_default_vq = default_vq;
}

static void cpu_arm_get_sve_default_vec_len(Object *obj, Visitor *v,
                                            const char *name, void *opaque,
                                            Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    int32_t value = cpu->sve_default_vq * 16;

    visit_type_int32(v, name, &value, errp);
}
#endif

void aarch64_add_sve_properties(Object *obj)
{
    uint32_t vq;

    object_property_add_bool(obj, "sve", cpu_arm_get_sve, cpu_arm_set_sve);

    for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
        char name[8];
        sprintf(name, "sve%d", vq * 128);
        object_property_add(obj, name, "bool", cpu_arm_get_sve_vq,
                            cpu_arm_set_sve_vq, NULL, NULL);
    }

#ifdef CONFIG_USER_ONLY
    /* Mirror linux /proc/sys/abi/sve_default_vector_length. */
    object_property_add(obj, "sve-default-vector-length", "int32",
                        cpu_arm_get_sve_default_vec_len,
                        cpu_arm_set_sve_default_vec_len, NULL, NULL);
#endif
}

void arm_cpu_pauth_finalize(ARMCPU *cpu, Error **errp)
{
    int arch_val = 0, impdef_val = 0;
    uint64_t t;

    /* TODO: Handle HaveEnhancedPAC, HaveEnhancedPAC2, HaveFPAC. */
    if (cpu->prop_pauth) {
        if (cpu->prop_pauth_impdef) {
            impdef_val = 1;
        } else {
            arch_val = 1;
        }
    } else if (cpu->prop_pauth_impdef) {
        error_setg(errp, "cannot enable pauth-impdef without pauth");
        error_append_hint(errp, "Add pauth=on to the CPU property list.\n");
    }

    t = cpu->isar.id_aa64isar1;
    t = FIELD_DP64(t, ID_AA64ISAR1, APA, arch_val);
    t = FIELD_DP64(t, ID_AA64ISAR1, GPA, arch_val);
    t = FIELD_DP64(t, ID_AA64ISAR1, API, impdef_val);
    t = FIELD_DP64(t, ID_AA64ISAR1, GPI, impdef_val);
    cpu->isar.id_aa64isar1 = t;
}

static Property arm_cpu_pauth_property =
    DEFINE_PROP_BOOL("pauth", ARMCPU, prop_pauth, true);
static Property arm_cpu_pauth_impdef_property =
    DEFINE_PROP_BOOL("pauth-impdef", ARMCPU, prop_pauth_impdef, false);

/* -cpu max: if KVM is enabled, like -cpu host (best possible with this host);
 * otherwise, a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-arm is defined in cpu.c;
 * this only needs to handle 64 bits.
 */
static void aarch64_max_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    if (kvm_enabled()) {
        kvm_arm_set_cpu_features_from_host(cpu);
    } else {
        uint64_t t;
        uint32_t u;
        aarch64_a57_initfn(obj);

        /*
         * Reset MIDR so the guest doesn't mistake our 'max' CPU type for a real
         * one and try to apply errata workarounds or use impdef features we
         * don't provide.
         * An IMPLEMENTER field of 0 means "reserved for software use";
         * ARCHITECTURE must be 0xf indicating "v7 or later, check ID registers
         * to see which features are present";
         * the VARIANT, PARTNUM and REVISION fields are all implementation
         * defined and we choose to define PARTNUM just in case guest
         * code needs to distinguish this QEMU CPU from other software
         * implementations, though this shouldn't be needed.
         */
        t = FIELD_DP64(0, MIDR_EL1, IMPLEMENTER, 0);
        t = FIELD_DP64(t, MIDR_EL1, ARCHITECTURE, 0xf);
        t = FIELD_DP64(t, MIDR_EL1, PARTNUM, 'Q');
        t = FIELD_DP64(t, MIDR_EL1, VARIANT, 0);
        t = FIELD_DP64(t, MIDR_EL1, REVISION, 0);
        cpu->midr = t;

        t = cpu->isar.id_aa64isar0;
        t = FIELD_DP64(t, ID_AA64ISAR0, AES, 2); /* AES + PMULL */
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2); /* SHA512 */
        t = FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 2);
        t = FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, DP, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, TS, 2); /* v8.5-CondM */
        t = FIELD_DP64(t, ID_AA64ISAR0, TLB, 2); /* FEAT_TLBIRANGE */
        t = FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);
        cpu->isar.id_aa64isar0 = t;

        t = cpu->isar.id_aa64isar1;
        t = FIELD_DP64(t, ID_AA64ISAR1, DPB, 2);
        t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, SB, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, BF16, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, LRCPC, 2); /* ARMv8.4-RCPC */
        t = FIELD_DP64(t, ID_AA64ISAR1, I8MM, 1);
        cpu->isar.id_aa64isar1 = t;

        t = cpu->isar.id_aa64pfr0;
        t = FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, FP, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, SEL2, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, DIT, 1);
        cpu->isar.id_aa64pfr0 = t;

        t = cpu->isar.id_aa64pfr1;
        t = FIELD_DP64(t, ID_AA64PFR1, BT, 1);
        t = FIELD_DP64(t, ID_AA64PFR1, SSBS, 2);
        /*
         * Begin with full support for MTE. This will be downgraded to MTE=0
         * during realize if the board provides no tag memory, much like
         * we do for EL2 with the virtualization=on property.
         */
        t = FIELD_DP64(t, ID_AA64PFR1, MTE, 3);
        cpu->isar.id_aa64pfr1 = t;

        t = cpu->isar.id_aa64mmfr0;
        t = FIELD_DP64(t, ID_AA64MMFR0, PARANGE, 5); /* PARange: 48 bits */
        cpu->isar.id_aa64mmfr0 = t;

        t = cpu->isar.id_aa64mmfr1;
        t = FIELD_DP64(t, ID_AA64MMFR1, HPDS, 1); /* HPD */
        t = FIELD_DP64(t, ID_AA64MMFR1, LO, 1);
        t = FIELD_DP64(t, ID_AA64MMFR1, VH, 1);
        t = FIELD_DP64(t, ID_AA64MMFR1, PAN, 2); /* ATS1E1 */
        t = FIELD_DP64(t, ID_AA64MMFR1, VMIDBITS, 2); /* VMID16 */
        t = FIELD_DP64(t, ID_AA64MMFR1, XNX, 1); /* TTS2UXN */
        cpu->isar.id_aa64mmfr1 = t;

        t = cpu->isar.id_aa64mmfr2;
        t = FIELD_DP64(t, ID_AA64MMFR2, UAO, 1);
        t = FIELD_DP64(t, ID_AA64MMFR2, CNP, 1); /* TTCNP */
        t = FIELD_DP64(t, ID_AA64MMFR2, ST, 1); /* TTST */
        cpu->isar.id_aa64mmfr2 = t;

        t = cpu->isar.id_aa64zfr0;
        t = FIELD_DP64(t, ID_AA64ZFR0, SVEVER, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, AES, 2);  /* PMULL */
        t = FIELD_DP64(t, ID_AA64ZFR0, BITPERM, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, BFLOAT16, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, SHA3, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, SM4, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, I8MM, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, F32MM, 1);
        t = FIELD_DP64(t, ID_AA64ZFR0, F64MM, 1);
        cpu->isar.id_aa64zfr0 = t;

        /* Replicate the same data to the 32-bit id registers.  */
        u = cpu->isar.id_isar5;
        u = FIELD_DP32(u, ID_ISAR5, AES, 2); /* AES + PMULL */
        u = FIELD_DP32(u, ID_ISAR5, SHA1, 1);
        u = FIELD_DP32(u, ID_ISAR5, SHA2, 1);
        u = FIELD_DP32(u, ID_ISAR5, CRC32, 1);
        u = FIELD_DP32(u, ID_ISAR5, RDM, 1);
        u = FIELD_DP32(u, ID_ISAR5, VCMA, 1);
        cpu->isar.id_isar5 = u;

        u = cpu->isar.id_isar6;
        u = FIELD_DP32(u, ID_ISAR6, JSCVT, 1);
        u = FIELD_DP32(u, ID_ISAR6, DP, 1);
        u = FIELD_DP32(u, ID_ISAR6, FHM, 1);
        u = FIELD_DP32(u, ID_ISAR6, SB, 1);
        u = FIELD_DP32(u, ID_ISAR6, SPECRES, 1);
        u = FIELD_DP32(u, ID_ISAR6, BF16, 1);
        u = FIELD_DP32(u, ID_ISAR6, I8MM, 1);
        cpu->isar.id_isar6 = u;

        u = cpu->isar.id_pfr0;
        u = FIELD_DP32(u, ID_PFR0, DIT, 1);
        cpu->isar.id_pfr0 = u;

        u = cpu->isar.id_pfr2;
        u = FIELD_DP32(u, ID_PFR2, SSBS, 1);
        cpu->isar.id_pfr2 = u;

        u = cpu->isar.id_mmfr3;
        u = FIELD_DP32(u, ID_MMFR3, PAN, 2); /* ATS1E1 */
        cpu->isar.id_mmfr3 = u;

        u = cpu->isar.id_mmfr4;
        u = FIELD_DP32(u, ID_MMFR4, HPDS, 1); /* AA32HPD */
        u = FIELD_DP32(u, ID_MMFR4, AC2, 1); /* ACTLR2, HACTLR2 */
        u = FIELD_DP32(u, ID_MMFR4, CNP, 1); /* TTCNP */
        u = FIELD_DP32(u, ID_MMFR4, XNX, 1); /* TTS2UXN */
        cpu->isar.id_mmfr4 = u;

        t = cpu->isar.id_aa64dfr0;
        t = FIELD_DP64(t, ID_AA64DFR0, PMUVER, 5); /* v8.4-PMU */
        cpu->isar.id_aa64dfr0 = t;

        u = cpu->isar.id_dfr0;
        u = FIELD_DP32(u, ID_DFR0, PERFMON, 5); /* v8.4-PMU */
        cpu->isar.id_dfr0 = u;

        u = cpu->isar.mvfr1;
        u = FIELD_DP32(u, MVFR1, FPHP, 3);      /* v8.2-FP16 */
        u = FIELD_DP32(u, MVFR1, SIMDHP, 2);    /* v8.2-FP16 */
        cpu->isar.mvfr1 = u;

#ifdef CONFIG_USER_ONLY
        /* For usermode -cpu max we can use a larger and more efficient DCZ
         * blocksize since we don't have to follow what the hardware does.
         */
        cpu->ctr = 0x80038003; /* 32 byte I and D cacheline size, VIPT icache */
        cpu->dcz_blocksize = 7; /*  512 bytes */
#endif

        /* Default to PAUTH on, with the architected algorithm. */
        qdev_property_add_static(DEVICE(obj), &arm_cpu_pauth_property);
        qdev_property_add_static(DEVICE(obj), &arm_cpu_pauth_impdef_property);
    }

    aarch64_add_sve_properties(obj);
    object_property_add(obj, "sve-max-vq", "uint32", cpu_max_get_sve_max_vq,
                        cpu_max_set_sve_max_vq, NULL, NULL);
}

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a57",         .initfn = aarch64_a57_initfn },
    { .name = "cortex-a53",         .initfn = aarch64_a53_initfn },
    { .name = "cortex-a72",         .initfn = aarch64_a72_initfn },
    { .name = "cortex-a78",         .initfn = aarch64_a78_initfn },
    { .name = "max",                .initfn = aarch64_max_initfn },
};

static bool aarch64_cpu_get_aarch64(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    return arm_feature(&cpu->env, ARM_FEATURE_AARCH64);
}

static void aarch64_cpu_set_aarch64(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* At this time, this property is only allowed if KVM is enabled.  This
     * restriction allows us to avoid fixing up functionality that assumes a
     * uniform execution state like do_interrupt.
     */
    if (value == false) {
        if (!kvm_enabled() || !kvm_arm_aarch32_supported()) {
            error_setg(errp, "'aarch64' feature cannot be disabled "
                             "unless KVM is enabled and 32-bit EL1 "
                             "is supported");
            return;
        }
        unset_feature(&cpu->env, ARM_FEATURE_AARCH64);
    } else {
        set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    }
}

static void aarch64_cpu_finalizefn(Object *obj)
{
}

static const char *a64_debug_ctx[] = {
       [DEBUG_CURRENT_EL] = "current-el",
       [DEBUG_EL0] = "el0",
       [DEBUG_EL1] = "el1",
       [DEBUG_EL2] = "el2",
       [DEBUG_EL3] = "el3",
       [DEBUG_PHYS] = "phys",
};

static gchar *aarch64_gdb_arch_name(CPUState *cs)
{
    return g_strdup("aarch64");
}

static void set_debug_context(CPUState *cs, unsigned int ctx)
{
    ARMCPU *cpu = ARM_CPU(cs);
    cpu->env.debug_ctx = ctx;
}

static void aarch64_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->debug_contexts = a64_debug_ctx;
    cc->set_debug_context = set_debug_context;
    cc->gdb_read_register = aarch64_cpu_gdb_read_register;
    cc->gdb_write_register = aarch64_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 34;
    cc->gdb_core_xml_file = "aarch64-core.xml";
    cc->gdb_arch_name = aarch64_gdb_arch_name;

    object_class_property_add_bool(oc, "aarch64", aarch64_cpu_get_aarch64,
                                   aarch64_cpu_set_aarch64);
    object_class_property_set_description(oc, "aarch64",
                                          "Set on/off to enable/disable aarch64 "
                                          "execution state ");
}

static void aarch64_cpu_instance_init(Object *obj)
{
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(obj);

    acc->info->initfn(obj);
    arm_cpu_post_init(obj);
}

static void cpu_register_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);

    acc->info = data;
}

void aarch64_cpu_register(const ARMCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_AARCH64_CPU,
        .instance_size = sizeof(ARMCPU),
        .instance_init = aarch64_cpu_instance_init,
        .class_size = sizeof(ARMCPUClass),
        .class_init = info->class_init ?: cpu_register_class_init,
        .class_data = (void *)info,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_ARM_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo aarch64_cpu_type_info = {
    .name = TYPE_AARCH64_CPU,
    .parent = TYPE_ARM_CPU,
    .instance_size = sizeof(ARMCPU),
    .instance_finalize = aarch64_cpu_finalizefn,
    .abstract = true,
    .class_size = sizeof(AArch64CPUClass),
    .class_init = aarch64_cpu_class_init,
};

static void aarch64_cpu_register_types(void)
{
    size_t i;

    type_register_static(&aarch64_cpu_type_info);

    for (i = 0; i < ARRAY_SIZE(aarch64_cpus); ++i) {
        aarch64_cpu_register(&aarch64_cpus[i]);
    }
}

type_init(aarch64_cpu_register_types)
