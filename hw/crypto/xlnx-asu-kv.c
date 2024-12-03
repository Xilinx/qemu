/*
 * Xilinx ASU keyvault
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
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/crypto/xlnx-asu-kv.h"
#include "trace.h"

REG32(AES_KEY_SEL, 0x0)
    FIELD(AES_KEY_SEL, SRC, 0, 32)

REG32(AES_KEY_CLEAR, 0x4)
    FIELD(AES_KEY_CLEAR, USER_KEY_0, 0, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_1, 1, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_2, 2, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_3, 3, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_4, 4, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_5, 5, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_6, 6, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_7, 7, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_0, 8, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_1, 9, 1)
    FIELD(AES_KEY_CLEAR, PUF_KEY, 10, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_RED_0, 11, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_RED_1, 12, 1)
    FIELD(AES_KEY_CLEAR, AES_KEY_ZEROIZE, 13, 1)
    FIELD(AES_KEY_CLEAR, RAM_KEY_CLEAR, 14, 1)
    FIELD(AES_KEY_CLEAR, RESERVED, 15, 17)

REG32(KEY_ZEROED_STATUS, 0x8)
    FIELD(KEY_ZEROED_STATUS, AES_KEY_ZEROED, 0, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_0, 1, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_1, 2, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_2, 3, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_3, 4, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_4, 5, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_5, 6, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_6, 7, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_7, 8, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_KEY_0, 9, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_KEY_1, 10, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_KEY_RED_0, 11, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_KEY_RED_1, 12, 1)
    FIELD(KEY_ZEROED_STATUS, PUF_KEY, 13, 1)
    FIELD(KEY_ZEROED_STATUS, RESERVED, 14, 18)

REG32(KEY_LOCK_CTRL, 0x10)
    FIELD(KEY_LOCK_CTRL, EFUSE, 0, 32)

REG32(KEY_LOCK_STATUS, 0x14)
    FIELD(KEY_LOCK_STATUS, EFUSE, 0, 1)
    FIELD(KEY_LOCK_STATUS, RESERVED, 1, 31)

REG32(AES_USER_SEL_CRC, 0x18)
    FIELD(AES_USER_SEL_CRC, VALUE, 0, 3)
#define AES_USER_SEL_CRC_WRITE_MASK R_AES_USER_SEL_CRC_VALUE_MASK

REG32(AES_USER_SEL_CRC_VALUE, 0x1c)
    FIELD(AES_USER_SEL_CRC_VALUE, VALUE, 0, 32)

REG32(AES_USER_KEY_CRC_STATUS, 0x20)
    FIELD(AES_USER_KEY_CRC_STATUS, PASS, 0, 1)
    FIELD(AES_USER_KEY_CRC_STATUS, DONE, 1, 1)

REG32(KEY_MASK_0, 0x24)
/* ... */
REG32(KEY_MASK_7, 0x40)

REG32(KEY_LOCK_0, 0x44)
    FIELD(KEY_LOCK_0, VALUE, 0, 1)
/* ... */
REG32(KEY_LOCK_7, 0x60)

REG32(USER_KEY_0_0, 0x64)
/* ... */
REG32(USER_KEY_1_0, 0x84)
/* ... */
REG32(USER_KEY_2_7, 0xc0)
/* register map irregularity: 0x10 gap */
REG32(USER_KEY_3_0, 0xd4)
/* ... */
REG32(USER_KEY_7_7, 0x170)

REG32(AES_KEY_SIZE, 0x174)
    FIELD(AES_KEY_SIZE, SELECT, 0, 2)

REG32(AES_KEY_TO_BE_DEC_SIZE, 0x178)
    FIELD(AES_KEY_TO_BE_DEC_SIZE, SELECT, 0, 2)

REG32(AES_KEY_DEC_MODE, 0x17c)
    FIELD(AES_KEY_DEC_MODE, VALUE, 0, 32)

REG32(AES_KEY_TO_BE_DEC_SEL, 0x180)
    FIELD(AES_KEY_TO_BE_DEC_SEL, SRC, 0, 32)

REG32(ASU_PMC_KEY_TRANSFER_READY, 0x184)
    FIELD(ASU_PMC_KEY_TRANSFER_READY, VAL, 0, 1)

