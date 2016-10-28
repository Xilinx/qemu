#include "qemu/osdep.h"
#include "hw/guest/linux.h"

static const TypeInfo linux_device_info = {
    .name          = TYPE_LINUX_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(LinuxDeviceClass),
};

static void linux_register_types(void)
{
    type_register_static(&linux_device_info);
}

type_init(linux_register_types)
