/*
 * ARM P-channel power management interface
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef HW_ARM_PCHANNEL_H
#define HW_ARM_PCHANNEL_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_ARM_PCHANNEL_IF "arm-pchannel-if"
typedef struct ARMPChannelIfClass ARMPChannelIfClass;
DECLARE_CLASS_CHECKERS(ARMPChannelIfClass, ARM_PCHANNEL_IF,
                       TYPE_ARM_PCHANNEL_IF)

#define ARM_PCHANNEL_IF(obj) \
    INTERFACE_CHECK(ARMPChannelIf, (obj), TYPE_ARM_PCHANNEL_IF)

typedef struct ARMPChannelIf ARMPChannelIf;

struct ARMPChannelIfClass {
    InterfaceClass parent_class;

    /**
     * request_state_change
     *
     * Request the device to switch to the device specific @new_state P-Channel
     * state.
     *
     * Returns: true if the request was fulfilled, false if it was denied
     */
    bool (*request_state_change)(ARMPChannelIf *obj, uint32_t new_state);

    /**
     * get_current_state
     *
     * Query the device's current P-Channel state
     *
     * Returns: the device specific P-Channel state
     */
    uint32_t (*get_current_state)(ARMPChannelIf *obj);
};

static inline bool pchannel_request_state_change(ARMPChannelIf *obj,
                                                 uint32_t new_state)
{
    ARMPChannelIfClass *klass = ARM_PCHANNEL_IF_GET_CLASS(obj);

    return klass->request_state_change(obj, new_state);
}

static inline uint32_t pchannel_get_current_state(ARMPChannelIf *obj)
{
    ARMPChannelIfClass *klass = ARM_PCHANNEL_IF_GET_CLASS(obj);

    return klass->get_current_state(obj);
}

#endif