REG32(EFUSE_KEY_0_BLACK_OR_RED, 0x188)
    FIELD(EFUSE_KEY_0_BLACK_OR_RED, VAL, 0, 2)
REG32(EFUSE_KEY_1_BLACK_OR_RED, 0x18c)
    FIELD(EFUSE_KEY_1_BLACK_OR_RED, VAL, 0, 2)

REG32(AES_PL_KEY_SEL, 0x190)
    FIELD(AES_PL_KEY_SEL, SRC, 0, 32)

REG32(KV_INTERRUPT_STATUS, 0x194)
    FIELD(KV_INTERRUPT_STATUS, KT_DONE, 0, 1)
REG32(KV_INTERRUPT_MASK, 0x198)
REG32(KV_INTERRUPT_ENABLE, 0x19c)
REG32(KV_INTERRUPT_DISABLE, 0x1a0)
REG32(KV_INTERRUPT_TRIGGER, 0x1a4)

REG32(KV_ADDR_ERROR_STATUS, 0x1ac)
    FIELD(KV_ADDR_ERROR_STATUS, KV_ADDR_DECODE_ERROR, 0, 1)
REG32(KV_ADDR_ERROR_MASK, 0x1b0)
REG32(KV_ADDR_ERROR_ENABLE, 0x1b4)
REG32(KV_ADDR_ERROR_DISABLE, 0x1b8)
REG32(KV_ADDR_ERROR_TRIGGER, 0x1bc)

/* Key flags */
enum {
    ASU_KV_KEY_SET = 1u << 0,
    ASU_KV_KEY_LOCKED = 1u << 1,
    ASU_KV_KEY_CRC_CHECKED = 1u << 2,
};

static const char *ASU_KV_KEY_STR[] = {
    [XILINX_ASU_KV_USER_0] = "user-0",
    [XILINX_ASU_KV_USER_1] = "user-1",
    [XILINX_ASU_KV_USER_2] = "user-2",
    [XILINX_ASU_KV_USER_3] = "user-3",
    [XILINX_ASU_KV_USER_4] = "user-4",
    [XILINX_ASU_KV_USER_5] = "user-5",
    [XILINX_ASU_KV_USER_6] = "user-6",
    [XILINX_ASU_KV_USER_7] = "user-7",
    [XILINX_ASU_KV_EFUSE_0] = "efuse-0",
    [XILINX_ASU_KV_EFUSE_1] = "efuse-1",
    [XILINX_ASU_KV_EFUSE_BLACK_0] = "efuse-black-0",
    [XILINX_ASU_KV_EFUSE_BLACK_1] = "efuse-black-1",
    [XILINX_ASU_KV_PUF] = "puf",
};

static inline bool key_is_locked(const XilinxAsuKvState *s, size_t idx)
{
    g_assert(idx < XILINX_ASU_KV_EFUSE_0);
    return !!(s->key[idx].flags & ASU_KV_KEY_LOCKED);
}

static inline void key_set_locked(XilinxAsuKvState *s, size_t idx)
{
    g_assert(idx < XILINX_ASU_KV_EFUSE_0);
    s->key[idx].flags |= ASU_KV_KEY_LOCKED;
}

static inline bool key_is_cleared(const XilinxAsuKvState *s, size_t idx)
{
    return !(s->key[idx].flags & ASU_KV_KEY_SET);
}

static inline void key_clear(XilinxAsuKvState *s, size_t idx)
{
    /*
     * Clear the flags as well. A key clear operation unlocks the key and
     * clears the CRC checked status.
     */
    memset(&s->key[idx], 0, sizeof(s->key[idx]));
}

static inline void key_mark_set(XilinxAsuKvState *s, size_t idx)
{
    g_assert(!(s->key[idx].flags & ASU_KV_KEY_LOCKED));

    /* clear CRC_CHECKED flag if set. (LOCKED is unset for sure) */
    s->key[idx].flags = ASU_KV_KEY_SET;
}

static inline bool key_is_crc_checked(const XilinxAsuKvState *s, size_t idx)
{
    return !!(s->key[idx].flags & ASU_KV_KEY_CRC_CHECKED);
}

