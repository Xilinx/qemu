/*
 * QEMU remote port PCI device.
 *
 * Copyright (c) 2016 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/remote-port.h"
#include "hw/remote-port-device.h"
#include "hw/remote-port-memory-slave.h"

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

#define TYPE_REMOTE_PORT_PCI_DEVICE "remote-port-pci-device"
#define REMOTE_PORT_PCI_DEVICE(obj) \
        OBJECT_CHECK(RemotePortPCIDevice, (obj), \
                     TYPE_REMOTE_PORT_PCI_DEVICE)

#define REMOTE_PORT_PCI_DEVICE_PARENT_CLASS \
    object_class_get_parent( \
            object_class_by_name(TYPE_REMOTE_PORT_PCI_DEVICE))

/*
 * RP-dev allocation.
 *
 * We allocate 20 RP devices for a single PCIe device.
 * dev          Function
 * 0            Config space
 * 1            Legacy IRQ
 * 2            Reserved for Messages
 * 3            DMA from the End-point towards us.
 * 4 - 9        Reserved
 * 10 - 20      IO or Memory Mapped BARs (6 + 4 reserved)
 */
#define RPDEV_PCI_CONFIG        0
#define RPDEV_PCI_LEGACY_IRQ    1
#define RPDEV_PCI_MESSAGES      2
#define RPDEV_PCI_DMA           3
#define RPDEV_PCI_BAR_BASE     10

typedef struct RemotePortPCIDevice RemotePortPCIDevice;

typedef struct RemotePortMap {
    RemotePortPCIDevice *parent;
    MemoryRegion iomem;
    uint32_t rp_dev;
    uint64_t offset;
} RemotePortMap;

struct RemotePortPCIDevice {
    /*< private >*/
    PCIDevice parent_obj;

    RemotePortMemorySlave *rp_dma;

    /*< public >*/
    RemotePortMap *maps;

    struct {
        uint32_t rp_dev;
        uint32_t nr_io_bars;
        uint32_t nr_mm_bars;
        uint64_t bar_size[6];
        uint32_t nr_devs;
        uint32_t vendor_id;
        uint32_t device_id;
        uint32_t revision;
        uint32_t class_id;
        uint8_t prog_if;
        uint8_t irq_pin;
    } cfg;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
};

static uint64_t rp_io_read(void *opaque, hwaddr addr, unsigned size,
                           MemTxAttrs attr)
{
    RemotePortMap *map = opaque;
    RemotePortPCIDevice *s = map->parent;
    struct rp_pkt_busaccess_ext_base pkt;
    struct rp_encode_busaccess_in in = {0};
    uint64_t value = 0;
    int64_t rclk;
    uint8_t *data;
    int len;
    int i;
    RemotePortDynPkt rsp;

    DB_PRINT_L(1, "\n");

    in.cmd = RP_CMD_read;
    in.id = rp_new_id(s->rp);
    in.flags = 0;
    in.dev = map->rp_dev;
    in.clk = rp_normalized_vmclk(s->rp);
    in.master_id = attr.requester_id;
    in.addr = addr;
    in.attr |= attr.secure ? RP_BUS_ATTR_SECURE : 0;
    in.size = size;
    in.width = 0;
    in.stream_width = size;
    len = rp_encode_busaccess(s->peer, &pkt, &in);
    rp_rsp_mutex_lock(s->rp);
    rp_write(s->rp, (void *) &pkt, len);

    rsp = rp_wait_resp(s->rp);
    /* We dont support out of order answers yet.  */
    assert(rsp.pkt->hdr.id == be32_to_cpu(pkt.hdr.id));

    data = rp_busaccess_rx_dataptr(s->peer, &rsp.pkt->busaccess_ext_base);
    for (i = 0; i < size; i++) {
        value |= data[i] << (i * 8);
    }
    rclk = rsp.pkt->busaccess.timestamp;
    rp_dpkt_invalidate(&rsp);
    rp_rsp_mutex_unlock(s->rp);
    rp_sync_vmclock(s->rp, in.clk, rclk);

    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(s->rp);
    rp_leave_iothread(s->rp);
    DB_PRINT_L(0, "addr: %" HWADDR_PRIx " data: %" PRIx64 "\n", addr, value);
    return value;
}

