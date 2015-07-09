#ifndef GUEST_LINUX_H

#include "qemu-common.h"
#include "qom/object.h"

#define TYPE_LINUX_DEVICE "linux,device"

#define LINUX_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(LinuxDeviceClass, (klass), TYPE_LINUX_DEVICE)
#define LINUX_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(LinuxDeviceClass, (obj), TYPE_LINUX_DEVICE)
#define LINUX_DEVICE(obj) \
    INTERFACE_CHECK(LinuxDevice, (obj), TYPE_LINUX_DEVICE)

typedef struct LinuxDevice {
    /*< private >*/
    Object parent_obj;
} LinuxDevice;

typedef struct LinuxDeviceClass {
    /*< private > */
    InterfaceClass parent_class;

    /*< public >*/
    /** linux_init - Init the device for a Linux boot. Setup the device in the
     * correct post-firmware state for a Linux boot.
     */
    void (*linux_init)(LinuxDevice *obj);
} LinuxDeviceClass;

#define GUEST_LINUX_H
#endif