static inline void key_set_crc_checked(XilinxAsuKvState *s, size_t idx)
{
    s->key[idx].flags |= ASU_KV_KEY_CRC_CHECKED;
}

static void user_key_write(XilinxAsuKvState *s, hwaddr addr,
                           uint32_t value)
{
    const size_t STRIDE = A_USER_KEY_1_0 - A_USER_KEY_0_0;
    size_t key_idx, sub_idx;

    if (addr >= A_USER_KEY_3_0) {
        /* workaround the buggy register map */
        addr -= 0x10;
    }

    key_idx = (addr - A_USER_KEY_0_0) / STRIDE;
    key_idx += XILINX_ASU_KV_USER_0;
    sub_idx = (addr - A_USER_KEY_0_0) % STRIDE;
    sub_idx /= sizeof(uint32_t);

    if (key_is_locked(s, key_idx)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_KV ": trying to write to locked key "
                      "%zu\n", key_idx);
        return;
    }

    /* store the key in big endian. This is how the AES model expects it. */
    sub_idx = ARRAY_SIZE(s->key[key_idx].val) - (sub_idx + 1);
    s->key[key_idx].val[sub_idx] = bswap32(value);

    key_mark_set(s, key_idx);
    trace_xilinx_asu_kv_write_key(ASU_KV_KEY_STR[key_idx]);
}

static void do_key_clearing(XilinxAsuKvState *s, uint32_t value)
{
    static const size_t KEY_MAPPING[] = {
        [XILINX_ASU_KV_USER_0] = R_AES_KEY_CLEAR_USER_KEY_0_SHIFT,
        [XILINX_ASU_KV_USER_1] = R_AES_KEY_CLEAR_USER_KEY_1_SHIFT,
        [XILINX_ASU_KV_USER_2] = R_AES_KEY_CLEAR_USER_KEY_2_SHIFT,
        [XILINX_ASU_KV_USER_3] = R_AES_KEY_CLEAR_USER_KEY_3_SHIFT,
        [XILINX_ASU_KV_USER_4] = R_AES_KEY_CLEAR_USER_KEY_4_SHIFT,
        [XILINX_ASU_KV_USER_5] = R_AES_KEY_CLEAR_USER_KEY_5_SHIFT,
        [XILINX_ASU_KV_USER_6] = R_AES_KEY_CLEAR_USER_KEY_6_SHIFT,
        [XILINX_ASU_KV_USER_7] = R_AES_KEY_CLEAR_USER_KEY_7_SHIFT,
        [XILINX_ASU_KV_EFUSE_0] = R_AES_KEY_CLEAR_EFUSE_KEY_RED_0_SHIFT,
        [XILINX_ASU_KV_EFUSE_1] = R_AES_KEY_CLEAR_EFUSE_KEY_RED_1_SHIFT,
        [XILINX_ASU_KV_EFUSE_BLACK_0] = R_AES_KEY_CLEAR_EFUSE_KEY_0_SHIFT,
        [XILINX_ASU_KV_EFUSE_BLACK_1] = R_AES_KEY_CLEAR_EFUSE_KEY_1_SHIFT,
        [XILINX_ASU_KV_PUF] = R_AES_KEY_CLEAR_PUF_KEY_SHIFT,
    };

    size_t i;

    for (i = 0; i < ARRAY_SIZE(KEY_MAPPING); i++) {
        if ((1 << KEY_MAPPING[i]) & value) {
            trace_xilinx_asu_kv_clear_key(ASU_KV_KEY_STR[i]);
            key_clear(s, i);
        }
    }
}

