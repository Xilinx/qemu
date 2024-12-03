/*
 * ASU AES Engine
 *
 * Copyright (c) 2024, Advanced Micro Device, Inc.
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
#include "qemu/log.h"
#include "crypto/aes.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/crypto/xlnx-asu-aes-new.h"
#include "trace.h"

REG32(STATUS, 0x0)
    FIELD(STATUS, BUSY, 0, 1)
    FIELD(STATUS, READY, 1, 1)

REG32(OPERATION, 0x4)
    FIELD(OPERATION, KEY_LOAD, 0, 1)
    FIELD(OPERATION, IV_LOAD, 1, 1)
    FIELD(OPERATION, INTMAC_LOAD, 2, 1)
    FIELD(OPERATION, S0_LOAD, 3, 1)

REG32(SOFT_RST, 0xc)
    FIELD(SOFT_RST, RESET, 0, 1)

REG32(IV_IN_0, 0x10)
/* ... */
REG32(IV_IN_3, 0x1c)

REG32(IV_MASK_IN_0, 0x20)
/* ... */
REG32(IV_MASK_IN_3, 0x2c)

REG32(IV_OUT_0, 0x30)
/* ... */
REG32(IV_OUT_3, 0x3c)

REG32(IV_MASK_OUT_0, 0x40)
/* ... */
REG32(IV_MASK_OUT_3, 0x4c)

REG32(KEY_DEC_TRIG, 0x5c)
    FIELD(KEY_DEC_TRIG, VALUE, 0, 1)

REG32(CM, 0x70)
    FIELD(CM, ENABLE, 0, 3)

REG32(SPLIT_CFG, 0x74)
    FIELD(SPLIT_CFG, DATA_SPLIT, 0, 1)
    FIELD(SPLIT_CFG, KEY_SPLIT, 1, 1)
#define SPLIT_CFG_WRITE_MASK 0x3

REG32(MODE_CONFIG, 0x78)
    FIELD(MODE_CONFIG, ENGINE_MODE, 0, 4)
    FIELD(MODE_CONFIG, ENC_DEC_N, 6, 1)
    FIELD(MODE_CONFIG, AUTH, 13, 1)
    FIELD(MODE_CONFIG, AUTH_WITH_NO_PAYLOAD, 14, 1)
#define MODE_CONFIG_WRITE_MASK 0x604f

REG32(MAC_OUT_0, 0x80)
/* ... */
REG32(MAC_OUT_3, 0x8c)

REG32(MAC_MASK_OUT_0, 0x90)
/* ... */
REG32(MAC_MASK_OUT_3, 0x9c)

REG32(DATA_SWAP, 0x100)
    FIELD(DATA_SWAP, DISABLE, 0, 1)

REG32(INTERRUPT_STATUS, 0x104)
    FIELD(INTERRUPT_STATUS, DONE, 0, 1)
REG32(INTERRUPT_MASK, 0x108)
REG32(INTERRUPT_ENABLE, 0x10c)
REG32(INTERRUPT_DISABLE, 0x110)
REG32(INTERRUPT_TRIGGER, 0x114)

REG32(INT_MAC_IN_0, 0x120)
/* ... */
REG32(INT_MAC_IN_3, 0x12c)

REG32(INT_MAC_MASK_IN_0, 0x130)
/* ... */
REG32(INT_MAC_MASK_IN_3, 0x13c)

REG32(INT_MAC_OUT_0, 0x140)
/* ... */
REG32(INT_MAC_OUT_3, 0x14c)

REG32(INT_MAC_MASK_OUT_0, 0x150)
/* ... */
REG32(INT_MAC_MASK_OUT_3, 0x15c)

REG32(S0_IN_0, 0x160)
/* ... */
REG32(S0_IN_3, 0x16c)

REG32(S0_MASK_IN_0, 0x170)
/* ... */
REG32(S0_MASK_IN_3, 0x17c)

