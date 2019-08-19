/*
 * Tiny device exposing GPIOs to control reset of the full machine.
 *
 * Copyright (c) 2015 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "sysemu/reset.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#define TYPE_RESET_DEVICE "qemu.reset-device"
#define RESET_DEVICE(obj) \
        OBJECT_CHECK(ResetDevice, (obj), TYPE_RESET_DEVICE)

typedef struct ResetDevice {
    DeviceState parent;
} ResetDevice;

static void reset_handler(void *opaque, int irq, int level)
{
    if (level) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static void reset_init(Object *obj)
{
    qdev_init_gpio_in(DEVICE(obj), reset_handler, 16);
}

static const TypeInfo reset_info = {
    .name          = TYPE_RESET_DEVICE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(ResetDevice),
    .instance_init = reset_init,
};

static void reset_register_types(void)
{
    type_register_static(&reset_info);
}

type_init(reset_register_types)