static void rp_io_write(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size, MemTxAttrs attr)
{
    RemotePortMap *map = opaque;
    RemotePortPCIDevice *s = map->parent;
    int64_t rclk;
    RemotePortDynPkt rsp;

    struct  {
        struct rp_pkt_busaccess_ext_base pkt;
        uint8_t reserved[8];
    } pay;
    uint8_t *data = rp_busaccess_tx_dataptr(s->peer, &pay.pkt);
    struct rp_encode_busaccess_in in = {0};
    int i;
    int len;

    DB_PRINT_L(0, "addr: %" HWADDR_PRIx " data: %" PRIx64 "\n", addr, value);

    for (i = 0; i < 8; i++) {
        data[i] = value >> (i * 8);
    }

    assert(size <= 8);

    in.cmd = RP_CMD_write;
    in.id = rp_new_id(s->rp);
    in.dev = map->rp_dev;
    in.clk = rp_normalized_vmclk(s->rp);
    in.master_id = attr.requester_id;
    in.addr = addr;
    in.attr |= attr.secure ? RP_BUS_ATTR_SECURE : 0;
    in.size = size;
    in.stream_width = size;
    len = rp_encode_busaccess(s->peer, &pay.pkt, &in);

    rp_rsp_mutex_lock(s->rp);

    rp_write(s->rp, (void *) &pay, len + size);

    rsp = rp_wait_resp(s->rp);

    /* We dont support out of order answers yet.  */
    assert(rsp.pkt->hdr.id == be32_to_cpu(pay.pkt.hdr.id));
    rclk = rsp.pkt->busaccess.timestamp;
    rp_dpkt_invalidate(&rsp);
    rp_rsp_mutex_unlock(s->rp);
    rp_sync_vmclock(s->rp, in.clk, rclk);
    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(s->rp);
    rp_leave_iothread(s->rp);
    DB_PRINT_L(1, "\n");
}

static void rp_io_access(MemoryTransaction *tr)
{
    MemTxAttrs attr = tr->attr;
    void *opaque = tr->opaque;
    hwaddr addr = tr->addr;
    unsigned size = tr->size;
    uint64_t value = tr->data.u64;;
    bool is_write = tr->rw;

    if (is_write) {
        rp_io_write(opaque, addr, value, size, attr);
    } else {
        tr->data.u64 = rp_io_read(opaque, addr, size, attr);
    }
}

static const MemoryRegionOps rp_ops = {
    .access = rp_io_access,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_pci_write_config(PCIDevice *pci_dev, uint32_t addr,
                                uint32_t value, int size)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(pci_dev);
    int64_t rclk;
    RemotePortDynPkt rsp;

    struct  {
        struct rp_pkt_busaccess_ext_base pkt;
        uint8_t reserved[8];
    } pay;
    uint8_t *data = rp_busaccess_tx_dataptr(s->peer, &pay.pkt);
    struct rp_encode_busaccess_in in = {0};
    int i;
    int len;

    DB_PRINT_L(0, "addr: %x data: %x\n", addr, value);

    for (i = 0; i < 8; i++) {
        data[i] = value >> (i * 8);
    }

    assert(size <= 8);
    in.clk = rp_normalized_vmclk(s->rp);
    in.id = rp_new_id(s->rp);

    in.cmd = RP_CMD_write;
    in.dev = s->cfg.rp_dev;
    in.addr = addr;
    in.size = size;
    in.stream_width = size;
    len = rp_encode_busaccess(s->peer, &pay.pkt, &in);

    rp_rsp_mutex_lock(s->rp);

    rp_write(s->rp, (void *) &pay, len + size);

    rsp = rp_wait_resp(s->rp);

    /* We dont support out of order answers yet.  */
    assert(rsp.pkt->hdr.id == be32_to_cpu(pay.pkt.hdr.id));
    rclk = rsp.pkt->busaccess.timestamp;
    rp_dpkt_invalidate(&rsp);
    rp_rsp_mutex_unlock(s->rp);
    rp_sync_vmclock(s->rp, in.clk, rclk);
    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(s->rp);
    rp_leave_iothread(s->rp);

    pci_default_write_config(pci_dev, addr, value, size);
    DB_PRINT_L(1, "\n");
}

static void rp_gpio_interrupt(RemotePortDevice *rpd, struct rp_pkt *pkt)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(rpd);
    PCIDevice *d = PCI_DEVICE(s);
    int irq = pkt->interrupt.line;
    int level = pkt->interrupt.val;

    DB_PRINT_L(0, "%s: irq[%d]=%d\n", __func__, irq, level);
    pci_set_irq(d, level);
}

