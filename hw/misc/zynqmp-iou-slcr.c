/*
 * Ronlado IOU system level control registers (SLCR)
 *
 * Copyright (c) 2013 Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"
#include "hw/fdt_generic_util.h"

#ifndef ZYNQMP_IOU_SLCR_ERR_DEBUG
#define ZYNQMP_IOU_SLCR_ERR_DEBUG 0
#endif

#define TYPE_ZYNQMP_IOU_SLCR "xilinx.zynqmp-iou-slcr"

#define ZYNQMP_IOU_SLCR(obj) \
     OBJECT_CHECK(ZynqMPIOUSLCR, (obj), TYPE_ZYNQMP_IOU_SLCR)

REG32(MIO, 0x0)
    #define R_MIO_RSVD               0xffffff01

REG32(SD_SLOTTYPE, 0x310)
    #define R_SD_SLOTTYPE_RSVD       0xffffff9c

#define R_MAX ((R_SD_SLOTTYPE) + 1)

typedef struct ZynqMPIOUSLCR ZynqMPIOUSLCR;

struct ZynqMPIOUSLCR {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
};

static const RegisterAccessInfo zynqmp_iou_slcr_regs_info[] = {
#define M(x) \
    {   .name = "MIO" #x,             .decode.addr = A_MIO + 4 * x,         \
            .rsvd = R_MIO_RSVD,                                             \
    },
    M( 0) M( 1) M( 2) M( 3) M( 4) M( 5) M( 6) M( 7) M( 8) M( 9)
    M(10) M(11) M(12) M(13) M(14) M(15) M(16) M(17) M(18) M(19)
    M(20) M(21) M(22) M(23) M(24) M(25) M(26) M(27) M(28) M(29)
    M(30) M(31) M(32) M(33) M(34) M(35) M(36) M(37) M(38) M(39)
    M(40) M(41) M(42) M(43) M(44) M(45) M(46) M(47) M(48) M(49)
    M(50) M(51) M(52) M(53) M(54) M(55) M(56) M(57) M(58) M(59)
    M(60) M(61) M(62) M(63) M(64) M(65) M(66) M(67) M(78) M(69)
    M(70) M(71) M(72) M(73) M(74) M(75) M(76) M(77)
#undef M
    {   .name = "SD Slot TYPE",             .decode.addr = A_SD_SLOTTYPE,
            .rsvd = R_SD_SLOTTYPE_RSVD,
            .gpios = (RegisterGPIOMapping []) {
                { .name = "SD0_SLOTTYPE",   .bit_pos = 0,    .width = 2 },
                { .name = "SD1_SLOTTYPE",   .bit_pos = 15,    .width = 2 },
                {},
            }
    }
};

static void zynqmp_iou_slcr_reset(DeviceState *dev)
{
    ZynqMPIOUSLCR *s = ZYNQMP_IOU_SLCR(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static const MemoryRegionOps zynqmp_iou_slcr_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void zynqmp_iou_slcr_realize(DeviceState *dev, Error **errp)
{
    ZynqMPIOUSLCR *s = ZYNQMP_IOU_SLCR(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < ARRAY_SIZE(zynqmp_iou_slcr_regs_info); ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    zynqmp_iou_slcr_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &zynqmp_iou_slcr_regs_info[i],
            .debug = ZYNQMP_IOU_SLCR_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        register_init(r);
        qdev_pass_all_gpios(DEVICE(r), dev);

        memory_region_init_io(&r->mem, OBJECT(dev), &zynqmp_iou_slcr_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, r->access->decode.addr, &r->mem);
    }
    return;
}

static void zynqmp_iou_slcr_init(Object *obj)
{
    ZynqMPIOUSLCR *s = ZYNQMP_IOU_SLCR(obj);

    memory_region_init(&s->iomem, obj, "MMIO", R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_zynqmp_iou_slcr = {
    .name = "zynqmp_iou_slcr",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, ZynqMPIOUSLCR, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const FDTGenericGPIOSet zynqmp_iou_slcr_controller_gpios [] = {
    {
        /* FIXME: this could be a much better name */
        .names = &fdt_generic_gpio_name_set_gpio,
        .gpios = (FDTGenericGPIOConnection []) {
            { .name = "SD0_SLOTTYPE",   .fdt_index = 0 },
            { .name = "SD1_SLOTTYPE",   .fdt_index = 1 },
            { },
        },
    },
    { },
};

static void zynqmp_iou_slcr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);

    dc->reset = zynqmp_iou_slcr_reset;
    dc->realize = zynqmp_iou_slcr_realize;
    dc->vmsd = &vmstate_zynqmp_iou_slcr;

    fggc->controller_gpios = zynqmp_iou_slcr_controller_gpios;
}

static const TypeInfo zynqmp_iou_slcr_info = {
    .name          = TYPE_ZYNQMP_IOU_SLCR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPIOUSLCR),
    .class_init    = zynqmp_iou_slcr_class_init,
    .instance_init = zynqmp_iou_slcr_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_GPIO },
        { },
    }
};

static void zynqmp_iou_slcr_register_types(void)
{
    type_register_static(&zynqmp_iou_slcr_info);
}

type_init(zynqmp_iou_slcr_register_types)
