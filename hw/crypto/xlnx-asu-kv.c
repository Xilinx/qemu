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
#include "hw/nvram/xlnx-efuse.h"
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

/* Valid key size registers values */
enum {
    ASU_KV_128BITS = 0,
    ASU_KV_256BITS = 2
};

/* Valid values for EFUSE_KEY_x_BLACK_OR_RED registers */
enum {
    ASU_KV_KEY_BLACK = 1,
    ASU_KV_KEY_RED = 2,
};

/* Magic values for key selection */
enum {
    ASU_KV_MAGIC_EFUSE_0 = 0xef856601,
    ASU_KV_MAGIC_EFUSE_1 = 0xef856602,
    ASU_KV_MAGIC_EFUSE_RED_0 = 0xef858201,
    ASU_KV_MAGIC_EFUSE_RED_1 = 0xef858202,
    ASU_KV_MAGIC_USER_0 = 0xbf858201,
    ASU_KV_MAGIC_USER_1 = 0xbf858202,
    ASU_KV_MAGIC_USER_2 = 0xbf858204,
    ASU_KV_MAGIC_USER_3 = 0xbf858208,
    ASU_KV_MAGIC_USER_4 = 0xbf858210,
    ASU_KV_MAGIC_USER_5 = 0xbf858220,
    ASU_KV_MAGIC_USER_6 = 0xbf858240,
    ASU_KV_MAGIC_USER_7 = 0xbf858280,
    ASU_KV_MAGIC_PUF = 0xdbde8200,
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

static inline size_t get_current_key_size(const XilinxAsuKvState *s)
{
    unsigned int key_size = FIELD_EX32(s->key_size, AES_KEY_SIZE, SELECT);

    switch (key_size) {
    case ASU_KV_128BITS:
        return 16;

    case ASU_KV_256BITS:
        return 32;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_KV ": invalid AES_KEY_SIZE value %u\n",
                      key_size);
        return 0;
    }
}

static inline uint8_t *get_selected_key_storage(const XilinxAsuKvState *s)
{
    uint32_t key_sel = FIELD_EX32(s->key_sel, AES_KEY_SEL, SRC);
    size_t key_idx;

    switch (key_sel) {
    case ASU_KV_MAGIC_EFUSE_RED_0:
        key_idx = XILINX_ASU_KV_EFUSE_0;
        break;

    case ASU_KV_MAGIC_EFUSE_RED_1:
        key_idx = XILINX_ASU_KV_EFUSE_0;
        break;

    case ASU_KV_MAGIC_USER_0:
    case ASU_KV_MAGIC_USER_1:
    case ASU_KV_MAGIC_USER_2:
    case ASU_KV_MAGIC_USER_3:
    case ASU_KV_MAGIC_USER_4:
    case ASU_KV_MAGIC_USER_5:
    case ASU_KV_MAGIC_USER_6:
    case ASU_KV_MAGIC_USER_7:
        key_idx = XILINX_ASU_KV_USER_0 + ctz32(key_sel);
        break;

    case ASU_KV_MAGIC_PUF:
        key_idx = XILINX_ASU_KV_PUF;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_KV ": invalid AES_KEY_SEL value "
                      "%" PRIx32 "\n", key_sel);
        return NULL;
    }

    return (uint8_t *) s->key[key_idx].val;
}

static inline void update_irq(XilinxAsuKvState *s)
{
    qemu_set_irq(s->irq, s->irq_sta && !s->irq_mask);
}

static inline void raise_irq(XilinxAsuKvState *s)
{
    s->irq_sta = true;
    trace_xilinx_asu_kv_raise_irq();
    update_irq(s);
}

