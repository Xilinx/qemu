/*
 * QEMU USB EHCI Emulation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/register-dep.h"

static const VMStateDescription vmstate_ehci_sysbus = {
    .name        = "ehci-sysbus",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(ehci, EHCISysBusState, 2, vmstate_ehci, EHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static Property ehci_sysbus_properties[] = {
    DEFINE_PROP_UINT32("maxframes", EHCISysBusState, ehci.maxframes, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_ehci_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(dev);
    EHCIState *s = &i->ehci;

    usb_ehci_realize(s, dev, errp);
    sysbus_init_irq(d, &s->irq);
}

static void usb_ehci_sysbus_reset(DeviceState *dev)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(d);
    EHCIState *s = &i->ehci;

    ehci_reset(s);
}

static void ehci_sysbus_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_GET_CLASS(obj);
    EHCIState *s = &i->ehci;

    s->capsbase = sec->capsbase;
    s->opregbase = sec->opregbase;
    s->portscbase = sec->portscbase;
    s->portnr = sec->portnr;
    s->as = &address_space_memory;

    usb_ehci_init(s, DEVICE(obj));
    sysbus_init_mmio(d, &s->mem);
}

static void ehci_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(klass);

    sec->portscbase = 0x44;
    sec->portnr = NB_PORTS;

    dc->realize = usb_ehci_sysbus_realize;
    dc->vmsd = &vmstate_ehci_sysbus;
    dc->props = ehci_sysbus_properties;
    dc->reset = usb_ehci_sysbus_reset;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_type_info = {
    .name          = TYPE_SYS_BUS_EHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(EHCISysBusState),
    .instance_init = ehci_sysbus_init,
    .abstract      = true,
    .class_init    = ehci_sysbus_class_init,
    .class_size    = sizeof(SysBusEHCIClass),
};

enum PS7USBRegs {
    XLNX_ID = 0x0,
    XLNX_HWGENERAL = 0x4,
    XLNX_HWHOST = 0x8,
    XLNX_HWTXBUF = 0x10,
    XLNX_HWRXBUF = 0x14,
    XLNX_DCIVERSION = 0x120,
    XLNX_DCCPARAMS  = 0x124,
};

/* FIXME: Add the functionality of remaining phy registers */
enum ULPIRegs {
    VENDOR_ID_L = 0x0,
    VENDOR_ID_H = 0x1,
    PRODUCT_ID_L = 0x2,
    PRODUCT_ID_H = 0x3,
    SCRATCH_REG_0 = 0x16,
};

DEP_REG32(ULPI_VIEWPORT, PS7USB_ULPIVP_OFFSET)
    DEP_FIELD(ULPI_VIEWPORT, ULPIDATWR, 8, 0)
    DEP_FIELD(ULPI_VIEWPORT, ULPIDATRD, 8, 8)
    DEP_FIELD(ULPI_VIEWPORT, ULPIADDR, 8, 16)
    DEP_FIELD(ULPI_VIEWPORT, ULPIPORT, 3, 24)
    DEP_FIELD(ULPI_VIEWPORT, ULPISS, 1, 27)
    DEP_FIELD(ULPI_VIEWPORT, ULPIRW, 1, 29)
    DEP_FIELD(ULPI_VIEWPORT, ULPIRUN, 1, 30)
    DEP_FIELD(ULPI_VIEWPORT, ULPIWU, 1, 31)

static void ehci_xlnx_reset(DeviceState *dev)
{
    PS7USBState *s = XLNX_PS7_USB(dev);
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(d);
    EHCIState *es = &i->ehci;

    ehci_reset(es);

    /* Show phy in normal functioning state after init */
    s->ulpi_viewport = 0x8000000;
    /* Vendor and product ID are as per micron ulpi phy specifications */
    s->ulpireg[VENDOR_ID_L] = 0x24;
    s->ulpireg[VENDOR_ID_H] = 0x04;
    s->ulpireg[PRODUCT_ID_L] = 0x4;
    s->ulpireg[PRODUCT_ID_H] = 0x0;

}

