/*
 * Xilinx PMXC Key Transfer Remote-Port Transport
 *
 * Bridges the PmxcKeyXferIf interface across remote-port, letting the ASU
 * key vault and the PMXC AES key transfer engine live in different QEMU
 * instances.
 *
 * The device is symmetric: one instance is placed in each QEMU, both sharing
 * a remote-port channel. Every call arriving on the local interface is
 * encoded and sent to the peer, and every packet received from the peer is
 * replayed on the local interface object linked through
 * "pmxc-key-xfer-peer". This carries traffic in both directions, i.e.
 * asu_ready() from the ASU towards the PMXC AES, and send_key()/done() the
 * other way around.
 *
 * Copyright Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/remote-port.h"
#include "hw/remote-port-device.h"
#include "hw/crypto/xlnx-pmxc-key-transfer-rp.h"
#include "hw/crypto/xlnx-pmxc-key-transfer.h"
#include "trace.h"

enum {
    RP_KT_OP_ASU_READY = 0,
    RP_KT_OP_SEND_KEY  = 1,
    RP_KT_OP_DONE      = 2,
};

/* Wire format. Byte sized fields only, no endian conversion needed. */
typedef struct RpKtPayload {
    uint8_t op;
    uint8_t n;    /* Key index, RP_KT_OP_SEND_KEY only. */
    uint8_t val;  /* Ready or done state. */
    uint8_t len;  /* Key length, RP_KT_OP_SEND_KEY only. */
    uint8_t key[XLNX_PMXC_KT_KEY_SIZE];
} QEMU_PACKED RpKtPayload;

struct XlnxPmxcKeyTransferRp {
    DeviceState parent_obj;

    /* Remote port connection */
    RemotePort *rp;
    uint32_t rp_dev;

    /* Local interface the calls received over remote-port are replayed on */
    PmxcKeyXferIf *peer;
};

static void rp_kt_send(XlnxPmxcKeyTransferRp *s, const RpKtPayload *payload)
{
    struct rp_pkt_busaccess_ext_base pkt;
    struct rp_encode_busaccess_in in = {0};
    int enclen;

    if (!s->rp) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rp-kt: no remote-port adaptor connected\n");
        return;
    }

    in.cmd = RP_CMD_write;
    in.id = rp_new_id(s->rp);
    in.flags = RP_PKT_FLAGS_posted;
    in.dev = s->rp_dev;
    in.clk = rp_normalized_vmclk(s->rp);
    in.attr = RP_BUS_ATTR_EOP;
    in.size = sizeof(*payload);
    in.stream_width = sizeof(*payload);
    enclen = rp_encode_busaccess(rp_get_peer(s->rp), &pkt, &in);

    rp_write(s->rp, (void *)&pkt, enclen);
    rp_write(s->rp, (void *)payload, sizeof(*payload));
}

static void rp_kt_asu_ready(PmxcKeyXferIf *kt, bool ready)
{
    XlnxPmxcKeyTransferRp *s = XLNX_PMXC_KEY_TRANSFER_RP(kt);
    RpKtPayload payload = {
        .op = RP_KT_OP_ASU_READY,
        .val = ready,
    };

    trace_rp_kt_tx_asu_ready(ready);
    rp_kt_send(s, &payload);
}

static void rp_kt_done(PmxcKeyXferIf *kt, bool done)
{
    XlnxPmxcKeyTransferRp *s = XLNX_PMXC_KEY_TRANSFER_RP(kt);
    RpKtPayload payload = {
        .op = RP_KT_OP_DONE,
        .val = done,
    };

    trace_rp_kt_tx_done(done);
    rp_kt_send(s, &payload);
}

static void rp_kt_send_key(PmxcKeyXferIf *kt, uint8_t n, uint8_t *key,
                           size_t len)
{
    XlnxPmxcKeyTransferRp *s = XLNX_PMXC_KEY_TRANSFER_RP(kt);
    RpKtPayload payload = {
        .op = RP_KT_OP_SEND_KEY,
        .n = n,
        .len = len,
    };

    if (len > XLNX_PMXC_KT_KEY_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rp-kt: key %u too large (%zu bytes)\n", n, len);
        return;
    }

    memcpy(payload.key, key, len);

    trace_rp_kt_tx_send_key(n, len);
    rp_kt_send(s, &payload);
}

