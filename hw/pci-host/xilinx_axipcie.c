/*
 * QEMU model of the Xilinx AXI_PCIE Controller
 *
 * Copyright (C) 2012 Peter A. G. Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (C) 2012 Xilinx Inc.
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
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "hw/pci/pci.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

#include "hw/fdt_generic_util.h"

#define XILINX_AXI_PCIE_DEBUG 1

#ifndef XILINX_AXI_PCIE_DEBUG
#define XILINX_AXI_PCIE_DEBUG 0
#endif
#define DB_PRINT(fmt, args...) do {\
    if (XILINX_AXI_PCIE_DEBUG) {\
        fprintf(stderr, "XILINX_AXI_PCIE: %s:" fmt, __func__, ## args);\
    } \
} while (0);

#define R_ADDR_TO_IDX(x) (((x) - 0x130) / 4)

#define R_BR_INFO       R_ADDR_TO_IDX(0x130)
#define R_BR_SCR        R_ADDR_TO_IDX(0x134)
#define R_IDR           R_ADDR_TO_IDX(0x138)
#define R_IMR           R_ADDR_TO_IDX(0x13c)
#define R_BUS_LOC       R_ADDR_TO_IDX(0x140)
#define R_PHY_SCR       R_ADDR_TO_IDX(0x144)
#define R_RP_MSI_1      R_ADDR_TO_IDX(0x14c)
#define R_RP_MSI_2      R_ADDR_TO_IDX(0x150)
#define R_RP_ERR_FIFO   R_ADDR_TO_IDX(0x154)
#define R_RP_INT_FIFO1  R_ADDR_TO_IDX(0x158)
#define R_RP_INT_FIFO2  R_ADDR_TO_IDX(0x15c)

#define R_MAX ((0x160 - 0x130)/4)

/* FIXME: this struct defintion is generic, may belong in bitops or somewhere
 * like that
 */

typedef struct XilinxAXIPCIERegInfo {
    const char *name;
    uint32_t ro;
    uint32_t wtc;
    uint32_t reset;
    int width;
}  XilinxAXIPCIERegInfo;

static const XilinxAXIPCIERegInfo xilinx_axi_pcie_reg_info[] = {
    [R_BR_INFO]        = {.name = "BRIDGE INFO", .width = 19, .reset = 0x70007,
                          .ro = ~0 },
    [R_BR_SCR]         = {.name = "BRIDGE STATUS CONTROL", .width = 18,
                          .ro = 0x0FEFF },
    [R_IDR]            = {.name = "INTERRUPT DECODE", .width = 29,
                          .wtc = 0x1FF30FEF, .ro = 0xCF010 },
    [R_IMR]            = {.name = "INTERRUPT MASK", .width = 29,
                          .ro = 0xCF010 },
    [R_BUS_LOC]        = {.name = "BUS LOCATION", .width = 24 },
    [R_PHY_SCR]        = {.name = "PHY STATUS CONTROL", .width = 22,
                          .ro = 0xFFFF, .reset = 0x800 },
    [R_RP_MSI_1]       = {.name = "ROOT PORT MSI BASE 1", .width = 32 },
    [R_RP_MSI_2]       = {.name = "ROOT PORT MSI BASE 2", .width = 32 },
    [R_MAX]            = {.name = NULL }
};

#define MAX_AXI_TO_PCI_BARS 6
#define MAX_PCI_TO_AXI_BARS 3

typedef struct XilinxACIPCIEMapping {
    hwaddr src;
    hwaddr dst;
    hwaddr size;
    uint8_t size2;
} XilinxACIPCIEMapping;

typedef struct XilinxAXIPCIE {
    SysBusDevice busdev;
    PCIBus *pci_bus;

    MemoryRegion container;
    MemoryRegion config;
    MemoryRegion mmio;
    MemoryRegion pci_space;

    MemoryRegion axi_to_pci_bar[MAX_AXI_TO_PCI_BARS];
    MemoryRegion pci_to_axi_bar[MAX_PCI_TO_AXI_BARS];

    PCIBus *bus;

    qemu_irq irq;
    int irqline;

    uint32_t regs[R_MAX];
} XilinxAXIPCIE;

