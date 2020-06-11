/*
 * QEMU remote-port network interface proxy.
 *
 * Copyright (c) 2019 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "sysemu/dma.h"
#include "hw/hw.h"
#include "net/net.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/remote-port.h"
#include "hw/remote-port-device.h"

#define D(x) do { \
    if (0) {      \
        x;        \
    }             \
} while (0);

#define TYPE_REMOTE_PORT_NET "remote-port-net"
#define REMOTE_PORT_NET(obj) \
    OBJECT_CHECK(struct RemotePortNet, (obj), TYPE_REMOTE_PORT_NET)

typedef struct RemotePortNetChannel {
    struct RemotePort *rp;
    struct rp_peer_state *peer;
    uint32_t rp_dev;
} RemotePortNetChannel;

typedef struct RemotePortNet {
    DeviceState parent_obj;

    unsigned char tx_buf[4 * 1024];

    NICState *nic;
    NICConf conf;

    RemotePortNetChannel rx;
    RemotePortNetChannel tx;

    RemotePortDynPkt rsp;
} RemotePortNet;

static void rp_net_tx(RemotePortDevice *rpd, struct rp_pkt *pkt)
{
    struct RemotePortNet *s = REMOTE_PORT_NET(rpd);
    RemotePortDynPkt rsp = {0};
    size_t pktlen = sizeof(struct rp_pkt_busaccess_ext_base);
    struct rp_encode_busaccess_in in = {0};
    size_t enclen;
    uint8_t *data = NULL;

    data = rp_busaccess_rx_dataptr(s->tx.peer, &pkt->busaccess_ext_base);

    assert(pkt->busaccess.width == 0);
    assert(pkt->busaccess.stream_width == pkt->busaccess.len);
    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    if (!(pkt->hdr.flags & RP_PKT_FLAGS_posted)) {
        rp_dpkt_alloc(&rsp, pktlen);

        rp_encode_busaccess_in_rsp_init(&in, pkt);
        in.clk = pkt->busaccess.timestamp;
        enclen = rp_encode_busaccess(s->tx.peer, &rsp.pkt->busaccess_ext_base,
                                     &in);
        assert(enclen <= pktlen);

        rp_write(s->tx.rp, (void *)rsp.pkt, enclen);
    }
    qemu_send_packet(qemu_get_queue(s->nic),
                     data,  pkt->busaccess.len);
}

static bool rp_net_can_rx(NetClientState *nc)
{
    return true;
}

#define RP_NET_MAX_PACKET_SIZE (4 * 1024)
static ssize_t rp_net_rx(NetClientState *nc, const uint8_t *buf, size_t size)
{
    struct RemotePortNet *s = qemu_get_nic_opaque(nc);
    struct  {
        struct rp_pkt_busaccess_ext_base pkt;
        uint8_t reserved[RP_NET_MAX_PACKET_SIZE];
    } pay;
    uint8_t *data = rp_busaccess_tx_dataptr(s->rx.peer, &pay.pkt);
    struct rp_encode_busaccess_in in = {0};
    int len;

    memcpy(data, buf, size);

    in.cmd = RP_CMD_write;
    in.flags = RP_PKT_FLAGS_posted;
    in.id = rp_new_id(s->rx.rp);
    in.dev = s->rx.rp_dev;
    in.clk = rp_normalized_vmclk(s->rx.rp);
    in.master_id = 0;
    in.addr = 0;
    in.attr = RP_BUS_ATTR_EOP;
    in.size = size;
    in.stream_width = size;
    len = rp_encode_busaccess(s->rx.peer, &pay.pkt, &in);
    len += size;

    rp_write(s->rx.rp, (void *) &pay, len);
    return size;
}

static void rp_net_reset(DeviceState *dev)
{
}

static NetClientInfo net_rp_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = rp_net_can_rx,
    .receive = rp_net_rx,
};

static void rp_net_realize(DeviceState *dev, Error **errp)
{
    struct RemotePortNet *s = REMOTE_PORT_NET(dev);

    if (!s->rx.rp) {
        error_report("%s: rp-adaptor0 not set!", __func__);
        exit(EXIT_FAILURE);
    }

    if (!s->rx.rp_dev || !s->tx.rp_dev) {
        error_report("%s: rp_dev0 and rp_dev1 must be non-zero!", __func__);
        exit(EXIT_FAILURE);
    }

    if (!s->tx.rp) {
        /* If the user only specifies one adaptor, use the same for tx.  */
        s->tx.rp = s->rx.rp;
    }

    s->rx.peer = rp_get_peer(s->rx.rp);
    s->tx.peer = rp_get_peer(s->tx.rp);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_rp_net_info, &s->conf,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void rp_net_init(Object *obj)
{
    struct RemotePortNet *s = REMOTE_PORT_NET(obj);

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&s->rx.rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "rp-adaptor1", "remote-port",
                             (Object **)&s->tx.rp,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static Property rp_net_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortNet, rx.rp_dev, 0),
    DEFINE_PROP_UINT32("rp-chan1", RemotePortNet, tx.rp_dev, 0),
    DEFINE_NIC_PROPERTIES(struct RemotePortNet, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp_net_class_init(ObjectClass *klass, void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rp_net_realize;
    dc->reset = rp_net_reset;
    device_class_set_props(dc, rp_net_properties);

    rpdc->ops[RP_CMD_write] = rp_net_tx;
}

static const TypeInfo rp_net_info = {
    .name          = TYPE_REMOTE_PORT_NET,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(struct RemotePortNet),
    .instance_init = rp_net_init,
    .class_init    = rp_net_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_net_register_types(void)
{
    type_register_static(&rp_net_info);
}

type_init(rp_net_register_types)