static void ehci_xlnx_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->reset = ehci_xlnx_reset;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
}

static uint64_t xlnx_devreg_read(void *opaque, hwaddr addr, unsigned size)
{
    EHCIState *s = opaque;
        /* DCIVERSION and DCCPARAMS are mapped at 0x20 words distance from
         * end of capacity registers
         */
    hwaddr offset = s->capsbase + 0x20 + addr;

    switch (offset) {
    case XLNX_DCIVERSION:
        return 0x00000001;
    case XLNX_DCCPARAMS:
        /* Host mode enabled
         * Number of endpoints fixed to 12 as per zynq-7000
         */
        return 0x0000010C;
    }
    return 0;
}

static uint64_t xlnx_hwreg_read(void *opaque, hwaddr addr, unsigned size)
{
    /* All the following registers will just read out default values as per
     * dwc_usb2_hs_device_controller spec
     */
    switch (addr) {
    case XLNX_ID:
        return XLNX_ID_DEFVAL;
    case XLNX_HWGENERAL:
        return XLNX_HWGENERAL_DEFVAL;
    case XLNX_HWHOST:
        return XLNX_HWHOST_DEFVAL;
    case XLNX_HWTXBUF:
        return XLNX_HWTXBUF_DEFVAL;
    case XLNX_HWRXBUF:
        return XLNX_HWRXBUF_DEFVAL;
    }
    return 0;
}

static uint64_t xlnx_ulpi_read(void *opaque, hwaddr addr, unsigned size)
{
    PS7USBState *s = opaque;

    return s->ulpi_viewport;
}

static void xlnx_ulpi_write(void *opaque, hwaddr addr, uint64_t data,
                            unsigned size)
{
    PS7USBState *s = opaque;
    uint8_t ulpiaddr;
    /* Clear RW feilds before writes */
    s->ulpi_viewport &= ~ULPIREG_RWBITS_MASK;
    s->ulpi_viewport |= data & ULPIREG_RWBITS_MASK;

    /* ULPI Wake Up call : Clear the bit when set */
    if(DEP_F_EX32(s->ulpi_viewport, ULPI_VIEWPORT, ULPIWU)) {
        s->ulpi_viewport = DEP_F_DP32(s->ulpi_viewport, ULPI_VIEWPORT, ULPIWU, 0);
    }

    if (DEP_F_EX32(s->ulpi_viewport, ULPI_VIEWPORT, ULPIRUN)) {
        ulpiaddr = DEP_F_EX32(s->ulpi_viewport, ULPI_VIEWPORT, ULPIADDR);

        if (DEP_F_EX32(s->ulpi_viewport, ULPI_VIEWPORT, ULPIRW)) {
            s->ulpireg[ulpiaddr] = DEP_F_EX32(s->ulpi_viewport, ULPI_VIEWPORT,
                                          ULPIDATWR);
        } else {
            s->ulpi_viewport = DEP_F_DP32(s->ulpi_viewport, ULPI_VIEWPORT,
                                      ULPIDATRD, s->ulpireg[ulpiaddr]);
        }

        s->ulpi_viewport = DEP_F_DP32(s->ulpi_viewport, ULPI_VIEWPORT, ULPIRUN, 0);
    }
}