static void rp_kt_write(RemotePortDevice *rpdev, struct rp_pkt *pkt)
{
    XlnxPmxcKeyTransferRp *s = XLNX_PMXC_KEY_TRANSFER_RP(rpdev);
    const RpKtPayload *payload;
    uint8_t key[XLNX_PMXC_KT_KEY_SIZE];
    size_t len;

    if (pkt->hdr.flags & RP_PKT_FLAGS_response) {
        return;
    }

    len = pkt->busaccess_ext_base.len;
    if (len != sizeof(*payload)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rp-kt: received unexpected payload size %zu\n", len);
        return;
    }

    payload = (const RpKtPayload *)rp_busaccess_rx_dataptr(
        rp_get_peer(s->rp), &pkt->busaccess_ext_base);

    switch (payload->op) {
    case RP_KT_OP_ASU_READY:
        trace_rp_kt_rx_asu_ready(payload->val);
        pmxc_kt_asu_ready(s->peer, payload->val);
        break;

    case RP_KT_OP_DONE:
        trace_rp_kt_rx_done(payload->val);
        pmxc_kt_done(s->peer, payload->val);
        break;

    case RP_KT_OP_SEND_KEY:
        if (payload->len > XLNX_PMXC_KT_KEY_SIZE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "rp-kt: received oversized key %u (%u bytes)\n",
                          payload->n, payload->len);
            break;
        }
        memcpy(key, payload->key, payload->len);
        trace_rp_kt_rx_send_key(payload->n, payload->len);
        pmxc_kt_send_key(s->peer, payload->n, key, payload->len);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "rp-kt: unknown op %u\n", payload->op);
        break;
    }
}

static void rp_kt_realize(DeviceState *dev, Error **errp)
{
    XlnxPmxcKeyTransferRp *s = XLNX_PMXC_KEY_TRANSFER_RP(dev);

    if (!s->rp) {
        error_setg(errp, "xlnx-pmxc-kt-rp: rp-adaptor0 link not set");
        return;
    }

    if (!s->peer) {
        error_setg(errp,
                   "xlnx-pmxc-kt-rp: pmxc-key-xfer-peer link not set");
        return;
    }
}

static void rp_kt_init(Object *obj)
{
    XlnxPmxcKeyTransferRp *s = XLNX_PMXC_KEY_TRANSFER_RP(obj);

    object_property_add_link(obj, "rp-adaptor0", TYPE_REMOTE_PORT,
                             (Object **)&s->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
}

static const Property rp_kt_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", XlnxPmxcKeyTransferRp, rp_dev, 0),
    DEFINE_PROP_LINK("pmxc-key-xfer-peer", XlnxPmxcKeyTransferRp, peer,
                     TYPE_PMXC_KEY_XFER_IF, PmxcKeyXferIf *),
};

static void rp_kt_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    PmxcKeyXferIfClass *ktc = PMXC_KEY_XFER_IF_CLASS(oc);

    dc->realize = rp_kt_realize;
    device_class_set_props(dc, rp_kt_properties);

    rpdc->ops[RP_CMD_write] = rp_kt_write;
    ktc->asu_ready = rp_kt_asu_ready;
    ktc->done = rp_kt_done;
    ktc->send_key = rp_kt_send_key;
}

static const TypeInfo rp_kt_info = {
    .name          = TYPE_XLNX_PMXC_KEY_TRANSFER_RP,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XlnxPmxcKeyTransferRp),
    .instance_init = rp_kt_init,
    .class_init    = rp_kt_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PMXC_KEY_XFER_IF },
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_kt_register_types(void)
{
    type_register_static(&rp_kt_info);
}

type_init(rp_kt_register_types)
