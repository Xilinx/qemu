/*
 * QEMU Remote Port PCI Express root port
 *
 * Copyright (c) 2022 AMD Inc
 * Written by Francisco Iglesias <francisco.iglesias@amd.com>
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
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/pci/pcie_host.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "trace.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"
#include "hw/remote-port-ats.h"
#include "hw/remote-port-memory-master.h"
#include "hw/remote-port-memory-slave.h"

#define TYPE_REMOTE_PORT_PCIE_ROOT_PORT "remote-port-pcie-root-port"

OBJECT_DECLARE_SIMPLE_TYPE(RemotePortPCIERootPort, REMOTE_PORT_PCIE_ROOT_PORT)

#define RP_ROOT_PORT_SSVID_OFFSET 0x40
#define RP_ROOT_PORT_MSI_OFFSET   0x60
#define RP_ROOT_PORT_EXP_OFFSET   0x90
#define RP_ROOT_PORT_AER_OFFSET   0x100

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

#define PCI_DEVICE_ID_REMOTE_PORT_RP 0x777a

#define R_CFG_BUS(x) ((x >> 16) & 0xFF)
#define R_CFG_DEVFN(x) ((x >> 8) & 0xFF)
#define R_CFG_REG_OFF(x) (x & 0xFC)

#define ECAM_BUS(x) ((x >> 20) & 0xFF)
#define ECAM_DEV(x) ((x >> 15) & 0x1F)
#define ECAM_MASK 0xFFFFFFF

struct RemotePortPCIERootPort {
    /*< private >*/
    PCIESlot parent_obj;

    RemotePortMemorySlave *rp_dma;
    RemotePortATS *rp_ats;

    /*< public >*/
    RemotePortMap *maps;

    struct {
        uint32_t rp_dev;
        uint32_t nr_devs;
    } cfg;

    MemoryRegion address_space_io;
    MemoryRegion address_space_mem;

    /* IO config */
    MemoryRegion conf_mem;
    MemoryRegion data_mem;
    PCIHostState *hs;

    /* MMCFG (ECAM) */
    MemoryRegion mmcfg;

    struct RemotePort *rp;
    struct rp_peer_state *peer;

    Notifier machine_done;
};

static bool pci_secondary_bus_in_range(PCIDevice *dev, int bus_num)
{
    /* Don't walk the bus if it's reset. */
    bool rst = !!(pci_get_word(dev->config + PCI_BRIDGE_CONTROL) &
                  PCI_BRIDGE_CTL_BUS_RESET);

    return bus_num != 0 && rst == false &&
           dev->config[PCI_SECONDARY_BUS] <= bus_num &&
           bus_num <= dev->config[PCI_SUBORDINATE_BUS];
}

static uint64_t rp_root_port_memory_read(void *opaque, hwaddr addr,
                                         unsigned size)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    MemoryTransaction tr = {
        .rw = false, /* read */
        .addr = addr,
        .size = size,
    };

    rp_mm_access(p->rp, RPDEV_PCI_BAR_BASE, p->peer, &tr, true, 0);

    return tr.data.u64;
}

static void rp_root_port_memory_write(void *opaque, hwaddr addr, uint64_t data,
                                      unsigned size)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    MemoryTransaction tr = {
        .rw = true, /* write */
        .addr = addr,
        .data.u64 = data,
        .size = size,
    };

    rp_mm_access(p->rp, RPDEV_PCI_BAR_BASE, p->peer, &tr, true, 0);
}

static uint64_t rp_root_port_io_read(void *opaque, hwaddr addr, unsigned size)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    MemoryTransaction tr = {
        .rw = false, /* read */
        .addr = addr,
        .size = (size < 4) ? size : 4,
    };

    rp_mm_access_with_def_attr(p->rp, RPDEV_PCI_BAR_BASE, p->peer, &tr, true, 0,
                               RP_BUS_ATTR_IO_ACCESS);

    return tr.data.u32;
}

static void rp_root_port_io_write(void *opaque, hwaddr addr, uint64_t data,
                                  unsigned size)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    MemoryTransaction tr = {
        .rw = true, /* write */
        .addr = addr,
        .data.u32 = data,
        .size = (size < 4) ? size : 4,
    };

    rp_mm_access_with_def_attr(p->rp, RPDEV_PCI_BAR_BASE, p->peer, &tr, true, 0,
                               RP_BUS_ATTR_IO_ACCESS);
}

static const MemoryRegionOps rp_root_port_mem_ops = {
    .read = rp_root_port_memory_read,
    .write = rp_root_port_memory_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    }
};

static const MemoryRegionOps rp_root_port_io_ops = {
    .read = rp_root_port_io_read,
    .write = rp_root_port_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    }
};