static const MemoryRegionOps ps7usb_devreg_ops = {
    .read = xlnx_devreg_read,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps ps7usb_hwreg_ops = {
    .read = xlnx_hwreg_read,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps ps7usb_ulpi_ops = {
    .read = xlnx_ulpi_read,
    .write = xlnx_ulpi_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ehci_xlnx_init(Object *Obj)
{
    EHCISysBusState *p = SYS_BUS_EHCI(Obj);
    PS7USBState *s = XLNX_PS7_USB(Obj);
    EHCIState *pp = &p->ehci;
    memory_region_init_io(&s->mem_hwreg, Obj, &ps7usb_hwreg_ops, pp,
                          "ps7usb_hwreg", PS7USB_HWREG_SIZE);
    memory_region_add_subregion(&pp->mem, PS7USB_HWREG_OFFSET, &s->mem_hwreg);

    memory_region_init_io(&s->mem_devreg, Obj, &ps7usb_devreg_ops, pp,
                          "ps7usb_devicemode", PS7USB_DEVREG_SIZE);
    memory_region_add_subregion(&pp->mem, PS7USB_DEVREG_OFFSET, &s->mem_devreg);

    memory_region_init_io(&s->mem_ulpi, Obj, &ps7usb_ulpi_ops, s,
                          "ps7usb_ulpi_viewport", PS7USB_ULPIVP_SIZE);
    memory_region_add_subregion(&pp->mem, PS7USB_ULPIVP_OFFSET, &s->mem_ulpi);
}

static const TypeInfo ehci_xlnx_type_info = {
    .name          = TYPE_XLNX_PS7_USB,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_xlnx_class_init,
    .instance_size = sizeof(PS7USBState),
    .instance_init = ehci_xlnx_init,
};

static void ehci_exynos4210_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_exynos4210_type_info = {
    .name          = TYPE_EXYNOS4210_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_exynos4210_class_init,
};

static void ehci_tegra2_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_tegra2_type_info = {
    .name          = TYPE_TEGRA2_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_tegra2_class_init,
};

/*
 * Faraday FUSBH200 USB 2.0 EHCI
 */

/**
 * FUSBH200EHCIRegs:
 * @FUSBH200_REG_EOF_ASTR: EOF/Async. Sleep Timer Register
 * @FUSBH200_REG_BMCSR: Bus Monitor Control/Status Register
 */
enum FUSBH200EHCIRegs {
    FUSBH200_REG_EOF_ASTR = 0x34,
    FUSBH200_REG_BMCSR    = 0x40,
};

static uint64_t fusbh200_ehci_read(void *opaque, hwaddr addr, unsigned size)
{
    EHCIState *s = opaque;
    hwaddr off = s->opregbase + s->portscbase + 4 * s->portnr + addr;

    switch (off) {
    case FUSBH200_REG_EOF_ASTR:
        return 0x00000041;
    case FUSBH200_REG_BMCSR:
        /* High-Speed, VBUS valid, interrupt level-high active */
        return (2 << 9) | (1 << 8) | (1 << 3);
    }

    return 0;
}

static void fusbh200_ehci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
}

static const MemoryRegionOps fusbh200_ehci_mmio_ops = {
    .read = fusbh200_ehci_read,
    .write = fusbh200_ehci_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fusbh200_ehci_init(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    FUSBH200EHCIState *f = FUSBH200_EHCI(obj);
    EHCIState *s = &i->ehci;

    memory_region_init_io(&f->mem_vendor, OBJECT(f), &fusbh200_ehci_mmio_ops, s,
                          "fusbh200", 0x4c);
    memory_region_add_subregion(&s->mem,
                                s->opregbase + s->portscbase + 4 * s->portnr,
                                &f->mem_vendor);
}

static void fusbh200_ehci_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x20;
    sec->portnr = 1;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_fusbh200_type_info = {
    .name          = TYPE_FUSBH200_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .instance_size = sizeof(FUSBH200EHCIState),
    .instance_init = fusbh200_ehci_init,
    .class_init    = fusbh200_ehci_class_init,
};

static void ehci_sysbus_register_types(void)
{
    type_register_static(&ehci_type_info);
    type_register_static(&ehci_xlnx_type_info);
    type_register_static(&ehci_exynos4210_type_info);
    type_register_static(&ehci_tegra2_type_info);
    type_register_static(&ehci_fusbh200_type_info);
}

type_init(ehci_sysbus_register_types)