static inline void xilinx_axi_pcie_update_irq(XilinxAXIPCIE *s)
{
    int new_irqline = !!(s->regs[R_IDR] & s->regs[R_IMR]);
    
    if (new_irqline != s->irqline) {
        DB_PRINT("irq state: %d\n", new_irqline);
        qemu_set_irq(s->irq, new_irqline);
        s->irqline = new_irqline;
    }
}

static void xilinx_axi_pcie_do_reset(XilinxAXIPCIE *s)
{
    int i;
    memset(s->regs, 0, sizeof(s->regs));

    for (i = 0; i < ARRAY_SIZE(s->regs); ++i) {
        if (xilinx_axi_pcie_reg_info[i].name) {
            s->regs[i] = xilinx_axi_pcie_reg_info[i].reset;
        }
    }

    xilinx_axi_pcie_update_irq(s);
}

static void xilinx_axi_pcie_reset(DeviceState *d)
{
    /* FIXME: implement QOM cast macro */
    xilinx_axi_pcie_do_reset((XilinxAXIPCIE *)d);
}

static inline void xilinx_axi_pcie_check_reg_access(hwaddr offset, uint32_t val,
                                                bool rnw)
{
    if (!xilinx_axi_pcie_reg_info[offset >> 2].name) {
        qemu_log_mask(LOG_UNIMP, "Xilinx AXI PCIE: %s offset %x\n",
                      rnw ? "read from" : "write to", (unsigned)offset);
        DB_PRINT("Unimplemented %s offset %x\n",
                 rnw ? "read from" : "write to", (unsigned)offset);
    } else {
        DB_PRINT("%s %s [%#02x] %s %#08x\n", rnw ? "read" : "write",
                 xilinx_axi_pcie_reg_info[offset >> 2].name, (unsigned) offset,
                 rnw ? "->" : "<-", val);
    }
}

static uint64_t xilinx_axi_pcie_config_read(void *opaque, hwaddr offset,
                                            unsigned int size)
{
    XilinxAXIPCIE *s = opaque;
    PCIDevice *pci_dev = pci_find_device(s->pci_bus, 0, 0);
    uint64_t ret = pci_dev ? pci_dev->config_read(pci_dev, offset, size) : 0;

    DB_PRINT("PCI config read device :%s offset: %x data: %x size: %d\n",
             pci_dev ? object_get_canonical_path(OBJECT(pci_dev)) : "(none)",
             (unsigned)offset, (unsigned)ret, size);

    return ret;
}

static void xilinx_axi_pcie_config_write(void *opaque, hwaddr offset,
                                         uint64_t value, unsigned int size)
{
    XilinxAXIPCIE *s = opaque;
    PCIDevice *pci_dev = pci_find_device(s->pci_bus, 0, 0);

    DB_PRINT("PCI config write device :%s offset: %x data: %x size: %d\n",
             pci_dev ? object_get_canonical_path(OBJECT(pci_dev)) : "(none)",
             (unsigned)offset, (unsigned)value, size);

    if (pci_dev) {
        pci_dev->config_write(pci_dev, offset, value, size);
    }
}