static uint8_t rp_root_port_aer_vector(const PCIDevice *dev)
{
    return 0;
}

static int rp_root_port_intr_init(PCIDevice *dev, Error **errp)
{
    bool msi_per_vector_mask = false;
    bool msi64bit = true;
    int rc;

    rc = msi_init(dev, RP_ROOT_PORT_MSI_OFFSET, 1, msi64bit,
                  msi_per_vector_mask, errp);
    if (rc < 0) {
        assert(rc == -ENOTSUP);
    }

    return rc;
}

static void rp_root_port_intr_uninit(PCIDevice *dev)
{
    msi_uninit(dev);
}

static void rp_pci_host_config_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned len)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    PCIHostState *s = p->hs;

    if (addr != 0 || len != 4) {
        return;
    }
    s->config_reg = val;
}

static uint64_t rp_pci_host_config_read(void *opaque, hwaddr addr,
                                     unsigned len)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    PCIHostState *s = p->hs;
    uint32_t val = s->config_reg;

    return val;
}

const MemoryRegionOps rp_pci_host_conf_ops = {
    .read = rp_pci_host_config_read,
    .write = rp_pci_host_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_pci_host_data_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned len)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    PCIHostState *s = p->hs;
    uint32_t devfn = R_CFG_DEVFN(s->config_reg);
    uint32_t bus = R_CFG_BUS(s->config_reg);

    /* Forward dev 0 on the RP root port's secondary bus */
    if (s->config_reg & (1u << 31) &&
        pci_secondary_bus_in_range(PCI_DEVICE(p), bus) &&
        PCI_SLOT(devfn) == 0) {
        uint32_t reg_off = R_CFG_REG_OFF(s->config_reg) | (addr & 3);
        MemoryTransaction tr = {
            .rw = true, /* write */
            .addr = (bus << 20) | devfn << 12 | reg_off,
            .data.u32 = val,
            .size = len,
        };

        rp_mm_access(p->rp, RPDEV_PCI_CONFIG, p->peer, &tr, true, 0);

        return;
    }

    if (s->config_reg & (1u << 31)) {
        pci_data_write(s->bus, s->config_reg | (addr & 3), val, len);
    }
}

static uint64_t rp_pci_host_data_read(void *opaque,
                                   hwaddr addr, unsigned len)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    PCIHostState *s = p->hs;
    uint32_t devfn = R_CFG_DEVFN(s->config_reg);
    uint32_t bus = R_CFG_BUS(s->config_reg);

    if (!(s->config_reg & (1U << 31))) {
        return 0xffffffff;
    }

    /* Forward dev 0 on the RP root port's secondary bus */
    if (pci_secondary_bus_in_range(PCI_DEVICE(p), bus) &&
        PCI_SLOT(devfn) == 0) {
        uint32_t reg_off = R_CFG_REG_OFF(s->config_reg) | (addr & 3);
        MemoryTransaction tr = {
                .rw = false, /* read */
                .addr = (bus << 20) | devfn << 12 | reg_off,
                .size = len,
            };

        rp_mm_access(p->rp, RPDEV_PCI_CONFIG, p->peer, &tr, true, 0);

        return tr.data.u32;
    }

    return pci_data_read(s->bus, s->config_reg | (addr & 3), len);
}

const MemoryRegionOps rp_pci_host_data_ops = {
    .read = rp_pci_host_data_read,
    .write = rp_pci_host_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static inline PCIDevice *pcie_dev_find_by_mmcfg_addr(PCIBus *s,
                                                     uint32_t mmcfg_addr)
{
    return pci_find_device(s, PCIE_MMCFG_BUS(mmcfg_addr),
                           PCIE_MMCFG_DEVFN(mmcfg_addr));
}

static void rp_pcie_mmcfg_data_write(void *opaque, hwaddr mmcfg_addr,
                                  uint64_t val, unsigned len)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    PCIExpressHost *e = PCIE_HOST_BRIDGE(p->hs);
    PCIBus *s = e->pci.bus;
    PCIDevice *pci_dev;
    uint32_t addr;
    uint32_t limit;

    if (pci_secondary_bus_in_range(PCI_DEVICE(p), ECAM_BUS(mmcfg_addr)) &&
        ECAM_DEV(mmcfg_addr) == 0) {
        MemoryTransaction tr = {
            .rw = true, /* write */
            .addr = mmcfg_addr & ECAM_MASK,
            .data.u32 = val,
            .size = len,
        };

        rp_mm_access(p->rp, RPDEV_PCI_CONFIG, p->peer, &tr, true, 0);

        return;
    }

    pci_dev = pcie_dev_find_by_mmcfg_addr(s, mmcfg_addr);
    if (!pci_dev) {
        return;
    }

    addr = PCIE_MMCFG_CONFOFFSET(mmcfg_addr);
    limit = pci_config_size(pci_dev);
    pci_host_config_write_common(pci_dev, addr, limit, val, len);
}

