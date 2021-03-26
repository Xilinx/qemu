/*
 * QEMU remote port memory slave. Read and write transactions
 * recieved from the remote port are translated into an address space.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "trace.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"
#include "hw/remote-port-memory-slave.h"

#ifndef REMOTE_PORT_ERR_DEBUG
#define REMOTE_PORT_DEBUG_LEVEL 0
#else
#define REMOTE_PORT_DEBUG_LEVEL 1
#endif

#define DB_PRINT_L(level, ...) do { \
    if (REMOTE_PORT_DEBUG_LEVEL > level) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

/* Slow path dealing with odd stuff like byte-enables.  */
static MemTxResult process_data_slow(RemotePortMemorySlave *s,
                                     struct rp_pkt *pkt,
                                     DMADirection dir,
                                     uint8_t *data, uint8_t *byte_en)
{
    unsigned int i;
    unsigned int byte_en_len = pkt->busaccess_ext_base.byte_enable_len;
    unsigned int sw = pkt->busaccess.stream_width;
    MemTxResult ret = MEMTX_OK;

    assert(sw);

    for (i = 0; i < pkt->busaccess.len; i++) {
        if (byte_en && !byte_en[i % byte_en_len]) {
            continue;
        }

        ret = dma_memory_rw_attr(&s->as, pkt->busaccess.addr + i % sw,
                                 data + i, 1, dir, s->attr);

        if (ret != MEMTX_OK) {
            break;
        }
    }

    return ret;
}

static void rp_cmd_rw(RemotePortMemorySlave *s, struct rp_pkt *pkt,
                      DMADirection dir)
{
    size_t pktlen = sizeof(struct rp_pkt_busaccess_ext_base);
    struct rp_encode_busaccess_in in = {0};
    size_t enclen;
    int64_t delay;
    uint8_t *data = NULL;
    uint8_t *byte_en;
    MemTxResult ret;
    AddressSpace *as = NULL;

    byte_en = rp_busaccess_byte_en_ptr(s->peer, &pkt->busaccess_ext_base);

    if (dir == DMA_DIRECTION_TO_DEVICE) {
        pktlen += pkt->busaccess.len;
    } else {
        data = rp_busaccess_rx_dataptr(s->peer, &pkt->busaccess_ext_base);
    }

    assert(pkt->busaccess.width == 0);
    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    rp_dpkt_alloc(&s->rsp, pktlen);
    if (dir == DMA_DIRECTION_TO_DEVICE) {
        data = rp_busaccess_tx_dataptr(s->peer,
                                       &s->rsp.pkt->busaccess_ext_base);
    }
    if (dir == DMA_DIRECTION_FROM_DEVICE && REMOTE_PORT_DEBUG_LEVEL > 0) {
        DB_PRINT_L(0, "address: %" PRIx64 "\n", pkt->busaccess.addr);
        qemu_hexdump((const char *)data, stderr, ": write: ",
                     pkt->busaccess.len);
    }
    trace_remote_port_memory_slave_rx_busaccess(rp_cmd_to_string(pkt->hdr.cmd),
        pkt->hdr.id, pkt->hdr.flags, pkt->hdr.dev, pkt->busaccess.addr,
        pkt->busaccess.len, pkt->busaccess.attributes);

    s->attr.secure = !!(pkt->busaccess.attributes & RP_BUS_ATTR_SECURE);
    s->attr.requester_id = pkt->busaccess.master_id;

    if (pkt->busaccess.attributes & RP_BUS_ATTR_PHYS_ADDR) {
        if (s->ats_cache) {
            IOMMUTLBEntry *iotlb =
                rp_ats_cache_lookup_translation(s->ats_cache,
                                                pkt->busaccess.addr,
                                                pkt->busaccess.len);
            if (iotlb) {
                as = iotlb->target_as;
            }
        }
    } else {
        as = &s->as;
    }
    if (as) {
        if (byte_en || pkt->busaccess.stream_width != pkt->busaccess.len) {
            ret = process_data_slow(s, pkt, dir, data, byte_en);
        } else {
            ret = dma_memory_rw_attr(as, pkt->busaccess.addr, data,
                                 pkt->busaccess.len, dir, s->attr);
        }
    } else {
        ret = MEMTX_ERROR;
    }

    if (dir == DMA_DIRECTION_TO_DEVICE && REMOTE_PORT_DEBUG_LEVEL > 0) {
        DB_PRINT_L(0, "address: %" PRIx64 "\n", pkt->busaccess.addr);
        qemu_hexdump((const char *)data, stderr, ": read: ",
                     pkt->busaccess.len);
    }
    /* delay here could be set to the annotated cost of doing issuing
       these accesses. QEMU doesn't support this kind of annotations
       at the moment. So we just clear the delay.  */
    delay = 0;

    rp_encode_busaccess_in_rsp_init(&in, pkt);
    in.clk = pkt->busaccess.timestamp + delay;

    switch (ret) {
    case MEMTX_ERROR:
        in.attr |= RP_RESP_BUS_GENERIC_ERROR << RP_BUS_RESP_SHIFT;
        break;
    case MEMTX_DECODE_ERROR:
        in.attr |= RP_RESP_ADDR_ERROR << RP_BUS_RESP_SHIFT;
        break;
    default:
        /* MEMTX_OK maps to RP_RESP_OK. */
        break;
    }

    enclen = rp_encode_busaccess(s->peer, &s->rsp.pkt->busaccess_ext_base,
                                 &in);
    assert(enclen <= pktlen);

    trace_remote_port_memory_slave_tx_busaccess(rp_cmd_to_string(in.cmd),
        in.id, in.flags, in.dev, in.addr, in.size, in.attr);

    rp_write(s->rp, (void *)s->rsp.pkt, enclen);
}

