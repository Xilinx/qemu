/*
 * QEMU remote port PCI device.
 *
 * Copyright (c) 2016-2020 Xilinx Inc
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
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#include "hw/remote-port.h"
#include "hw/remote-port-device.h"
#include "hw/remote-port-ats.h"
#include "hw/remote-port-memory-master.h"
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
 * 21           ATS
 */
#define RPDEV_PCI_CONFIG        0
#define RPDEV_PCI_LEGACY_IRQ    1
#define RPDEV_PCI_MESSAGES      2
#define RPDEV_PCI_DMA           3
#define RPDEV_PCI_BAR_BASE     10
#define RPDEV_PCI_ATS          21

typedef struct RemotePortPCIDevice RemotePortPCIDevice;

struct RemotePortPCIDevice {
    /*< private >*/
    PCIDevice parent_obj;

    RemotePortMemorySlave *rp_dma;
    RemotePortATS *rp_ats;

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

        /* Controls if the remote dev is responsible for the config space.  */
        bool remote_config;

        bool msi;
        bool msix;
        bool ats;
    } cfg;
    struct RemotePort *rp;
    struct rp_peer_state *peer;
};

static MemTxResult rp_io_access(MemoryTransaction *tr)
{
    RemotePortMap *map = tr->opaque;
    RemotePortPCIDevice *s = map->parent;

    return rp_mm_access(s->rp, map->rp_dev, s->peer, tr, true, 0);
}

static const MemoryRegionOps rp_ops = {
    .access = rp_io_access,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint32_t rp_pci_read_config(PCIDevice *pci_dev, uint32_t addr, int size)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(pci_dev);
    MemoryTransaction tr = {
        .addr = addr,
        .rw = false,
        .size = size,
        .attr = MEMTXATTRS_UNSPECIFIED
    };

    rp_mm_access(s->rp, s->cfg.rp_dev, s->peer, &tr, true, 0);
    DB_PRINT_L(0, "addr: %x data: %x\n", addr, (uint32_t) tr.data.u64);
    return tr.data.u64;
}

