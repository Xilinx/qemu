/*
 * QEMU Remote-port PCI adaptor.
 *
 * Copyright (c) 2020 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"

#include "hw/remote-port.h"

#define TYPE_REMOTE_PORT_PCI_ADAPTOR "remote-port-pci-adaptor"
#define REMOTE_PORT_PCI_ADAPTOR(obj) \
        OBJECT_CHECK(RemotePortPCIAdaptor, (obj), \
                     TYPE_REMOTE_PORT_PCI_ADAPTOR)

typedef struct RemotePortPCIAdaptor {
    /*< private >*/
    PCIDevice parent_obj;

    struct {
        uint32_t vendor_id;
        uint32_t device_id;
        uint32_t revision;
        uint32_t class_id;
        uint8_t prog_if;
        char *chardev_id;
    } cfg;

    struct RemotePort *rp;
} RemotePortPCIAdaptor;

static void rp_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    RemotePortPCIAdaptor *s = REMOTE_PORT_PCI_ADAPTOR(pci_dev);
    Chardev *chr = NULL;

    /* Update device IDs after our properties have been set.  */
    pci_config_set_vendor_id(pci_dev->config, s->cfg.vendor_id);
    pci_config_set_device_id(pci_dev->config, s->cfg.device_id);
    pci_config_set_revision(pci_dev->config, s->cfg.revision);
    pci_config_set_class(pci_dev->config, s->cfg.class_id);
    pci_dev->config[PCI_CLASS_PROG] = s->cfg.prog_if;
    pci_dev->config[PCI_INTERRUPT_PIN] = 1;

    if (s->cfg.chardev_id) {
        chr = qemu_chr_find(s->cfg.chardev_id);
        if (chr) {
            qdev_prop_set_chr(DEVICE(s->rp), "chardev", chr);
        }
    }

    object_property_set_bool(OBJECT(s->rp), "realized", true, &error_abort);
    info_report("%s ready", object_get_canonical_path(OBJECT(s->rp)));
}

static void rp_pci_init(Object *obj)
{
    RemotePortPCIAdaptor *s = REMOTE_PORT_PCI_ADAPTOR(obj);

    /* Can't embedd since the adaptor may outlive the PCI wrapper.  */
    s->rp = REMOTE_PORT(object_new(TYPE_REMOTE_PORT));
    object_property_add_child(OBJECT(s), "rp", OBJECT(s->rp));
    /* Drop once since we now own it twice. */
    object_unref(OBJECT(s->rp));
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("vendor-id", RemotePortPCIAdaptor, cfg.vendor_id,
                       PCI_VENDOR_ID_XILINX),
    DEFINE_PROP_UINT32("device-id", RemotePortPCIAdaptor, cfg.device_id, 0),
    DEFINE_PROP_UINT32("revision", RemotePortPCIAdaptor, cfg.revision, 0),
    DEFINE_PROP_UINT32("class-id", RemotePortPCIAdaptor, cfg.class_id,
                       PCI_CLASS_NETWORK_ETHERNET),
    DEFINE_PROP_UINT8("prog-if", RemotePortPCIAdaptor, cfg.prog_if, 1),
    DEFINE_PROP_STRING("chardev", RemotePortPCIAdaptor, cfg.chardev_id),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_pci_class_init(ObjectClass *oc, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "Remote-Port PCI Adaptor";
    device_class_set_props(dc, rp_properties);

    k->realize = rp_pci_realize;
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = 0;
    k->revision = 0;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_PCI_ADAPTOR,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RemotePortPCIAdaptor),
    .instance_init = rp_pci_init,
    .class_init    = rp_pci_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
