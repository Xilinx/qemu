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
#include "hw/qdev-properties.h"
#include "hw/arm/pchannel.h"

static uint32_t dummy_get_current_state(ARMPChannelIf *obj)
{
    ARMPChannelDummyState *s = ARM_PCHANNEL_DUMMY(obj);

    return s->pstate == s->pstate_on ? s->pactive_on : s->pactive_off;
}

static bool dummy_request_state_change(ARMPChannelIf *obj, uint32_t state)
{
    ARMPChannelDummyState *s = ARM_PCHANNEL_DUMMY(obj);

    s->pstate = state;
    return true;
}

static void arm_pchannel_dummy_reset(DeviceState *dev)
{
    ARMPChannelDummyState *s = ARM_PCHANNEL_DUMMY(dev);

    s->pstate = s->reset_pstate;
}

static Property arm_pchannel_dummy_properties[] = {
    DEFINE_PROP_UINT32("pstate-reset-val", ARMPChannelDummyState,
                       reset_pstate, 0),
    DEFINE_PROP_UINT32("pstate-on", ARMPChannelDummyState, pstate_on, 0),
    DEFINE_PROP_UINT32("pactive-on", ARMPChannelDummyState, pactive_on, 0),
    DEFINE_PROP_UINT32("pactive-off", ARMPChannelDummyState, pactive_off, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void arm_pchannel_dummy_class_init(ObjectClass *klass, void *data)
{
    ARMPChannelIfClass *apcic = ARM_PCHANNEL_IF_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, arm_pchannel_dummy_properties);
    dc->reset = arm_pchannel_dummy_reset;
    apcic->get_current_state = dummy_get_current_state;
    apcic->request_state_change = dummy_request_state_change;
}

static const TypeInfo arm_pchannel_if_info = {
    .name = TYPE_ARM_PCHANNEL_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(ARMPChannelIfClass),
};

static const TypeInfo arm_pchannel_dummy_info = {
    .name = TYPE_ARM_PCHANNEL_DUMMY,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ARMPChannelDummyState),
    .class_init = arm_pchannel_dummy_class_init,
    .interfaces = (InterfaceInfo []) {
        { TYPE_ARM_PCHANNEL_IF },
        { }
    },
};

static void arm_pchannel_register_types(void)
{
    type_register_static(&arm_pchannel_if_info);
    type_register_static(&arm_pchannel_dummy_info);
}

type_init(arm_pchannel_register_types)
