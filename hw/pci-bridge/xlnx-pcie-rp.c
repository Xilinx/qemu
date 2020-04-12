/*
 * xlnx-pcie-rp.c
 * Model of the Xilinx PCIe Root-Port
 *
 * Copyright (c) 2016 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "qapi/error.h"

#define PCI_DEVICE_ID_EPORT         0xd022
#define PCI_DEVICE_ID_REV           0x1
#define EP_EXP_OFFSET               0x60
#define EP_AER_OFFSET               0x100

static void xlnx_write_config(PCIDevice *d,
                              uint32_t address, uint32_t val, int len)
{
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);
    uint16_t slt_ctl, slt_sta;

    pcie_cap_slot_get(d, &slt_ctl, &slt_sta);

    pci_bridge_write_config(d, address, val, len);
    pcie_cap_slot_write_config(d, slt_ctl, slt_sta, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);
}

static void xlnx_reset(DeviceState *qdev)
{
    PCIDevice *d = PCI_DEVICE(qdev);

    pcie_cap_root_reset(d);
    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_cap_arifwd_reset(d);
    pcie_aer_root_reset(d);
    pci_bridge_reset(qdev);
    pci_bridge_disable_base_limit(d);
}

static void xlnx_realize(PCIDevice *d, Error **errp)
{
    PCIEPort *p = PCIE_PORT(d);
    PCIESlot *s = PCIE_SLOT(d);
    int rc;

    pci_bridge_initfn(d, TYPE_PCIE_BUS);
    pcie_port_init_reg(d);

    rc = pcie_cap_init(d, EP_EXP_OFFSET, PCI_EXP_TYPE_ROOT_PORT, p->port,
                       errp);
    if (rc < 0) {
        goto err_bridge;
    }

    pcie_cap_arifwd_init(d);
    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s);
    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        goto err_pcie_cap;
    }
    pcie_cap_root_init(d);
    rc = pcie_aer_init(d, PCI_ERR_VER, EP_AER_OFFSET, PCI_ERR_SIZEOF, errp);
    if (rc < 0) {
        goto err;
    }

    return;
err:
    pcie_chassis_del_slot(s);
err_pcie_cap:
    pcie_cap_exit(d);
err_bridge:
    pci_bridge_exitfn(d);
}

static void xlnx_exitfn(PCIDevice *d)
{
    PCIESlot *s = PCIE_SLOT(d);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    pci_bridge_exitfn(d);
}

static Property xlnx_props[] = {
    DEFINE_PROP_BIT(COMPAT_PROP_PCP, PCIDevice, cap_present,
                    QEMU_PCIE_SLTCAP_PCP_BITNR, true),
    DEFINE_PROP_END_OF_LIST()
};

static const VMStateDescription vmstate_xlnx = {
    .name = "xlnx-pcie-rp",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj.parent_obj.parent_obj, PCIESlot),
        VMSTATE_STRUCT(parent_obj.parent_obj.parent_obj.exp.aer_log,
                       PCIESlot, 0, vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static void xlnx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->is_bridge = 1;
    k->config_write = xlnx_write_config;
    k->realize = xlnx_realize;
    k->exit = xlnx_exitfn;
    k->vendor_id = PCI_VENDOR_ID_XILINX;
    k->device_id = PCI_DEVICE_ID_EPORT;
    k->revision = PCI_DEVICE_ID_REV;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Xilinx PCIe Root Port";
    dc->reset = xlnx_reset;
    dc->vmsd = &vmstate_xlnx;
    device_class_set_props(dc, xlnx_props);
}

static const TypeInfo xlnx_info = {
    .name          = "xlnx-pcie-rp",
    .parent        = TYPE_PCIE_SLOT,
    .class_init    = xlnx_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { },
    },

};

static void xlnx_register_types(void)
{
    type_register_static(&xlnx_info);
}

type_init(xlnx_register_types)