static inline void clear_irq(XilinxAsuKvState *s)
{
    s->irq_sta = false;
    update_irq(s);
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

static void do_crc_check(XilinxAsuKvState *s, uint32_t crc)
{
    size_t key_idx = XILINX_ASU_KV_USER_0 + s->crc_key_sel;
    uint32_t ref_crc;
    uint32_t le_key[8];

    g_assert(key_idx <= XILINX_ASU_KV_USER_7);

    s->crc_status = R_AES_USER_KEY_CRC_STATUS_DONE_MASK;

    if (key_is_crc_checked(s, key_idx)) {
        /* only one CRC computation allowed for a given key */
        return;
    }

    key_set_crc_checked(s, key_idx);

    /* the CRC computation function expects a little-endian key */
    for (size_t i = 0; i < ARRAY_SIZE(le_key); i++) {
        le_key[i] = bswap32(s->key[key_idx].val[ARRAY_SIZE(le_key) - (i + 1)]);
    }

    ref_crc = xlnx_efuse_calc_crc(le_key, ARRAY_SIZE(le_key), 0);

    s->crc_status = FIELD_DP32(s->crc_status, AES_USER_KEY_CRC_STATUS,
                               PASS, crc == ref_crc);
}

static uint64_t xilinx_asu_kv_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(opaque);
    uint64_t ret;
    size_t idx;

    switch (addr) {
    case A_AES_KEY_SEL:
        ret = s->key_sel;
        break;

    case A_KEY_ZEROED_STATUS:
        ret = get_key_clear_status(s);
        break;

    case A_AES_USER_SEL_CRC:
        ret = s->crc_key_sel;
        break;

    case A_AES_USER_KEY_CRC_STATUS:
        ret = s->crc_status;
        break;

    case A_AES_KEY_SIZE:
        ret = s->key_size;
        break;

    case A_KEY_LOCK_0 ... A_KEY_LOCK_7:
        idx = XILINX_ASU_KV_USER_0 + (addr - A_KEY_LOCK_0) / sizeof(uint32_t);
        ret = FIELD_DP32(0, KEY_LOCK_0, VALUE, key_is_locked(s, idx));
        break;

    case A_ASU_PMC_KEY_TRANSFER_READY:
        ret = s->asu_pmc_key_xfer_ready;
        break;

    case A_EFUSE_KEY_0_BLACK_OR_RED:
        ret = s->efuse_0_cfg;
        break;

    case A_EFUSE_KEY_1_BLACK_OR_RED:
        ret = s->efuse_1_cfg;
        break;

    case A_KV_INTERRUPT_STATUS:
        ret = s->irq_sta;
        break;

    case A_KV_INTERRUPT_MASK:
        ret = s->irq_mask;
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
    size_t idx;

    trace_xilinx_asu_kv_write(addr, value, size);

    switch (addr) {
    case A_AES_KEY_SEL:
        s->key_sel = value;
        break;

    case A_AES_KEY_CLEAR:
        do_key_clearing(s, value);
        break;

    case A_AES_USER_SEL_CRC:
        s->crc_key_sel = value & AES_USER_SEL_CRC_WRITE_MASK;

        /* writing to this reg clears the done bit in CRC_STATUS */
        s->crc_status = 0;
        break;

    case A_AES_USER_SEL_CRC_VALUE:
        do_crc_check(s, value);
        break;

    case A_KEY_LOCK_0 ... A_KEY_LOCK_7:
        idx = XILINX_ASU_KV_USER_0 + (addr - A_KEY_LOCK_0) / sizeof(uint32_t);

        if (value & R_KEY_LOCK_0_VALUE_MASK) {
            key_set_locked(s, idx);
            trace_xilinx_asu_kv_lock_key(ASU_KV_KEY_STR[idx]);
        }
        break;

    case A_USER_KEY_0_0 ... A_USER_KEY_7_7:
        user_key_write(s, addr, value);
        break;

    case A_AES_KEY_SIZE:
        s->key_size = value;
        break;

    case A_ASU_PMC_KEY_TRANSFER_READY:
        s->asu_pmc_key_xfer_ready =
            FIELD_EX32(value, ASU_PMC_KEY_TRANSFER_READY, VAL);
        pmxc_kt_asu_ready(s->pmxc_aes, s->asu_pmc_key_xfer_ready);
        break;

    case A_EFUSE_KEY_0_BLACK_OR_RED:
        s->efuse_0_cfg = FIELD_EX32(value, EFUSE_KEY_0_BLACK_OR_RED, VAL);
        break;

    case A_EFUSE_KEY_1_BLACK_OR_RED:
        s->efuse_1_cfg = FIELD_EX32(value, EFUSE_KEY_1_BLACK_OR_RED, VAL);
        break;

    case A_KV_INTERRUPT_STATUS:
        if (FIELD_EX32(value, KV_INTERRUPT_STATUS, KT_DONE)) {
            clear_irq(s);
        }
        break;

    case A_KV_INTERRUPT_ENABLE:
        s->irq_mask &= ~(value & R_KV_INTERRUPT_STATUS_KT_DONE_MASK);
        update_irq(s);
        break;

    case A_KV_INTERRUPT_DISABLE:
        s->irq_mask |= value & R_KV_INTERRUPT_STATUS_KT_DONE_MASK;
        update_irq(s);
        break;

    case A_KV_INTERRUPT_TRIGGER:
        if (FIELD_EX32(value, KV_INTERRUPT_STATUS, KT_DONE)) {
            raise_irq(s);
        }
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

static size_t get_key(uint8_t *buf, size_t len,
                      const uint8_t *storage, size_t key_size)
{
    g_assert(len >= key_size);

    if (storage == NULL) {
        /*
         * Invalid key_sel value -> undefined behaviour. Fill the buffer with
         * zeros.
         */
        memset(buf, 0, key_size);
    } else {
        memcpy(buf, storage + 32 - key_size, key_size);
    }

    return key_size;
}

static size_t get_selected_key(XilinxAsuKvState *s, uint8_t *buf, size_t len)
{
    return get_key(buf, len,
                   get_selected_key_storage(s), get_current_key_size(s));
}

static void pmxc_key_xfer_recv_key(PmxcKeyXferIf *kt, uint8_t n, uint8_t *key,
                                   size_t len)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(kt);
    XilinxAsuKvKeyId dest;

    g_assert(len <= sizeof(s->key[0].val));

    switch (n) {
    case 0:
        dest = XILINX_ASU_KV_PUF;
        break;

    case 1:
        switch (s->efuse_0_cfg) {
        case ASU_KV_KEY_BLACK:
            dest = XILINX_ASU_KV_EFUSE_BLACK_0;
            break;

        case ASU_KV_KEY_RED:
            dest = XILINX_ASU_KV_EFUSE_0;
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          TYPE_XILINX_ASU_KV ": invalid "
                          "EFUSE_KEY_0_BLACK_OR_RED register value "
                          "0x%" PRIx32 "\n", s->efuse_0_cfg);
            return;
        }
        break;

    case 2:
        switch (s->efuse_1_cfg) {
        case ASU_KV_KEY_BLACK:
            dest = XILINX_ASU_KV_EFUSE_BLACK_1;
            break;

        case ASU_KV_KEY_RED:
            dest = XILINX_ASU_KV_EFUSE_1;
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          TYPE_XILINX_ASU_KV ": invalid "
                          "EFUSE_KEY_1_BLACK_OR_RED register value "
                          "0x%" PRIx32 "\n", s->efuse_1_cfg);
            return;
        }
        break;

    default:
        g_assert_not_reached();
    }

    key_mark_set(s, dest);
    memcpy(s->key[dest].val, key, len);
    trace_xilinx_asu_kv_write_key(ASU_KV_KEY_STR[dest]);
}