static void rp_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(pci_dev);
    AddressSpace *as;
    int i;

    assert(s->rp);
    s->peer = rp_get_peer(s->rp);

    /* Update device IDs after our properties have been set.  */
    pci_config_set_vendor_id(pci_dev->config, s->cfg.vendor_id);
    pci_config_set_device_id(pci_dev->config, s->cfg.device_id);
    pci_config_set_revision(pci_dev->config, s->cfg.revision);
    pci_config_set_class(pci_dev->config, s->cfg.class_id);
    pci_dev->config[PCI_CLASS_PROG] = s->cfg.prog_if;
    pci_dev->config[PCI_INTERRUPT_PIN] = s->cfg.irq_pin;

    /* The remote peer may want to snoop on CFG writes.  */
    pci_dev->config_write = rp_pci_write_config;

    /* Create and hook up the BARs.  */
    s->maps = g_new0(typeof(*s->maps), s->cfg.nr_io_bars + s->cfg.nr_mm_bars);

    for (i = 0; i < s->cfg.nr_io_bars; i++) {
        char *name = g_strdup_printf("rp-pci-io-%d", i);
        memory_region_init_io(&s->maps[i].iomem, OBJECT(s), &rp_ops,
                              &s->maps[i], name, s->cfg.bar_size[i]);
        pci_register_bar(pci_dev, i, PCI_BASE_ADDRESS_SPACE_IO,
                         &s->maps[i].iomem);
        s->maps[i].rp_dev = RPDEV_PCI_BAR_BASE + i;
        s->maps[i].parent = s;
        g_free(name);
    }
    for (; i < s->cfg.nr_mm_bars; i++) {
        char *name = g_strdup_printf("rp-pci-mmio-%d", i);
        memory_region_init_io(&s->maps[i].iomem, OBJECT(s), &rp_ops,
                              &s->maps[i], name, s->cfg.bar_size[i]);
        pci_register_bar(pci_dev, s->cfg.nr_io_bars + i,
                         PCI_BASE_ADDRESS_SPACE_MEMORY,
                         &s->maps[i].iomem);
        s->maps[i].rp_dev = RPDEV_PCI_BAR_BASE + i;
        s->maps[i].parent = s;
        g_free(name);
    }

    /* Setup the DMA dev.  */
    rp_device_attach(OBJECT(s->rp), OBJECT(s->rp_dma), 0,
                            s->cfg.rp_dev + RPDEV_PCI_DMA, &error_abort);

    as = pci_get_address_space(pci_dev);
    object_property_set_link(OBJECT(s->rp_dma), OBJECT(as->root), "mr",
                             &error_abort);

    object_property_set_bool(OBJECT(s->rp_dma), true, "realized", &error_abort);
}

static void rp_pci_init(Object *obj)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(obj);
    Object *tmp_obj;

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&s->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG,
                             &error_abort);


    tmp_obj = object_new(TYPE_REMOTE_PORT_MEMORY_SLAVE);
    s->rp_dma = REMOTE_PORT_MEMORY_SLAVE(tmp_obj);

    object_property_add_child(obj, "rp-dma", tmp_obj, &error_abort);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortPCIDevice, cfg.rp_dev, 0),
    DEFINE_PROP_UINT32("nr-io-bars", RemotePortPCIDevice, cfg.nr_io_bars, 0),
    DEFINE_PROP_UINT32("nr-mm-bars", RemotePortPCIDevice, cfg.nr_mm_bars, 0),
    DEFINE_PROP_UINT32("vendor-id", RemotePortPCIDevice, cfg.vendor_id, 0),
    DEFINE_PROP_UINT32("device-id", RemotePortPCIDevice, cfg.device_id, 0),
    DEFINE_PROP_UINT32("revision", RemotePortPCIDevice, cfg.revision, 0),
    DEFINE_PROP_UINT32("class-id", RemotePortPCIDevice, cfg.class_id, 0),
    DEFINE_PROP_UINT8("prog-if", RemotePortPCIDevice, cfg.prog_if, 1),
    DEFINE_PROP_UINT8("irq-pin", RemotePortPCIDevice, cfg.irq_pin, 1),

    DEFINE_PROP_UINT64("bar-size0", RemotePortPCIDevice,
                                    cfg.bar_size[0], 0x1000),
    DEFINE_PROP_UINT64("bar-size1", RemotePortPCIDevice,
                                    cfg.bar_size[1], 0x1000),
    DEFINE_PROP_UINT64("bar-size2", RemotePortPCIDevice,
                                    cfg.bar_size[2], 0x1000),
    DEFINE_PROP_UINT64("bar-size3", RemotePortPCIDevice,
                                    cfg.bar_size[3], 0x1000),
    DEFINE_PROP_UINT64("bar-size4", RemotePortPCIDevice,
                                    cfg.bar_size[4], 0x1000),
    DEFINE_PROP_UINT64("bar-size5", RemotePortPCIDevice,
                                    cfg.bar_size[5], 0x1000),

    /* These are read-only.  */
    DEFINE_PROP_UINT32("nr-devs", RemotePortPCIDevice, cfg.nr_devs, 20),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_pci_class_init(ObjectClass *oc, void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oc);

    dc->desc = "Remote-Port PCI Device";
    device_class_set_props(dc, rp_properties);

    rpdc->ops[RP_CMD_interrupt] = rp_gpio_interrupt;
    k->realize = rp_pci_realize;
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = 0;
    k->revision = 0;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RemotePortPCIDevice),
    .instance_init = rp_pci_init,
    .class_init    = rp_pci_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
