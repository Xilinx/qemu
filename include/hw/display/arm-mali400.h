/*
 * QEMU model of the ARM MALI-400 GPU
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ARM_MALI400_H
#define ARM_MALI400_H

#include "hw/qdev-core.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"

#define TYPE_ARM_MALI400 "arm-mali-400"
OBJECT_DECLARE_SIMPLE_TYPE(ArmMali400, ARM_MALI400);

/* See arm-mali400.c for detail of the ranges */
#define ARM_MALI400_L2C_R_MAX      (0x0200 / 4)
#define ARM_MALI400_PMU_R_MAX      (0x0100 / 4)
#define ARM_MALI400_MMU_R_MAX      (0x0100 / 4)
#define ARM_MALI400_GP_CORE_R_MAX  (0x0100 / 4)
#define ARM_MALI400_PP_CORE_R_MAX  (0x0100 / 4)
#define ARM_MALI400_PP_REND_R_MAX  (0x0100 / 4)
#define ARM_MALI400_PP_WB_R_MAX    (0x0100 / 4)

typedef struct ArmMali400GP_Reg {
    uint32_t mmu[ARM_MALI400_MMU_R_MAX];
    uint32_t core[ARM_MALI400_GP_CORE_R_MAX];
} ArmMali400GP_Reg;

typedef struct ArmMali400PP_Reg {
    uint32_t mmu[ARM_MALI400_MMU_R_MAX];
    uint32_t core[ARM_MALI400_PP_CORE_R_MAX];
    uint32_t rend[ARM_MALI400_PP_REND_R_MAX];
    uint32_t wb[3][ARM_MALI400_PP_WB_R_MAX];
} ArmMali400PP_Reg;

typedef struct ArmMali400 {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    bool resetting;

    /*
     * Configurable register-access tracing, with suppression of identical
     * consecutive reads, e.g., busy-poll, to prevent trace output flood.
     */
    struct {
        RegisterInfoArray *block;
        hwaddr   addr;
        uint64_t count;
        uint64_t data;
        bool     enable;
    } reg_trc;

    /* TODO: configurable irq sharing; for now, 1 IRQ for all sub-units */
    qemu_irq irq;
    uint32_t irq_pending;

    /* Configurables */
    uint32_t l2c_version;
    uint32_t l2c_size;
    uint32_t num_pp;

    /* MMIO registers */
    struct {
        uint32_t l2c[ARM_MALI400_L2C_R_MAX];
        uint32_t pmu[ARM_MALI400_PMU_R_MAX];
        ArmMali400GP_Reg gp;
        ArmMali400PP_Reg pp[4];
    } regs;

    struct {
        RegisterInfo l2c[ARM_MALI400_L2C_R_MAX];
        RegisterInfo pmu[ARM_MALI400_PMU_R_MAX];
        struct {
            RegisterInfo mmu[ARM_MALI400_MMU_R_MAX];
            RegisterInfo core[ARM_MALI400_GP_CORE_R_MAX];
        } gp;
        struct {
            RegisterInfo mmu[ARM_MALI400_MMU_R_MAX];
            RegisterInfo core[ARM_MALI400_PP_CORE_R_MAX];
            RegisterInfo rend[ARM_MALI400_PP_REND_R_MAX];
            RegisterInfo wb[3][ARM_MALI400_PP_WB_R_MAX];
        } pp[4];
    } regs_info;
} ArmMali400;

#endif
