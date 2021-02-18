/*
 * Copyright (c) 2013 Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (c) 2013 Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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
#include "hw/sysbus.h"
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/stream.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/remote-port-device.h"
#include "trace.h"

#ifndef REMOTE_PORT_STREAM_ERR_DEBUG
#define REMOTE_PORT_STREAM_ERR_DEBUG 0
#endif

#define TYPE_REMOTE_PORT_STREAM "remote-port-stream"

#define REMOTE_PORT_STREAM(obj) \
     OBJECT_CHECK(RemotePortStream, (obj), TYPE_REMOTE_PORT_STREAM)

typedef struct RemotePortStream RemotePortStream;

struct RemotePortStream {
    DeviceState parent_obj;

    RemotePort *rp;
    uint32_t rp_dev;
    uint16_t stream_width;

    StreamSlave *tx_dev;

    StreamCanPushNotifyFn notify;
    void *notify_opaque;

    uint8_t *buf;
    struct rp_pkt pkt;

    bool rsp_pending;
    uint32_t current_id;
};

static void rp_stream_notify(void *opaque)
{
    RemotePortStream *s = REMOTE_PORT_STREAM(opaque);

    /* FIXME: This is completely bogus.  */
    if (s->buf && stream_can_push(s->tx_dev, rp_stream_notify, s)) {
        RemotePortDynPkt rsp;
        struct rp_encode_busaccess_in in = {0};
        size_t pktlen = sizeof(struct rp_pkt_busaccess_ext_base);
        size_t enclen;
        int64_t delay = 0; /* FIXME - Implement */

        size_t ret = stream_push(s->tx_dev, s->buf, 4, 0);
        assert(ret == 4);
        s->buf = NULL;

        memset(&rsp, 0, sizeof(rsp));
        rp_dpkt_alloc(&rsp, pktlen);
        rp_encode_busaccess_in_rsp_init(&in, &s->pkt);
        in.clk = s->pkt.busaccess.timestamp + delay;
        enclen = rp_encode_busaccess(rp_get_peer(s->rp),
                                     &rsp.pkt->busaccess_ext_base,
                                     &in);
        assert(enclen <= pktlen);
        trace_remote_port_stream_tx_busaccess(rp_cmd_to_string(in.cmd),
            in.id, in.flags, in.dev, in.addr, in.size, in.attr);

        rp_write(s->rp, (void *)rsp.pkt, pktlen);
    }
}

static void rp_stream_write(RemotePortDevice *obj, struct rp_pkt *pkt)
{
    RemotePortStream *s = REMOTE_PORT_STREAM(obj);

    trace_remote_port_stream_rx_busaccess(rp_cmd_to_string(pkt->hdr.cmd),
        pkt->hdr.id, pkt->hdr.flags, pkt->hdr.dev, pkt->busaccess.addr,
        pkt->busaccess.len, pkt->busaccess.attributes);

    assert(pkt->busaccess.width == 0);
    assert(pkt->busaccess.stream_width == pkt->busaccess.len);
    assert(pkt->busaccess.addr == 0);

    if (pkt->hdr.flags & RP_PKT_FLAGS_response) {
        /* FXIME - probably need to do syncs and stuff */
        assert(s->rsp_pending);
        s->rsp_pending = false;
        if (s->notify) {
            StreamCanPushNotifyFn notify = s->notify;
            s->notify = NULL;
            notify(s->notify_opaque);
        }
    } else {
        assert(!s->buf);
        s->buf = g_memdup(pkt + 1, 4);
        s->pkt = *pkt;
        rp_stream_notify(s);
    }
}

static bool rp_stream_stream_can_push(StreamSlave *obj,
                                            StreamCanPushNotifyFn notify,
                                            void *notify_opaque)
{
    RemotePortStream *s = REMOTE_PORT_STREAM(obj);

    if (s->rsp_pending) {
        s->notify = notify;
        s->notify_opaque = notify_opaque;
        return false;
    }
    return true;
}

static size_t rp_stream_stream_push(StreamSlave *obj, uint8_t *buf,
                                    size_t len, bool eop)
{
    RemotePortStream *s = REMOTE_PORT_STREAM(obj);
    RemotePortDynPkt rsp;
    struct rp_pkt_busaccess_ext_base pkt;
    struct rp_encode_busaccess_in in = {0};
    uint64_t rp_attr = eop ? RP_BUS_ATTR_EOP : 0;
    int64_t clk;
    int enclen;

    clk = rp_normalized_vmclk(s->rp);

    in.cmd = RP_CMD_write;
    in.id = rp_new_id(s->rp);
    in.dev = s->rp_dev;
    in.clk = clk;
    in.attr = rp_attr;
    in.size = len;
    in.stream_width = s->stream_width;
    enclen = rp_encode_busaccess(rp_get_peer(s->rp), &pkt, &in);

    trace_remote_port_stream_tx_busaccess(rp_cmd_to_string(in.cmd),
        in.id, in.flags, in.dev, in.addr, in.size, in.attr);

    rp_rsp_mutex_lock(s->rp);
    rp_write(s->rp, (void *) &pkt, enclen);
    rp_write(s->rp, buf, len);
    rsp = rp_wait_resp(s->rp);
    assert(rsp.pkt->hdr.id == be32_to_cpu(pkt.hdr.id));

    trace_remote_port_stream_rx_busaccess(
        rp_cmd_to_string(rsp.pkt->hdr.cmd), rsp.pkt->hdr.id,
        rsp.pkt->hdr.flags, rsp.pkt->hdr.dev, rsp.pkt->busaccess.addr,
        rsp.pkt->busaccess.len, rsp.pkt->busaccess.attributes);

    rp_dpkt_invalidate(&rsp);
    rp_rsp_mutex_unlock(s->rp);
    rp_restart_sync_timer(s->rp);
    return len;
}

static void rp_stream_init(Object *obj)
{
    RemotePortStream *s = REMOTE_PORT_STREAM(obj);

    object_property_add_link(obj, "axistream-connected", TYPE_STREAM_SLAVE,
                             (Object **)&s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&s->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortStream, rp_dev, 0),
    DEFINE_PROP_UINT16("stream-width", RemotePortStream, stream_width, 4),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp_stream_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(oc);
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);

    ssc->push = rp_stream_stream_push;
    ssc->can_push = rp_stream_stream_can_push;
    device_class_set_props(dc, rp_properties);
    rpdc->ops[RP_CMD_write] = rp_stream_write;
}

static const TypeInfo rp_stream_info = {
    .name          = TYPE_REMOTE_PORT_STREAM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemotePortStream),
    .class_init    = rp_stream_class_init,
    .instance_init = rp_stream_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    }
};

static void rp_stream_register_types(void)
{
    type_register_static(&rp_stream_info);
}

type_init(rp_stream_register_types)
