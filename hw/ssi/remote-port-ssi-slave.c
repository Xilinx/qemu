/*
 * QEMU remote port SSI slave. Write transactions recieved from the remote port
 * are sent over an SSI bus.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "hw/qdev.h"
#include "qapi/error.h"
#include "hw/ssi/ssi.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"

#define TYPE_REMOTE_PORT_SSI_SLAVE "remote-port-ssi-slave"
#define REMOTE_PORT_SSI_SLAVE(obj) \
        OBJECT_CHECK(RemotePortSSISlave, (obj), \
                     TYPE_REMOTE_PORT_SSI_SLAVE)

typedef struct RemotePortSSISlave {
    /* private */
    DeviceState parent;
    /* public */
    struct RemotePort *rp;
    SSIBus *ssib;

    uint16_t num_ssi_devs;
} RemotePortSSISlave;

static void rp_ssi_slave_write(RemotePortDevice *rpd, struct rp_pkt *pkt)
{
    RemotePortSSISlave *s = REMOTE_PORT_SSI_SLAVE(rpd);
    /* FIXME: be less hardcoded */
    size_t pktlen = sizeof(struct rp_pkt_busaccess) + 4;
    size_t enclen;
    int64_t delay;
    uint32_t data, *data_p;
    RemotePortDynPkt rsp;

    data_p = (uint32_t *)(pkt + 1);
    data = be32_to_cpu(*data_p);

    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    memset(&rsp, 0, sizeof(rsp));
    rp_dpkt_alloc(&rsp, pktlen);

    data = ssi_transfer(s->ssib, data);
    data_p = (uint32_t *)(rsp.pkt + 1);
    *data_p = cpu_to_be32(data);
    /* delay here could be set to the annotated cost of doing issuing
       these accesses. QEMU doesn't support this kind of annotations
       at the moment. So we just clear the delay.  */
    delay = 0;

    enclen = rp_encode_read_resp(
                    pkt->hdr.id, pkt->hdr.dev, &rsp.pkt->busaccess,
                    pkt->busaccess.timestamp + delay,
                    0,
                    pkt->busaccess.addr,
                    pkt->busaccess.attributes,
                    pkt->busaccess.len,
                    pkt->busaccess.width,
                    pkt->busaccess.stream_width);
    /* FIXME */
    assert(enclen == pktlen);

    rp_write(s->rp, (void *)rsp.pkt, pktlen);
}

static void rp_ssi_slave_realize(DeviceState *dev, Error **errp)
{
    RemotePortSSISlave *s = REMOTE_PORT_SSI_SLAVE(dev);

    s->ssib = ssi_create_bus(dev, "ssib");
}

static void rp_ssi_slave_init(Object *obj)
{
    RemotePortSSISlave *rpms = REMOTE_PORT_SSI_SLAVE(obj);

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static Property rp_properties[] = {
     DEFINE_PROP_UINT16("num-ssi-devs", RemotePortSSISlave, num_ssi_devs, 1),
     DEFINE_PROP_END_OF_LIST(),
};

static void rp_ssi_slave_class_init(ObjectClass *oc, void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    rpdc->ops[RP_CMD_write] = rp_ssi_slave_write;
    dc->realize = rp_ssi_slave_realize;
    dc->props = rp_properties;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_SSI_SLAVE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemotePortSSISlave),
    .instance_init = rp_ssi_slave_init,
    .class_init    = rp_ssi_slave_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