static void rp_memory_slave_realize(DeviceState *dev, Error **errp)
{
    RemotePortMemorySlave *s = REMOTE_PORT_MEMORY_SLAVE(dev);

    s->peer = rp_get_peer(s->rp);
    address_space_init(&s->as, s->mr ? s->mr : get_system_memory(), "dma");
}

static void rp_memory_slave_write(RemotePortDevice *s, struct rp_pkt *pkt)
{
    return rp_cmd_rw(REMOTE_PORT_MEMORY_SLAVE(s), pkt,
                     DMA_DIRECTION_FROM_DEVICE);
}

static void rp_memory_slave_read(RemotePortDevice *s, struct rp_pkt *pkt)
{
    return rp_cmd_rw(REMOTE_PORT_MEMORY_SLAVE(s), pkt,
                     DMA_DIRECTION_TO_DEVICE);
}

static void rp_memory_slave_init(Object *obj)
{
    RemotePortMemorySlave *rpms = REMOTE_PORT_MEMORY_SLAVE(obj);

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "mr", TYPE_MEMORY_REGION,
                             (Object **)&rpms->mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "rp-ats-cache", TYPE_REMOTE_PORT_ATS_CACHE,
                             (Object **)&rpms->ats_cache,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void rp_memory_slave_unrealize(DeviceState *dev)
{
    RemotePortMemorySlave *s = REMOTE_PORT_MEMORY_SLAVE(dev);

    address_space_destroy(&s->as);
}

static void rp_memory_slave_class_init(ObjectClass *oc, void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    rpdc->ops[RP_CMD_write] = rp_memory_slave_write;
    rpdc->ops[RP_CMD_read] = rp_memory_slave_read;
    dc->realize = rp_memory_slave_realize;
    dc->unrealize = rp_memory_slave_unrealize;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_MEMORY_SLAVE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RemotePortMemorySlave),
    .instance_init = rp_memory_slave_init,
    .class_init    = rp_memory_slave_class_init,
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