static uint64_t rp_pcie_mmcfg_data_read(void *opaque,
                                     hwaddr mmcfg_addr,
                                     unsigned len)
{
    RemotePortPCIERootPort *p = REMOTE_PORT_PCIE_ROOT_PORT(opaque);
    PCIExpressHost *e = PCIE_HOST_BRIDGE(p->hs);
    PCIBus *s = e->pci.bus;
    PCIDevice *pci_dev;
    uint32_t addr;
    uint32_t limit;

    if (pci_secondary_bus_in_range(PCI_DEVICE(p), ECAM_BUS(mmcfg_addr)) &&
        ECAM_DEV(mmcfg_addr) == 0) {
        MemoryTransaction tr = {
            .rw = false, /* read */
            .addr = mmcfg_addr & ECAM_MASK,
            .size = len,
        };

        rp_mm_access(p->rp, RPDEV_PCI_CONFIG, p->peer, &tr, true, 0);

        return tr.data.u32;
    }

    pci_dev = pcie_dev_find_by_mmcfg_addr(s, mmcfg_addr);
    if (!pci_dev) {
        return ~0x0;
    }

    addr = PCIE_MMCFG_CONFOFFSET(mmcfg_addr);
    limit = pci_config_size(pci_dev);
    return pci_host_config_read_common(pci_dev, addr, limit, len);
}

static const MemoryRegionOps rp_pcie_mmcfg_ops = {
    .read = rp_pcie_mmcfg_data_read,
    .write = rp_pcie_mmcfg_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static AddressSpace *pci_bus_iommu_address_space(PCIBus *root_bus,
                                                 PCIBus *bus, uint16_t devfn)
{
    PCIBus *iommu_bus = root_bus;

    if (!pci_bus_bypass_iommu(bus) && iommu_bus && iommu_bus->iommu_fn) {
        return iommu_bus->iommu_fn(bus, iommu_bus->iommu_opaque, devfn);
    }
    return &address_space_memory;
}

/*
 * This needs to be executed after a potential IOMMU for the PCI has hierarchy
 * been setup (realized).
 */
static void rp_root_port_machine_done(Notifier *notifier, void *data)
{
    RemotePortPCIERootPort *s = container_of(notifier,
                                             RemotePortPCIERootPort,
                                             machine_done);
    PCIBus *sec = pci_bridge_get_sec_bus(PCI_BRIDGE(s));
    PCIBus *root_bus = PCI_BUS(DEVICE(s)->parent_bus);
    AddressSpace *as;
    Object *tmp_obj;

    /* Only devfn 0 is currently supported */
    as = pci_bus_iommu_address_space(root_bus, sec, 0);

    tmp_obj = object_new(TYPE_REMOTE_PORT_MEMORY_SLAVE);
    s->rp_dma = REMOTE_PORT_MEMORY_SLAVE(tmp_obj);

    object_property_add_child(OBJECT(s), "rp-dma", tmp_obj);
    /* add_child will grant us another ref, free the initial one.  */
    object_unref(tmp_obj);

    tmp_obj = object_new(TYPE_REMOTE_PORT_ATS);
    s->rp_ats = REMOTE_PORT_ATS(tmp_obj);
    object_property_add_child(OBJECT(s), "rp-ats", tmp_obj);
    object_unref(tmp_obj);

    /* Setup ATS */
    rp_device_attach(OBJECT(s->rp), OBJECT(s->rp_ats), 0,
                            s->cfg.rp_dev + RPDEV_PCI_ATS, &error_abort);
    object_property_set_link(OBJECT(s->rp_ats), "mr", OBJECT(as->root),
                             &error_abort);
    object_property_set_bool(OBJECT(s->rp_ats), "realized", true, &error_abort);

    /* Setup the DMA dev.  */
    rp_device_attach(OBJECT(s->rp), OBJECT(s->rp_dma), 0,
                            s->cfg.rp_dev + RPDEV_PCI_DMA, &error_abort);
    object_property_set_link(OBJECT(s->rp_dma), "mr", OBJECT(as->root),
                             &error_abort);
    object_property_set_link(OBJECT(s->rp_dma), "rp-ats-cache",
                             OBJECT(s->rp_ats), &error_abort);
    object_property_set_bool(OBJECT(s->rp_dma), "realized", true, &error_abort);
}

static void rp_root_port_realize(DeviceState *d, Error **errp)
{
    PCIERootPortClass *rpcls = PCIE_ROOT_PORT_GET_CLASS(d);
    RemotePortPCIERootPort *port = REMOTE_PORT_PCIE_ROOT_PORT(d);
    PCIBridge *br = PCI_BRIDGE(d);
    PCIBus *root_bus = PCI_BUS(d->parent_bus);
    PCIHostState *hs = PCI_HOST_BRIDGE(BUS(root_bus)->parent);
    PCIExpressHost *peh = PCIE_HOST_BRIDGE(hs);
    Error *local_err = NULL;

    if (!pci_bus_is_root(root_bus) ||
        !object_dynamic_cast(OBJECT(root_bus), TYPE_PCIE_BUS)) {
        error_setg(errp,
                   "The remote-port-pcie-root-port must be connected to a PCIe"
                   " rootbus");
        return;
    }

    /* IO configuration setup */
    memory_region_add_subregion_overlap(root_bus->address_space_io,
                                        0xcf8, &port->conf_mem, 1);
    memory_region_add_subregion_overlap(root_bus->address_space_io,
                                        0xcfc, &port->data_mem, 1);

    /* Store the PCIHostState for later use */
    port->hs = hs;

    /* MMCFG (ECAM) configuration */
    memory_region_add_subregion_overlap(&peh->mmio, 0, &port->mmcfg, 1);

    rpcls->parent_realize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_add_subregion(&br->address_space_io, 0,
                                &port->address_space_io);

    memory_region_add_subregion(&br->address_space_mem, 0,
                                &port->address_space_mem);

    device_legacy_reset(DEVICE(port->rp));
    port->peer = rp_get_peer(port->rp);

    port->machine_done.notify = rp_root_port_machine_done;
    qemu_add_machine_init_done_notifier(&port->machine_done);
}

static void rp_root_port_init(Object *obj)
{
    RemotePortPCIERootPort *s = REMOTE_PORT_PCIE_ROOT_PORT(obj);

    /* Remote port setup */
    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&s->rp,
                             qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);

    /* IO configuration setup */
    memory_region_init_io(&s->conf_mem, obj, &rp_pci_host_conf_ops, s,
                          "pci-conf-idx", 4);
    memory_region_init_io(&s->data_mem, obj, &rp_pci_host_data_ops, s,
                          "pci-conf-data", 4);

    /* MMCFG (ECAM) configuration */
    memory_region_init_io(&s->mmcfg, OBJECT(s), &rp_pcie_mmcfg_ops, s,
                          "pcie-mmcfg-mmio", PCIE_MMCFG_SIZE_MAX);

    /* I/O requests routed to this port (bridge) */
    memory_region_init_io(&s->address_space_io, OBJECT(s),
                          &rp_root_port_io_ops, s,
                          TYPE_REMOTE_PORT_PCIE_ROOT_PORT "-io",
                          UINT32_MAX);

    /* Memory requests routed to this port (bridge) */
    memory_region_init_io(&s->address_space_mem, OBJECT(s),
                          &rp_root_port_mem_ops, s,
                          TYPE_REMOTE_PORT_PCIE_ROOT_PORT "-mem",
                          UINT64_MAX);
}

