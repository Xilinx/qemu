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
#include "hw/qdev.h"

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


static void rp_cmd_rw(RemotePortMemorySlave *s, struct rp_pkt *pkt,
                      DMADirection dir)
{
    size_t pktlen = sizeof(struct rp_pkt_busaccess);
    size_t enclen;
    int64_t delay;
    uint8_t *data = NULL;

    if (dir == DMA_DIRECTION_TO_DEVICE) {
        pktlen += pkt->busaccess.len;
    } else {
        data = (uint8_t *)(pkt + 1);
    }

    assert(pkt->busaccess.width == 0);
    assert(pkt->busaccess.stream_width == pkt->busaccess.len);
    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    rp_dpkt_alloc(&s->rsp, pktlen);
    if (dir == DMA_DIRECTION_TO_DEVICE) {
        data = (uint8_t *)(s->rsp.pkt + 1);
    }
    if (dir == DMA_DIRECTION_FROM_DEVICE && REMOTE_PORT_DEBUG_LEVEL > 0) {
        DB_PRINT_L(0, "address: %" PRIx64 "\n", pkt->busaccess.addr);
        qemu_hexdump((const char *)data, stderr, ": write: ",
                     pkt->busaccess.len);
    }
    s->attr.secure = !!(pkt->busaccess.attributes & RP_BUS_ATTR_SECURE);
    s->attr.master_id = pkt->busaccess.master_id;
    dma_memory_rw_attr(s->as, pkt->busaccess.addr, data, pkt->busaccess.len,
                       dir, s->attr);
    if (dir == DMA_DIRECTION_TO_DEVICE && REMOTE_PORT_DEBUG_LEVEL > 0) {
        DB_PRINT_L(0, "address: %" PRIx64 "\n", pkt->busaccess.addr);
        qemu_hexdump((const char *)data, stderr, ": read: ",
                     pkt->busaccess.len);
    }
    /* delay here could be set to the annotated cost of doing issuing
       these accesses. QEMU doesn't support this kind of annotations
       at the moment. So we just clear the delay.  */
    delay = 0;

    enclen = (dir == DMA_DIRECTION_FROM_DEVICE ? rp_encode_write_resp :
                                                 rp_encode_read_resp)(
                    pkt->hdr.id, pkt->hdr.dev, &s->rsp.pkt->busaccess,
                    pkt->busaccess.timestamp + delay,
                    pkt->busaccess.master_id,
                    pkt->busaccess.addr,
                    pkt->busaccess.attributes,
                    pkt->busaccess.len,
                    pkt->busaccess.width,
                    pkt->busaccess.stream_width);
    assert(enclen == pktlen);

    rp_write(s->rp, (void *)s->rsp.pkt, pktlen);
}

static void rp_memory_slave_realize(DeviceState *dev, Error **errp)
{
    RemotePortMemorySlave *s = REMOTE_PORT_MEMORY_SLAVE(dev);

    /* FIXME: do something with per paster address spaces */
    s->as = s->mr ? address_space_init_shareable(s->mr, NULL)
                  : &address_space_memory;
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
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
    object_property_add_link(obj, "mr", TYPE_MEMORY_REGION,
                             (Object **)&rpms->mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void rp_memory_slave_class_init(ObjectClass *oc, void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    rpdc->ops[RP_CMD_write] = rp_memory_slave_write;
    rpdc->ops[RP_CMD_read] = rp_memory_slave_read;
    dc->realize = rp_memory_slave_realize;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_MEMORY_SLAVE,
    .parent        = TYPE_DEVICE,
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
