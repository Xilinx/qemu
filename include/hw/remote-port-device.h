#ifndef REMOTE_PORT_DEVICE_H
#define REMOTE_PORT_DEVICE_H

#include "qemu-common.h"
#include "qom/object.h"
#include "hw/remote-port-proto.h"

#define TYPE_REMOTE_PORT_DEVICE "remote-port-device"

#define REMOTE_PORT_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(RemotePortDeviceClass, (klass), TYPE_REMOTE_PORT_DEVICE)
#define REMOTE_PORT_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RemotePortDeviceClass, (obj), TYPE_REMOTE_PORT_DEVICE)
#define REMOTE_PORT_DEVICE(obj) \
     INTERFACE_CHECK(RemotePortDevice, (obj), TYPE_REMOTE_PORT_DEVICE)

typedef struct RemotePort RemotePort;

typedef struct RemotePortDevice {
    /*< private >*/
    Object parent_obj;
} RemotePortDevice;

typedef struct RemotePortDeviceClass {
    /*< private >*/
    InterfaceClass parent_class;

    /*< public >*/
    /**
     * ops - operations to perform when remote port packets are recieved for
     * this device. Function N will be called for a remote port packet with
     * cmd == N in the header.
     *
     * @obj - Remote port device to recieve packet
     * @pkt - remote port packets
     */

    void (*ops[RP_CMD_max+1])(RemotePortDevice *obj, struct rp_pkt *pkt);

} RemotePortDeviceClass;

uint32_t rp_new_id(RemotePort *s);
/* FIXME: Cleanup and reduce the API complexity for dealing with responses.  */
void rp_rsp_mutex_lock(RemotePort *s);
void rp_rsp_mutex_unlock(RemotePort *s);
void rp_restart_sync_timer(RemotePort *s);

ssize_t rp_write(RemotePort *s, const void *buf, size_t count);

RemotePortDynPkt rp_wait_resp(RemotePort *s);

int64_t rp_normalized_vmclk(RemotePort *s);

struct rp_peer_state *rp_get_peer(RemotePort *s);

#endif