static Property rp_root_port_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortPCIERootPort, cfg.rp_dev, 0),
    DEFINE_PROP_UINT32("nr-devs", RemotePortPCIERootPort, cfg.nr_devs, 21),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp_root_port_class_init(ObjectClass *cls, void *data)
{
    DeviceClass *devcls = DEVICE_CLASS(cls);
    PCIDeviceClass *pcicls = PCI_DEVICE_CLASS(cls);
    PCIERootPortClass *pciecls = PCIE_ROOT_PORT_CLASS(cls);

    device_class_set_props(devcls, rp_root_port_properties);

    device_class_set_parent_realize(devcls, rp_root_port_realize,
                                    &pciecls->parent_realize);

    devcls->desc = "Remote-Port PCIe root port";
    pcicls->vendor_id = PCI_VENDOR_ID_XILINX;
    pcicls->device_id = PCI_DEVICE_ID_REMOTE_PORT_RP;

    pciecls->exp_offset = RP_ROOT_PORT_EXP_OFFSET;
    pciecls->aer_offset = RP_ROOT_PORT_AER_OFFSET;
    pciecls->ssvid_offset = RP_ROOT_PORT_SSVID_OFFSET;
    pciecls->ssid = 0;

    pciecls->aer_vector = rp_root_port_aer_vector;
    pciecls->interrupts_init = rp_root_port_intr_init;
    pciecls->interrupts_uninit = rp_root_port_intr_uninit;
}

static const TypeInfo rp_root_port_info = {
    .name       = TYPE_REMOTE_PORT_PCIE_ROOT_PORT,
    .parent     = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(RemotePortPCIERootPort),
    .instance_init = rp_root_port_init,
    .class_init = rp_root_port_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_root_port_register(void)
{
    type_register_static(&rp_root_port_info);
}

type_init(rp_root_port_register);