static void rp_pci_write_config(PCIDevice *pci_dev, uint32_t addr,
                                uint32_t value, int size)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(pci_dev);
    MemoryTransaction tr = {
        .addr = addr,
        .rw = true,
        .size = size,
        .data.u64 = value,
        .attr = MEMTXATTRS_UNSPECIFIED
    };

    DB_PRINT_L(0, "addr: %x data: %x\n", addr, value);
    rp_mm_access(s->rp, s->cfg.rp_dev, s->peer, &tr, true, 0);
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

    /*
     * If MSI/MSI-X is enabled, map interrupt wires onto MSI.
     * This will only work when QEMU owns the CONFIG space.
     */
    if (s->cfg.msix && msix_enabled(d)) {
        if (level) {
            msix_notify(d, 0);
        }
    } else if (s->cfg.msi && msi_enabled(d)) {
        if (level) {
            msi_notify(d, 0);
        }
    } else {
        pci_set_irq(d, level);
    }
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

    if (s->cfg.remote_config) {
        pci_dev->config_read = rp_pci_read_config;
    }
    /* The remote peer may want to snoop on CFG writes.  */
    pci_dev->config_write = rp_pci_write_config;

    pcie_endpoint_cap_init(pci_dev, 0);

    if (s->cfg.msi) {
        msi_init(pci_dev, 0x60, 1, true, false, &error_fatal);
    }
    if (s->cfg.ats) {
        pcie_ats_init(pci_dev, 256);
    }

    /* Create and hook up the BARs.  */
    s->maps = g_new0(typeof(*s->maps), s->cfg.nr_io_bars + s->cfg.nr_mm_bars);

    for (i = 0; i < s->cfg.nr_io_bars + s->cfg.nr_mm_bars; i++) {
        bool io_bar = i < s->cfg.nr_io_bars;
        char *name = g_strdup_printf("rp-pci-%s-%d", io_bar ? "io" : "mmio", i);
        uint8_t attr = io_bar ?
               PCI_BASE_ADDRESS_SPACE_IO : PCI_BASE_ADDRESS_SPACE_MEMORY;

        memory_region_init_io(&s->maps[i].iomem, OBJECT(s), &rp_ops,
                              &s->maps[i], name, s->cfg.bar_size[i]);
        pci_register_bar(pci_dev, i, attr, &s->maps[i].iomem);
        s->maps[i].rp_dev = RPDEV_PCI_BAR_BASE + i;
        s->maps[i].parent = s;
        g_free(name);
    }

    if (s->cfg.msix) {
        msix_init_exclusive_bar(pci_dev, 1,
                                s->cfg.nr_io_bars + s->cfg.nr_mm_bars,
                                NULL);
        msix_vector_use(pci_dev, 0);
    }

    as = pci_get_address_space(pci_dev);

    /* Setup ATS */
    rp_device_attach(OBJECT(s->rp), OBJECT(s->rp_ats), 0,
                            s->cfg.rp_dev + RPDEV_PCI_ATS, &error_abort);
    object_property_set_link(OBJECT(s->rp_ats), "mr", OBJECT(as->root),
                             &error_abort);
    object_property_set_bool(OBJECT(s->rp_ats), "realized", true, &error_abort);

    /* Setup the DMA dev.  */
    rp_device_attach(OBJECT(s->rp), OBJECT(s->rp_dma), 0,
                            s->cfg.rp_dev + RPDEV_PCI_DMA, &error_abort);
    object_property_set_link(OBJECT(s->rp_dma), "rp-ats-cache",
                             OBJECT(s->rp_ats), &error_abort);
    object_property_set_link(OBJECT(s->rp_dma), "mr", OBJECT(as->root), &error_abort);

    object_property_set_bool(OBJECT(s->rp_dma), "realized", true, &error_abort);
}

static void rp_pci_exit(PCIDevice *pci_dev)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(pci_dev);

    /* Setup the DMA dev.  */
    rp_device_detach(OBJECT(s->rp), OBJECT(s->rp_dma), 0,
                            s->cfg.rp_dev + RPDEV_PCI_DMA, &error_abort);
    rp_device_detach(OBJECT(s->rp), OBJECT(s), 0,
                            s->cfg.rp_dev, &error_abort);

    /*
     * Cannot be a child of us since as->root->owner gets reffed by
     * address_space_init in rp_dma creating a circular dep.
     */
    object_unparent(OBJECT(s->rp_dma));
}

static void rp_pci_init(Object *obj)
{
    RemotePortPCIDevice *s = REMOTE_PORT_PCI_DEVICE(obj);
    Object *tmp_obj;

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&s->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);


    tmp_obj = object_new(TYPE_REMOTE_PORT_MEMORY_SLAVE);
    s->rp_dma = REMOTE_PORT_MEMORY_SLAVE(tmp_obj);

    object_property_add_child(obj, "rp-dma", tmp_obj);
    /* add_child will grant us another ref, free the initial one.  */
    object_unref(tmp_obj);

    tmp_obj = object_new(TYPE_REMOTE_PORT_ATS);
    s->rp_ats = REMOTE_PORT_ATS(tmp_obj);
    object_property_add_child(obj, "rp-ats", tmp_obj);
    object_unref(tmp_obj);
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

    DEFINE_PROP_BOOL("remote-config", RemotePortPCIDevice,
                     cfg.remote_config, false),
    DEFINE_PROP_BOOL("msi", RemotePortPCIDevice, cfg.msi, false),
    DEFINE_PROP_BOOL("msix", RemotePortPCIDevice, cfg.msix, false),
    DEFINE_PROP_BOOL("ats", RemotePortPCIDevice, cfg.ats, false),

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
    k->exit = rp_pci_exit;
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
        { INTERFACE_PCIE_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
