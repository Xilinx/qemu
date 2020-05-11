/*
 * QEMU remote port memory master.
 *
 * Copyright (c) 2014 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port.h"
#include "hw/remote-port-device.h"

#include "hw/fdt_generic_util.h"

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

#define TYPE_REMOTE_PORT_MEMORY_MASTER "remote-port-memory-master"
#define REMOTE_PORT_MEMORY_MASTER(obj) \
        OBJECT_CHECK(RemotePortMemoryMaster, (obj), \
                     TYPE_REMOTE_PORT_MEMORY_MASTER)

#define REMOTE_PORT_MEMORY_MASTER_PARENT_CLASS \
    object_class_get_parent( \
            object_class_by_name(TYPE_REMOTE_PORT_MEMORY_MASTER))

typedef struct RemotePortMemoryMaster RemotePortMemoryMaster;

typedef struct RemotePortMap {
    RemotePortMemoryMaster *parent;
    MemoryRegion iomem;
    uint64_t offset;
} RemotePortMap;

struct RemotePortMemoryMaster {
    /* private */
    SysBusDevice parent;

    MemoryRegionOps *rp_ops;
    RemotePortMap *mmaps;

    /* public */
    uint32_t map_num;
    uint64_t map_offset;
    uint64_t map_size;
    uint32_t rp_dev;
    bool relative;
    uint32_t max_access_size;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
};

#define RP_MAX_ACCESS_SIZE 128

static void rp_io_access(MemoryTransaction *tr)
{
    uint64_t addr = tr->addr;
    RemotePortMap *map = tr->opaque;
    RemotePortMemoryMaster *s = map->parent;
    int64_t rclk;
    RemotePortRespSlot *rsp_slot;
    RemotePortDynPkt *rsp;
    struct  {
        struct rp_pkt_busaccess_ext_base pkt;
        uint8_t reserved[RP_MAX_ACCESS_SIZE];
    } pay;
    uint8_t *data = rp_busaccess_tx_dataptr(s->peer, &pay.pkt);
    struct rp_encode_busaccess_in in = {0};
    int i;
    int len;

    DB_PRINT_L(0, "addr: %" HWADDR_PRIx " data: %" PRIx64 "\n",
               addr, tr->data.u64);

    if (tr->rw) {
        /* Data up to 8 bytes is passed as values.  */
        if (tr->size <= 8) {
            for (i = 0; i < tr->size; i++) {
                data[i] = tr->data.u64 >> (i * 8);
            }
        } else {
            memcpy(data, tr->data.p8, tr->size);
        }
    }

    addr += s->relative ? 0 : map->offset;

    in.cmd = tr->rw ? RP_CMD_write : RP_CMD_read;
    in.id = rp_new_id(s->rp);
    in.dev = s->rp_dev;
    in.clk = rp_normalized_vmclk(s->rp);
    in.master_id = tr->attr.requester_id;
    in.addr = addr;
    in.attr |= tr->attr.secure ? RP_BUS_ATTR_SECURE : 0;
    in.size = tr->size;
    in.stream_width = tr->size;
    len = rp_encode_busaccess(s->peer, &pay.pkt, &in);
    len += tr->rw ? tr->size : 0;

    rp_rsp_mutex_lock(s->rp);
    rp_write(s->rp, (void *) &pay, len);

    rsp_slot = rp_dev_wait_resp(s->rp, in.dev, in.id);
    rsp = &rsp_slot->rsp;

    /* We dont support out of order answers yet.  */
    assert(rsp->pkt->hdr.id == in.id);

    if (!tr->rw) {
        data = rp_busaccess_rx_dataptr(s->peer, &rsp->pkt->busaccess_ext_base);
        /* Data up to 8 bytes is return as values.  */
        if (tr->size <= 8) {
            for (i = 0; i < tr->size; i++) {
                tr->data.u64 |= data[i] << (i * 8);
            }
        } else {
            memcpy(tr->data.p8, data, tr->size);
        }
    }

    rclk = rsp->pkt->busaccess.timestamp;
    rp_resp_slot_done(s->rp, rsp_slot);
    rp_rsp_mutex_unlock(s->rp);
    rp_sync_vmclock(s->rp, in.clk, rclk);
    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(s->rp);
    rp_leave_iothread(s->rp);
    DB_PRINT_L(1, "\n");
}

