/*
 * ARM P-channel power management interface
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "hw/arm/pchannel.h"

static const TypeInfo arm_pchannel_if_info = {
    .name = TYPE_ARM_PCHANNEL_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(ARMPChannelIfClass),
};

static void arm_pchannel_register_types(void)
{
    type_register_static(&arm_pchannel_if_info);
}

type_init(arm_pchannel_register_types)