static uint32_t get_key_clear_status(XilinxAsuKvState *s)
{
    static const size_t KEY_MAPPING[] = {
        [XILINX_ASU_KV_USER_0] = R_KEY_ZEROED_STATUS_USER_KEY_0_MASK,
        [XILINX_ASU_KV_USER_1] = R_KEY_ZEROED_STATUS_USER_KEY_1_MASK,
        [XILINX_ASU_KV_USER_2] = R_KEY_ZEROED_STATUS_USER_KEY_2_MASK,
        [XILINX_ASU_KV_USER_3] = R_KEY_ZEROED_STATUS_USER_KEY_3_MASK,
        [XILINX_ASU_KV_USER_4] = R_KEY_ZEROED_STATUS_USER_KEY_4_MASK,
        [XILINX_ASU_KV_USER_5] = R_KEY_ZEROED_STATUS_USER_KEY_5_MASK,
        [XILINX_ASU_KV_USER_6] = R_KEY_ZEROED_STATUS_USER_KEY_6_MASK,
        [XILINX_ASU_KV_USER_7] = R_KEY_ZEROED_STATUS_USER_KEY_7_MASK,
        [XILINX_ASU_KV_EFUSE_0] = R_KEY_ZEROED_STATUS_EFUSE_KEY_RED_0_MASK,
        [XILINX_ASU_KV_EFUSE_1] = R_KEY_ZEROED_STATUS_EFUSE_KEY_RED_1_MASK,
        [XILINX_ASU_KV_EFUSE_BLACK_0] = R_KEY_ZEROED_STATUS_EFUSE_KEY_0_MASK,
        [XILINX_ASU_KV_EFUSE_BLACK_1] = R_KEY_ZEROED_STATUS_EFUSE_KEY_1_MASK,
        [XILINX_ASU_KV_PUF] = R_KEY_ZEROED_STATUS_PUF_KEY_MASK,
    };

    uint32_t ret = 0;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(KEY_MAPPING); i++) {
        if (key_is_cleared(s, i)) {
            ret |= KEY_MAPPING[i];
        }
    }

    return ret;
}

static uint64_t xilinx_asu_kv_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(opaque);
    uint64_t ret;

    switch (addr) {
    case A_KEY_ZEROED_STATUS:
        ret = get_key_clear_status(s);
        break;

    case A_USER_KEY_0_0 ... A_USER_KEY_7_7:
    case A_AES_KEY_CLEAR:
    case A_KV_INTERRUPT_ENABLE:
    case A_KV_INTERRUPT_DISABLE:
    case A_KV_INTERRUPT_TRIGGER:
    case A_AES_KEY_DEC_MODE:
        /* wo */
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_KV ": read to write-only register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_XILINX_ASU_KV ": read to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        ret = 0;
        break;
    }

    trace_xilinx_asu_kv_read(addr, ret, size);
    return ret;
}

static void xilinx_asu_kv_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(opaque);

    trace_xilinx_asu_kv_write(addr, value, size);

    switch (addr) {
    case A_AES_KEY_CLEAR:
        do_key_clearing(s, value);
        break;

    case A_USER_KEY_0_0 ... A_USER_KEY_7_7:
        user_key_write(s, addr, value);
        break;

    case A_AES_USER_KEY_CRC_STATUS:
    case A_KEY_ZEROED_STATUS:
    case A_KV_INTERRUPT_MASK:
        /* ro */
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_KV ": write to read-only register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_XILINX_ASU_KV ": write to unimplemented register "
                      "at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps xilinx_asu_kv_ops = {
    .read = xilinx_asu_kv_read,
    .write = xilinx_asu_kv_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xilinx_asu_kv_reset_enter(Object *obj, ResetType type)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(obj);

    memset(s->key, 0, sizeof(s->key));
}

static void xilinx_asu_kv_reset_hold(Object *obj)
{
}

static void xilinx_asu_kv_realize(DeviceState *dev, Error **errp)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &xilinx_asu_kv_ops,
                          s, TYPE_XILINX_ASU_KV,
                          XILINX_ASU_KV_MMIO_LEN);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property xilinx_asu_kv_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

static void xilinx_asu_kv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = xilinx_asu_kv_realize;
    rc->phases.enter = xilinx_asu_kv_reset_enter;
    rc->phases.hold = xilinx_asu_kv_reset_hold;
    device_class_set_props(dc, xilinx_asu_kv_properties);
}

static const TypeInfo xilinx_asu_kv_info = {
    .name = TYPE_XILINX_ASU_KV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAsuKvState),
    .class_init = xilinx_asu_kv_class_init,
    .class_size = sizeof(XilinxAsuKvClass),
    .interfaces = (InterfaceInfo []) {
        { }
    },
};

static void xilinx_asu_kv_register_types(void)
{
    type_register_static(&xilinx_asu_kv_info);
}

type_init(xilinx_asu_kv_register_types)