REG32(S0_OUT_0, 0x180)
/* ... */
REG32(S0_OUT_3, 0x18c)

REG32(S0_MASK_OUT_0, 0x190)
/* ... */
REG32(S0_MASK_OUT_3, 0x19c)

REG32(GCMLEN_IN_0, 0x1a0)
/* ... */
REG32(GCMLEN_IN_3, 0x1ac)

REG32(GCMLEN_OUT_0, 0x1b0)
/* ... */
REG32(GCMLEN_OUT_3, 0x1bc)

REG32(SLV_ERR_CTRL_STATUS, 0x220)
    FIELD(SLV_ERR_CTRL_STATUS, ADDR_DECODE_ERR, 0, 1)
REG32(SLV_ERR_CTRL_MASK, 0x224)
REG32(SLV_ERR_CTRL_ENABLE, 0x228)
REG32(SLV_ERR_CTRL_DISABLE, 0x22c)
REG32(SLV_ERR_CTRL_TRIGGER, 0x230)

static uint64_t xilinx_asu_aes_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    uint64_t ret;

    switch (addr) {
    case A_CM:
    case A_OPERATION:
    case A_KEY_DEC_TRIG:
    case A_INTERRUPT_ENABLE:
    case A_INTERRUPT_DISABLE:
    case A_INTERRUPT_TRIGGER:
        /* wo */
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_AES ": read to write-only register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_XILINX_ASU_AES ": read to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_xilinx_asu_aes_read(addr, ret, size);
    return ret;
}

static void xilinx_asu_aes_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
    trace_xilinx_asu_aes_write(addr, value, size);

    switch (addr) {
    case A_STATUS:
    case A_IV_OUT_0 ... A_IV_OUT_3:
    case A_IV_MASK_OUT_0 ... A_IV_MASK_OUT_3:
    case A_MAC_OUT_0 ... A_MAC_OUT_3:
    case A_MAC_MASK_OUT_0 ... A_MAC_MASK_OUT_3:
    case A_INT_MAC_OUT_0 ... A_INT_MAC_OUT_3:
    case A_INT_MAC_MASK_OUT_0 ... A_INT_MAC_MASK_OUT_3:
    case A_S0_OUT_0 ... A_S0_OUT_3:
    case A_S0_MASK_OUT_0 ... A_S0_MASK_OUT_3:
    case A_GCMLEN_OUT_0 ... A_GCMLEN_OUT_3:
    case A_INTERRUPT_MASK:
        /* ro */
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_AES ": write to read-only register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_XILINX_ASU_AES ": write to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps xilinx_asu_aes_ops = {
    .read = xilinx_asu_aes_read,
    .write = xilinx_asu_aes_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xilinx_asu_aes_reset_enter(Object *obj, ResetType type)
{
}

static void xilinx_asu_aes_reset_hold(Object *obj)
{
}

static void xilinx_asu_aes_realize(DeviceState *dev, Error **errp)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &xilinx_asu_aes_ops,
                          s, TYPE_XILINX_ASU_AES,
                          XILINX_ASU_AES_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property xilinx_asu_aes_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static void xilinx_asu_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = xilinx_asu_aes_realize;
    rc->phases.enter = xilinx_asu_aes_reset_enter;
    rc->phases.hold = xilinx_asu_aes_reset_hold;
    device_class_set_props(dc, xilinx_asu_aes_properties);
}

static const TypeInfo xilinx_asu_aes_info = {
    .name = TYPE_XILINX_ASU_AES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAsuAesState),
    .class_init = xilinx_asu_aes_class_init,
    .class_size = sizeof(XilinxAsuAesClass),
    .interfaces = (InterfaceInfo[]) {
        { }
    },
};

static void xilinx_asu_aes_register_types(void)
{
    type_register_static(&xilinx_asu_aes_info);
}

type_init(xilinx_asu_aes_register_types)