static const MemoryRegionOps rp_ops_template = {
    .access = rp_io_access,
    .valid.max_access_size = RP_MAX_ACCESS_SIZE,
    .impl.unaligned = false,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_memory_master_realize(DeviceState *dev, Error **errp)
{
    RemotePortMemoryMaster *s = REMOTE_PORT_MEMORY_MASTER(dev);
    int i;

    /* Sanity check max access size.  */
    if (s->max_access_size > RP_MAX_ACCESS_SIZE) {
        error_setg(errp, "%s: max-access-size %d too large! MAX is %d",
                   TYPE_REMOTE_PORT_MEMORY_MASTER, s->max_access_size,
                   RP_MAX_ACCESS_SIZE);
        return;
    }

    if (s->max_access_size < 4) {
        error_setg(errp, "%s: max-access-size %d too small! MIN is 4",
                   TYPE_REMOTE_PORT_MEMORY_MASTER, s->max_access_size);
        return;
    }

    assert(s->rp);
    s->peer = rp_get_peer(s->rp);

    /* Create a single static region if configuration says so.  */
    if (s->map_num) {
        /* Initialize rp_ops from template.  */
        s->rp_ops = g_malloc(sizeof *s->rp_ops);
        memcpy(s->rp_ops, &rp_ops_template, sizeof *s->rp_ops);
        s->rp_ops->valid.max_access_size = s->max_access_size;

        s->mmaps = g_new0(typeof(*s->mmaps), s->map_num);
        for (i = 0; i < s->map_num; ++i) {
            char *name = g_strdup_printf("rp-%d", i);

            s->mmaps[i].offset = s->map_offset;
            memory_region_init_io(&s->mmaps[i].iomem, OBJECT(dev), s->rp_ops,
                                  &s->mmaps[i], name, s->map_size);
            sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmaps[i].iomem);
            s->mmaps[i].parent = s;
            g_free(name);
        }
    }
}

static void rp_memory_master_init(Object *obj)
{
    RemotePortMemoryMaster *rpms = REMOTE_PORT_MEMORY_MASTER(obj);
    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG,
                             &error_abort);
}

static bool rp_parse_reg(FDTGenericMMap *obj, FDTGenericRegPropInfo reg,
                         Error **errp)
{
    RemotePortMemoryMaster *s = REMOTE_PORT_MEMORY_MASTER(obj);
    FDTGenericMMapClass *parent_fmc =
        FDT_GENERIC_MMAP_CLASS(REMOTE_PORT_MEMORY_MASTER_PARENT_CLASS);
    int i;

    /* Initialize rp_ops from template.  */
    s->rp_ops = g_malloc(sizeof *s->rp_ops);
    memcpy(s->rp_ops, &rp_ops_template, sizeof *s->rp_ops);
    s->rp_ops->valid.max_access_size = s->max_access_size;

    s->mmaps = g_new0(typeof(*s->mmaps), reg.n);
    for (i = 0; i < reg.n; ++i) {
        char *name = g_strdup_printf("rp-%d", i);

        s->mmaps[i].offset = reg.a[i];
        memory_region_init_io(&s->mmaps[i].iomem, OBJECT(obj), s->rp_ops,
                              &s->mmaps[i], name, reg.s[i]);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmaps[i].iomem);
        s->mmaps[i].parent = s;
        g_free(name);
    }

    return parent_fmc ? parent_fmc->parse_reg(obj, reg, errp) : false;
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("map-num", RemotePortMemoryMaster, map_num, 0),
    DEFINE_PROP_UINT64("map-offset", RemotePortMemoryMaster, map_offset, 0),
    DEFINE_PROP_UINT64("map-size", RemotePortMemoryMaster, map_size, 0),
    DEFINE_PROP_UINT32("rp-chan0", RemotePortMemoryMaster, rp_dev, 0),
    DEFINE_PROP_BOOL("relative", RemotePortMemoryMaster, relative, false),
    DEFINE_PROP_UINT32("max-access-size", RemotePortMemoryMaster,
                       max_access_size, RP_MAX_ACCESS_SIZE),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_memory_master_class_init(ObjectClass *oc, void *data)
{
    FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    device_class_set_props(dc, rp_properties);
    dc->realize = rp_memory_master_realize;
    fmc->parse_reg = rp_parse_reg;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_MEMORY_MASTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RemotePortMemoryMaster),
    .instance_init = rp_memory_master_init,
    .class_init    = rp_memory_master_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_MMAP },
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