static const MemoryRegionOps xilinx_axi_pcie_config_ops = {
    .read = xilinx_axi_pcie_config_read,
    .write = xilinx_axi_pcie_config_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static uint64_t xilinx_axi_pcie_read(void *opaque, hwaddr offset,
                                     unsigned int size)
{
    XilinxAXIPCIE *s = opaque;
    uint32_t ret = s->regs[offset >> 2];

    xilinx_axi_pcie_check_reg_access(offset, ret, true);
    return ret;    
}

static void xilinx_axi_pcie_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned int size)
{
    XilinxAXIPCIE *s = opaque;
    const XilinxAXIPCIERegInfo *info = &xilinx_axi_pcie_reg_info[offset >> 2];
    uint32_t new_value = value;
    uint32_t ro_mask;

    xilinx_axi_pcie_check_reg_access(offset, value, false);
    if (!info->name) {
        return;
    }

    offset >>= 2;
    assert(!(info->wtc & info->ro));
    /* preserve read-only and write to clear bits */
    ro_mask = info->ro | info->wtc | ~((1ull << info->width) - 1);
    new_value &= ~ro_mask;
    new_value |= ro_mask & s->regs[offset];
    /* do write to clear */
    new_value &= ~(value & info->wtc);
    s->regs[offset] = new_value;
    
    xilinx_axi_pcie_update_irq(s);
}

static const MemoryRegionOps xilinx_axi_pcie_ops = {
    .read = xilinx_axi_pcie_read,
    .write = xilinx_axi_pcie_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static int xilinx_axi_pcie_init(SysBusDevice *dev)
{
#if 0
    XilinxAXIPCIE *s = (XilinxAXIPCIE *)dev;

    DB_PRINT("\n");

    /*FIXME: Parameterise BAR size */
    memory_region_init(&s->pci_space, OBJECT(dev), "xilinx-pci-bar", 1ull << 32);
    s->pci_bus = pci_register_bus(DEVICE(dev), "xilinx-pci",
                                  NULL, NULL, s->irq,
                                  &s->pci_space, get_system_io(),
                                  0 , 1, TYPE_PCI_BUS);

    /*memory_region_init_alias(&s->axi_to_pci_bar[0], "foo", &s->pci_space,
                             0x40000000, 0x10000000);*/
    memory_region_init_io(&s->config, OBJECT(dev), &razwi_unimp_ops, s,
                          "xilinx-axi_pcie_bar", 0x10000000);
    memory_region_add_subregion(get_system_memory(), 0x70000000,
                                &s->axi_to_pci_bar[0]);

    memory_region_init_alias(&s->pci_to_axi_bar[0], OBJECT(dev), "bar", get_system_memory(),
                             0x50000000, 0x10000000);
    memory_region_add_subregion(&s->pci_space, 0x50000000,
                                &s->pci_to_axi_bar[0]);

    sysbus_init_irq(dev, &s->irq);
    memory_region_init(&s->container, OBJECT(dev), "a9mp-priv-container", 16 << 20);
    memory_region_init_io(&s->mmio, OBJECT(dev), &xilinx_axi_pcie_ops, s,
                          "xilinx-axi_pcie_mmio", R_MAX * 4);
    memory_region_add_subregion(&s->container, 0x130, &s->mmio);
    
    memory_region_init_io(&s->config, OBJECT(dev), &xilinx_axi_pcie_config_ops, s,
                          "xilinx-axi_pcie_mmio", 0x130);
    memory_region_add_subregion(&s->container, 0, &s->config);

    sysbus_init_mmio(dev, &s->container);

    s->irqline = -1;
#endif
    return 0;
}

static const VMStateDescription vmstate_xilinx_axi_pcie = {
    .name = "xlnx.axi-pcie",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XilinxAXIPCIE, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static Property xilinx_axi_pcie_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_axi_pcie_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = xilinx_axi_pcie_init;
    dc->reset = xilinx_axi_pcie_reset;
    dc->props = xilinx_axi_pcie_properties;
    dc->vmsd = &vmstate_xilinx_axi_pcie;
}

static TypeInfo xilinx_axi_pcie_info = {
    .name           = "xlnx.axi-pcie",
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XilinxAXIPCIE),
    .class_init     = xilinx_axi_pcie_class_init,
};

static void xilinx_axi_pcie_register_types(void)
{
    type_register_static(&xilinx_axi_pcie_info);
}

type_init(xilinx_axi_pcie_register_types)