static void pmxc_key_xfer_done(PmxcKeyXferIf *kt, bool done)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(kt);

    raise_irq(s);
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
    s->key_sel = 0;
    s->key_size = ASU_KV_256BITS;
    s->efuse_0_cfg = 0;
    s->efuse_1_cfg = 0;
    s->crc_key_sel = 0;
    s->crc_status = 0;
    s->irq_mask = true;
    s->irq_sta = false;
}

static void xilinx_asu_kv_reset_hold(Object *obj)
{
    XilinxAsuKvState *s = XILINX_ASU_KV(obj);

    update_irq(s);
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
    DEFINE_PROP_LINK("pmxc-aes", XilinxAsuKvState,
                    pmxc_aes, TYPE_PMXC_KEY_XFER_IF,
                    PmxcKeyXferIf *),
    DEFINE_PROP_END_OF_LIST()
};

static void xilinx_asu_kv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    XilinxAsuKvClass *xakc = XILINX_ASU_KV_CLASS(klass);
    PmxcKeyXferIfClass *pktc = PMXC_KEY_XFER_IF_CLASS(klass);

    dc->realize = xilinx_asu_kv_realize;
    rc->phases.enter = xilinx_asu_kv_reset_enter;
    rc->phases.hold = xilinx_asu_kv_reset_hold;
    xakc->get_selected_key = get_selected_key;
    pktc->send_key = pmxc_key_xfer_recv_key;
    pktc->done = pmxc_key_xfer_done;
    device_class_set_props(dc, xilinx_asu_kv_properties);
}

static const TypeInfo xilinx_asu_kv_info = {
    .name = TYPE_XILINX_ASU_KV,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAsuKvState),
    .class_init = xilinx_asu_kv_class_init,
    .class_size = sizeof(XilinxAsuKvClass),
    .interfaces = (InterfaceInfo []) {
        { TYPE_PMXC_KEY_XFER_IF },
        { }
    },
};

static void xilinx_asu_kv_register_types(void)
{
    type_register_static(&xilinx_asu_kv_info);
}

type_init(xilinx_asu_kv_register_types)
