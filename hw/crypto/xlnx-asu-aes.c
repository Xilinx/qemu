/*
 * QEMU model of the Xilinx ASU AES computation engine.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "xlnx-asu-aes-impl.h"

#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/fdt_generic_util.h"
#include "hw/nvram/xlnx-efuse.h"

#ifndef XLNX_ASU_AES_ERR_DEBUG
#define XLNX_ASU_AES_ERR_DEBUG 0
#endif

#ifndef XLNX_ASU_AES_KV_ERR_DEBUG
#define XLNX_ASU_AES_KV_ERR_DEBUG 0
#endif

/*
 * ASU-AES Control MMIO
 */
REG32(AES_STATUS, 0x0)
    FIELD(AES_STATUS, READY, 1, 1)
    FIELD(AES_STATUS, BUSY, 0, 1)
REG32(AES_OPERATION, 0x4)
    FIELD(AES_OPERATION, IV_LOAD, 1, 1)
    FIELD(AES_OPERATION, KEY_LOAD, 0, 1)
REG32(AES_SOFT_RST, 0xc)
    FIELD(AES_SOFT_RST, RESET, 0, 1)
REG32(AES_IV_IN_0, 0x10)
REG32(AES_IV_IN_1, 0x14)
REG32(AES_IV_IN_2, 0x18)
REG32(AES_IV_IN_3, 0x1c)
REG32(AES_IV_MASK_IN_0, 0x20)
REG32(AES_IV_MASK_IN_1, 0x24)
REG32(AES_IV_MASK_IN_2, 0x28)
REG32(AES_IV_MASK_IN_3, 0x2c)
REG32(AES_IV_OUT_0, 0x30)
REG32(AES_IV_OUT_1, 0x34)
REG32(AES_IV_OUT_2, 0x38)
REG32(AES_IV_OUT_3, 0x3c)
REG32(AES_IV_MASK_OUT_0, 0x40)
REG32(AES_IV_MASK_OUT_1, 0x44)
REG32(AES_IV_MASK_OUT_2, 0x48)
REG32(AES_IV_MASK_OUT_3, 0x4c)
REG32(KEY_DEC_TRIG, 0x5c)
    FIELD(KEY_DEC_TRIG, VALUE, 0, 1)
REG32(AES_CM, 0x70)
    FIELD(AES_CM, ENABLE, 0, 3)
REG32(AES_SPLIT_CFG, 0x74)
    FIELD(AES_SPLIT_CFG, KEY_SPLIT, 1, 1)
    FIELD(AES_SPLIT_CFG, DATA_SPLIT, 0, 1)
REG32(AES_MODE_CONFIG, 0x78)
    FIELD(AES_MODE_CONFIG, AUTH, 13, 1)
    FIELD(AES_MODE_CONFIG, ENC_DEC_N, 6, 1)
    FIELD(AES_MODE_CONFIG, ENGINE_MODE, 0, 4)
REG32(AES_MAC_OUT_0, 0x80)
REG32(AES_MAC_OUT_1, 0x84)
REG32(AES_MAC_OUT_2, 0x88)
REG32(AES_MAC_OUT_3, 0x8c)
REG32(AES_MAC_MASK_OUT_0, 0x90)
REG32(AES_MAC_MASK_OUT_1, 0x94)
REG32(AES_MAC_MASK_OUT_2, 0x98)
REG32(AES_MAC_MASK_OUT_3, 0x9c)
REG32(AES_DATA_SWAP, 0x100)
    FIELD(AES_DATA_SWAP, DISABLE, 0, 1)
REG32(AES_INTERRUPT_STATUS, 0x104)
    FIELD(AES_INTERRUPT_STATUS, DONE, 0, 1)
REG32(AES_INTERRUPT_MASK, 0x108)
    FIELD(AES_INTERRUPT_MASK, DONE, 0, 1)
REG32(AES_INTERRUPT_ENABLE, 0x10c)
    FIELD(AES_INTERRUPT_ENABLE, DONE, 0, 1)
REG32(AES_INTERRUPT_DISABLE, 0x110)
    FIELD(AES_INTERRUPT_DISABLE, DONE, 0, 1)
REG32(AES_INTERRUPT_TRIGGER, 0x114)
    FIELD(AES_INTERRUPT_TRIGGER, DONE, 0, 1)

#define ASU_AES_R_MAX (R_AES_INTERRUPT_TRIGGER + 1)

QEMU_BUILD_BUG_ON(ASU_AES_R_MAX != ARRAY_SIZE(((XlnxAsuAes *)0)->regs));

/*
 * ASU-AES Key-Vault MMIO.
 */
REG32(AES_KEY_SEL, 0x0)
REG32(AES_KEY_CLEAR, 0x4)
    FIELD(AES_KEY_CLEAR, AES_KEY_ZEROIZE, 13, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_RED_1, 12, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_RED_0, 11, 1)
    FIELD(AES_KEY_CLEAR, PUF_KEY, 10, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_1, 9, 1)
    FIELD(AES_KEY_CLEAR, EFUSE_KEY_0, 8, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_7, 7, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_6, 6, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_5, 5, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_4, 4, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_3, 3, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_2, 2, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_1, 1, 1)
    FIELD(AES_KEY_CLEAR, USER_KEY_0, 0, 1)
REG32(KEY_ZEROED_STATUS, 0x8)
    FIELD(KEY_ZEROED_STATUS, PUF_KEY, 13, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_RED_KEY_1, 12, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_RED_KEY_0, 11, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_KEY_1, 10, 1)
    FIELD(KEY_ZEROED_STATUS, EFUSE_KEY_0, 9, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_7, 8, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_6, 7, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_5, 6, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_4, 5, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_3, 4, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_2, 3, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_1, 2, 1)
    FIELD(KEY_ZEROED_STATUS, USER_KEY_0, 1, 1)
    FIELD(KEY_ZEROED_STATUS, AES_KEY_ZEROED, 0, 1)
REG32(AES_USER_SEL_CRC, 0x18)
    FIELD(AES_USER_SEL_CRC, VALUE, 0, 3)
REG32(AES_USER_SEL_CRC_VALUE, 0x1c)
REG32(AES_USER_KEY_CRC_STATUS, 0x20)
    FIELD(AES_USER_KEY_CRC_STATUS, DONE, 1, 1)
    FIELD(AES_USER_KEY_CRC_STATUS, PASS, 0, 1)
REG32(KEY_MASK_0, 0x24)
REG32(KEY_MASK_1, 0x28)
REG32(KEY_MASK_2, 0x2c)
REG32(KEY_MASK_3, 0x30)
REG32(KEY_MASK_4, 0x34)
REG32(KEY_MASK_5, 0x38)
REG32(KEY_MASK_6, 0x3c)
REG32(KEY_MASK_7, 0x40)
REG32(KEY_LOCK_0, 0x44)
    FIELD(KEY_LOCK_0, VALUE, 0, 1)
REG32(KEY_LOCK_1, 0x48)
    FIELD(KEY_LOCK_1, VALUE, 0, 1)
REG32(KEY_LOCK_2, 0x4c)
    FIELD(KEY_LOCK_2, VALUE, 0, 1)
REG32(KEY_LOCK_3, 0x50)
    FIELD(KEY_LOCK_3, VALUE, 0, 1)
REG32(KEY_LOCK_4, 0x54)
    FIELD(KEY_LOCK_4, VALUE, 0, 1)
REG32(KEY_LOCK_5, 0x58)
    FIELD(KEY_LOCK_5, VALUE, 0, 1)
REG32(KEY_LOCK_6, 0x5c)
    FIELD(KEY_LOCK_6, VALUE, 0, 1)
REG32(KEY_LOCK_7, 0x60)
    FIELD(KEY_LOCK_7, VALUE, 0, 1)
REG32(USER_KEY_0_0, 0x64)
REG32(USER_KEY_0_1, 0x68)
REG32(USER_KEY_0_2, 0x6c)
REG32(USER_KEY_0_3, 0x70)
REG32(USER_KEY_0_4, 0x74)
REG32(USER_KEY_0_5, 0x78)
REG32(USER_KEY_0_6, 0x7c)
REG32(USER_KEY_0_7, 0x80)
REG32(USER_KEY_1_0, 0x84)
REG32(USER_KEY_1_1, 0x88)
REG32(USER_KEY_1_2, 0x8c)
REG32(USER_KEY_1_3, 0x90)
REG32(USER_KEY_1_4, 0x94)
REG32(USER_KEY_1_5, 0x98)
REG32(USER_KEY_1_6, 0x9c)
REG32(USER_KEY_1_7, 0xa0)
REG32(USER_KEY_2_0, 0xa4)
REG32(USER_KEY_2_1, 0xa8)
REG32(USER_KEY_2_2, 0xac)
REG32(USER_KEY_2_3, 0xb0)
REG32(USER_KEY_2_4, 0xb4)
REG32(USER_KEY_2_5, 0xb8)
REG32(USER_KEY_2_6, 0xbc)
REG32(USER_KEY_2_7, 0xc0)
REG32(USER_KEY_3_0, 0xd4)
REG32(USER_KEY_3_1, 0xd8)
REG32(USER_KEY_3_2, 0xdc)
REG32(USER_KEY_3_3, 0xe0)
REG32(USER_KEY_3_4, 0xe4)
REG32(USER_KEY_3_5, 0xe8)
REG32(USER_KEY_3_6, 0xec)
REG32(USER_KEY_3_7, 0xf0)
REG32(USER_KEY_4_0, 0xf4)
REG32(USER_KEY_4_1, 0xf8)
REG32(USER_KEY_4_2, 0xfc)
REG32(USER_KEY_4_3, 0x100)
REG32(USER_KEY_4_4, 0x104)
REG32(USER_KEY_4_5, 0x108)
REG32(USER_KEY_4_6, 0x10c)
REG32(USER_KEY_4_7, 0x110)
REG32(USER_KEY_5_0, 0x114)
REG32(USER_KEY_5_1, 0x118)
REG32(USER_KEY_5_2, 0x11c)
REG32(USER_KEY_5_3, 0x120)
REG32(USER_KEY_5_4, 0x124)
REG32(USER_KEY_5_5, 0x128)
REG32(USER_KEY_5_6, 0x12c)
REG32(USER_KEY_5_7, 0x130)
REG32(USER_KEY_6_0, 0x134)
REG32(USER_KEY_6_1, 0x138)
REG32(USER_KEY_6_2, 0x13c)
REG32(USER_KEY_6_3, 0x140)
REG32(USER_KEY_6_4, 0x144)
REG32(USER_KEY_6_5, 0x148)
REG32(USER_KEY_6_6, 0x14c)
REG32(USER_KEY_6_7, 0x150)
REG32(USER_KEY_7_0, 0x154)
REG32(USER_KEY_7_1, 0x158)
REG32(USER_KEY_7_2, 0x15c)
REG32(USER_KEY_7_3, 0x160)
REG32(USER_KEY_7_4, 0x164)
REG32(USER_KEY_7_5, 0x168)
REG32(USER_KEY_7_6, 0x16c)
REG32(USER_KEY_7_7, 0x170)
REG32(AES_KEY_SIZE, 0x174)
    FIELD(AES_KEY_SIZE, SELECT, 0, 2)
REG32(AES_KEY_TO_BE_DEC_SIZE, 0x178)
    FIELD(AES_KEY_TO_BE_DEC_SIZE, SELECT, 0, 2)
REG32(AES_KEY_DEC_MODE, 0x17c)
REG32(AES_KEY_TO_BE_DEC_SEL, 0x180)
REG32(ASU_PMC_KEY_TRANSFER_READY, 0x184)
    FIELD(ASU_PMC_KEY_TRANSFER_READY, VAL, 0, 1)
REG32(EFUSE_KEY_0_BLACK_OR_RED, 0x188)
    FIELD(EFUSE_KEY_0_BLACK_OR_RED, VAL, 0, 2)
REG32(EFUSE_KEY_1_BLACK_OR_RED, 0x18c)
    FIELD(EFUSE_KEY_1_BLACK_OR_RED, VAL, 0, 2)
REG32(AES_PL_KEY_SEL, 0x190)
REG32(KV_INTERRUPT_STATUS, 0x194)
    FIELD(KV_INTERRUPT_STATUS, KT_DONE, 0, 1)
REG32(KV_INTERRUPT_MASK, 0x198)
    FIELD(KV_INTERRUPT_MASK, KT_DONE, 0, 1)
REG32(KV_INTERRUPT_ENABLE, 0x19c)
    FIELD(KV_INTERRUPT_ENABLE, KT_DONE, 0, 1)
REG32(KV_INTERRUPT_DISABLE, 0x1a0)
    FIELD(KV_INTERRUPT_DISABLE, KT_DONE, 0, 1)
REG32(KV_INTERRUPT_TRIGGER, 0x1a4)
    FIELD(KV_INTERRUPT_TRIGGER, KT_DONE, 0, 1)

#define ASU_AES_KV_R_MAX (R_KV_INTERRUPT_TRIGGER + 1)

QEMU_BUILD_BUG_ON(ASU_AES_KV_R_MAX != ARRAY_SIZE(((XlnxAsuAes *)0)->kv));

enum {
    /* Offset from base of control MMIO */
    KEY_VAULT_MMIO_OFFSET = 0x2000,

    /* Key-select for encrypt / decrypt */
    KEY_SEL_EFUSE_KEY_RED_0 = 0xef858201,
    KEY_SEL_EFUSE_KEY_RED_1 = 0xef858202,
    KEY_SEL_USER_0 = 0xbf858200,
    KEY_SEL_USER_1 = 0xbf858201,
    KEY_SEL_USER_2 = 0xbf858202,
    KEY_SEL_USER_3 = 0xbf858203,
    KEY_SEL_USER_4 = 0xbf858204,
    KEY_SEL_USER_5 = 0xbf858205,
    KEY_SEL_USER_6 = 0xbf858206,
    KEY_SEL_USER_7 = 0xbf858207,
    KEY_SEL_PUF_KEY = 0xdbde8200,

    /* Source of key decrypt, i.e., only support black eFuse keys */
    KEY_DEC_EFUSE_KEY_0 = 0xef856601,
    KEY_DEC_EFUSE_KEY_1 = 0xef856602,
};

/*
 * Length of a block, an IV, or a MAC, must all be the same for AES
 */
#define SIZEOF_STATE(fld) sizeof_field(XlnxAsuAes, fld)

QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != ASU_AES_IVLEN);
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != ASU_AES_IVLEN);
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != ASU_AES_MACLEN);
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != ASU_AES_BLKLEN);
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != SIZEOF_STATE(partial));
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != SIZEOF_STATE(cipher.be_iv_in));
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != SIZEOF_STATE(cipher.be_iv_out));
QEMU_BUILD_BUG_ON(ASU_AES_U8_128 != SIZEOF_STATE(cipher.be_mac_out));

QEMU_BUILD_BUG_ON(ASU_AES_U8_256 != SIZEOF_STATE(cipher.be_key_in));
QEMU_BUILD_BUG_ON(ASU_AES_U8_256 != SIZEOF_STATE(efuse_ukey0_black));
QEMU_BUILD_BUG_ON(ASU_AES_U8_256 != SIZEOF_STATE(efuse_ukey1_black));
QEMU_BUILD_BUG_ON(ASU_AES_U8_256 != SIZEOF_STATE(efuse_ukey0_red));
QEMU_BUILD_BUG_ON(ASU_AES_U8_256 != SIZEOF_STATE(efuse_ukey1_red));
QEMU_BUILD_BUG_ON(ASU_AES_U8_256 != SIZEOF_STATE(puf_key));

/*
 * Dynamic cipher binding to allow QOM instantiation, e.g., from a Versal
 * machine, even when a crypto-lib is not (or cannot be) configured into
 * the build.
 */
static bool asu_aes_noop_cipher(XlnxAsuAes *s, unsigned op, size_t len,
                                const void *din, void *dout)
{
    static unsigned log_max = 5;

    if (op == ASU_AES_RESET) {
        goto done;
    }

    if (log_max) {
        log_max--;
        warn_report("QOM Class " TYPE_XLNX_ASU_AES ": "
                    "Controller does not have installed cipher");
    }

    switch (op) {
    case ASU_AES_INIT:
        goto done;
    case ASU_AES_TEXT:
        ASU_AES_NZERO(dout, len);
        if (s->cipher.fin_phase) {
            break;
        }
        goto done;
    case ASU_AES_AEAD:
        if (!asu_aes_no_aad(s)) {
            break;
        }
        __attribute__((fallthrough));
    default:
        error_setg(&error_abort, "Bug: Unsupported op %u in mode %u",
                   op, s->cipher.mode);
        return true;
    }

    switch (s->cipher.mode) {
    case ASU_AES_MODE_CMAC:
    case ASU_AES_MODE_CCM:
    case ASU_AES_MODE_GCM:
        ASU_AES_MZERO(s->cipher.be_mac_out);
        s->cipher.mac_valid = true;
    }

 done:
    return false;
}

static xlnx_asu_aes_cipher_t asu_aes_cipher = asu_aes_noop_cipher;

void xlnx_asu_aes_cipher_bind(xlnx_asu_aes_cipher_t cipher)
{
    ASU_AES_BUG(cipher == NULL);

    if (asu_aes_cipher && asu_aes_cipher != asu_aes_noop_cipher) {
        warn_report("QOM Class " TYPE_XLNX_ASU_AES ": "
                    "Cipher %p binding ignored - already installed with %p",
                    cipher, asu_aes_cipher);
    } else {
        asu_aes_cipher = cipher;
    }
}

/*
 * Real hardware registers keep multi-word data as little-endian,
 * i.e., smaller address offset is less significant.
 */
static void asu_aes_reg_to_be(const uint32_t *rp, void *be, unsigned n)
{
    unsigned i8, i32;

    ASU_AES_BUG((n % 4) != 0);

    for (i32 = n / 4, i8 = 0; i8 < n; i8 += 4) {
        stl_be_p(be + i8, rp[--i32]);
    }
}

static void asu_aes_reg_from_be(uint32_t *rp, const void *be, unsigned n)
{
    unsigned i8, i32;

    ASU_AES_BUG((n % 4) != 0);

    for (i32 = n / 4, i8 = 0; i8 < n; i8 += 4) {
        rp[--i32] = ldl_be_p(be + i8);
    }
}

static void asu_aes_set_busy(XlnxAsuAes *s, bool on)
{
    ARRAY_FIELD_DP32(s->regs, AES_STATUS, BUSY, !!on);
}

static void asu_aes_set_ready(XlnxAsuAes *s, bool on)
{
    ARRAY_FIELD_DP32(s->regs, AES_STATUS, READY, !!on);
}

static bool asu_aes_aad_mode(XlnxAsuAes *s)
{
    /* AUTH bit is recognized only in selected mode(s). */
    if (asu_aes_no_aad(s)) {
        return false;
    } else {
        return !!ARRAY_FIELD_EX32(s->regs, AES_MODE_CONFIG, AUTH);
    }
}

static bool asu_aes_cmac_mode(XlnxAsuAes *s)
{
    uint32_t mode;

    mode = ARRAY_FIELD_EX32(s->regs, AES_MODE_CONFIG, ENGINE_MODE);
    return mode == ASU_AES_MODE_CMAC;
}

static void asu_aes_update_irq(XlnxAsuAes *s)
{
    uint32_t isr = s->regs[R_AES_INTERRUPT_STATUS];
    uint32_t mask = s->regs[R_AES_INTERRUPT_MASK];
    bool pending = isr & ~mask;

    qemu_set_irq(s->irq_aes_interrupt, pending);
}

static void asu_aes_irq_set_done(XlnxAsuAes *s)
{
    asu_aes_set_busy(s, false);
    asu_aes_set_ready(s, false);

    ARRAY_FIELD_DP32(s->regs, AES_INTERRUPT_STATUS, DONE, 1);
    asu_aes_update_irq(s);
}

static void asu_aes_irq_status_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    asu_aes_update_irq(s);
}

static uint64_t asu_aes_irq_enable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t val = val64;

    s->regs[R_AES_INTERRUPT_MASK] &= ~val;
    asu_aes_update_irq(s);
    return 0;
}

static uint64_t asu_aes_irq_disable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t val = val64;

    s->regs[R_AES_INTERRUPT_MASK] |= val;
    asu_aes_update_irq(s);
    return 0;
}

static uint64_t asu_aes_irq_trigger_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t val = val64;

    s->regs[R_AES_INTERRUPT_STATUS] |= val;
    asu_aes_update_irq(s);
    return 0;
}

static void asu_aes_update_kv_irq(XlnxAsuAes *s)
{
    uint32_t isr = s->kv[R_KV_INTERRUPT_STATUS];
    uint32_t mask = s->kv[R_KV_INTERRUPT_MASK];
    bool pending = isr & ~mask;

    qemu_set_irq(s->irq_kv_interrupt, pending);
}

static void asu_aes_kv_irq_status_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    asu_aes_update_kv_irq(s);
}

static uint64_t asu_aes_kv_irq_enable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t val = val64;

    s->kv[R_KV_INTERRUPT_MASK] &= ~val;
    asu_aes_update_kv_irq(s);
    return 0;
}

static uint64_t asu_aes_kv_irq_disable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t val = val64;

    s->kv[R_KV_INTERRUPT_MASK] |= val;
    asu_aes_update_kv_irq(s);
    return 0;
}

static uint64_t asu_aes_kv_irq_trigger_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t val = val64;

    s->kv[R_KV_INTERRUPT_STATUS] |= val;
    asu_aes_update_kv_irq(s);

    return 0;
}

static bool asu_aes_key_decrypt_mode(XlnxAsuAes *s)
{
    return s->kv[R_AES_KEY_DEC_MODE] == 0xffffffff;
}

static void asu_aes_kt_set_done(XlnxAsuAes *s)
{
    ARRAY_FIELD_DP32(s->kv, KV_INTERRUPT_STATUS, KT_DONE, 1);
    asu_aes_update_kv_irq(s);
}

static void *asu_aes_kt_efuse_ukey(XlnxAsuAes *s, unsigned nr, void *alt)
{
    uint32_t nr_sel, sel;

    static const struct {
        unsigned nr_sel;
        size_t black;
        size_t red;
    } efuse_uk_map[] = {
        [0] = {
            .nr_sel = R_EFUSE_KEY_0_BLACK_OR_RED,
            .black = offsetof(XlnxAsuAes, efuse_ukey0_black),
            .red = offsetof(XlnxAsuAes, efuse_ukey0_red),
        },
        [1] = {
            .nr_sel = R_EFUSE_KEY_1_BLACK_OR_RED,
            .black = offsetof(XlnxAsuAes, efuse_ukey1_black),
            .red = offsetof(XlnxAsuAes, efuse_ukey1_red),
        },
    };

    assert(nr < ARRAY_SIZE(efuse_uk_map));

    nr_sel = efuse_uk_map[nr].nr_sel;
    sel = s->kv[nr_sel];

    switch (sel) {
    case 1:
        return (void *)s + efuse_uk_map[nr].black;
    case 2:
        return (void *)s + efuse_uk_map[nr].red;
    default:
        ASU_AES_GUEST_ERROR(s, "Invalid value (0x%02x) "
                            "in EFUSE_KEY_%u_BLACK_OR_RED register",
                            sel, nr);
        return alt;
    }
}

static void asu_aes_kt_simulated(XlnxAsuAes *s, void *kp[])
{
    unsigned nk, nr;
    void *p;

    /*
     * Simulate transfer for unit-test backdoor as follows:
     *  EFUSE_0 <= USER_0, EFUSE_1 <= USER_1, PUF <= USER_2
     */
    for (nk = 0; nk < 3; nk++) {
        for (p = kp[nk], nr = 0; nr < 8; nr++, p += 4) {
            stl_be_p(p, s->kv[R_USER_KEY_0_0 + nr]);
        }
    }

    asu_aes_kt_set_done(s);
}

static void asu_aes_kt_launch(XlnxAsuAes *s)
{
    uint8_t discarded[ASU_AES_U8_256];
    void *kp[3];

    kp[0] = asu_aes_kt_efuse_ukey(s, 0, discarded);
    kp[1] = asu_aes_kt_efuse_ukey(s, 1, discarded);
    kp[2] = s->puf_key;

    if (s->kv_qtest) {
        asu_aes_kt_simulated(s, kp);
        return;
    }

    /* TODO: Signal KT-done only after key materials have been obtained */
    ASU_AES_KZERO(kp[0]);
    ASU_AES_KZERO(kp[1]);
    ASU_AES_KZERO(kp[2]);
    pmxc_kt_asu_ready(s->pmxc_aes, 1);
}

static void asu_aes_int_pmxc_kt_done(pmxcKT *kt, bool done)
{
    XlnxAsuAes *s = XLNX_ASU_AES(kt);

    ARRAY_FIELD_DP32(s->kv, KV_INTERRUPT_STATUS, KT_DONE, done);
    asu_aes_update_kv_irq(s);
}

static void asu_aes_int_receive_key(pmxcKT *kt, uint8_t n, uint8_t *key,
                                    size_t len)
{
    XlnxAsuAes *s = XLNX_ASU_AES(kt);

    switch (n) {
    case 0:
        ASU_AES_KCOPY(s->puf_key, key);
        break;
    case 1:
        ASU_AES_KCOPY(asu_aes_kt_efuse_ukey(s, 0, NULL), key);
        break;
    case 2:
        ASU_AES_KCOPY(asu_aes_kt_efuse_ukey(s, 1, NULL), key);
        break;
    default:
        g_assert_not_reached();
    };
}

static void asu_aes_load_le_key(XlnxAsuAes *s, const uint32_t *reg)
{
    ASU_AES_KZERO(s->cipher.be_key_in);
    asu_aes_reg_to_be(reg, asu_aes_key_in(s), asu_aes_key_in_len(s));
}

static void asu_aes_load_be_key(XlnxAsuAes *s, const void *ksrc)
{
    ASU_AES_KCOPY(s->cipher.be_key_in, ksrc);

    if (asu_aes_k128(s->cipher.be_key_in)) {
        memset(s->cipher.be_key_in, 0, ASU_AES_U8_128);
    }
}

static void asu_aes_load_key(XlnxAsuAes *s, unsigned key_sel)
{
    unsigned sr;
    int klen;

    klen = ARRAY_FIELD_EX32(s->kv, AES_KEY_SIZE, SELECT);
    klen = asu_aes_set_klen(s->cipher.be_key_in, klen);
    if (klen < 0) {
        ASU_AES_GUEST_ERROR(s, "Invalid AES key size-code %u", -klen);
        return;
    }

    switch (key_sel) {
    case KEY_SEL_USER_0:
        sr = R_USER_KEY_0_0;
        break;
    case KEY_SEL_USER_1:
        sr = R_USER_KEY_1_0;
        break;
    case KEY_SEL_USER_2:
        sr = R_USER_KEY_2_0;
        break;
    case KEY_SEL_USER_3:
        sr = R_USER_KEY_3_0;
        break;
    case KEY_SEL_USER_4:
        sr = R_USER_KEY_4_0;
        break;
    case KEY_SEL_USER_5:
        sr = R_USER_KEY_5_0;
        break;
    case KEY_SEL_USER_6:
        sr = R_USER_KEY_6_0;
        break;
    case KEY_SEL_USER_7:
        sr = R_USER_KEY_7_0;
        break;
    case KEY_SEL_PUF_KEY:
        asu_aes_load_be_key(s, s->puf_key);
        return;
    case KEY_SEL_EFUSE_KEY_RED_0:
        asu_aes_load_be_key(s, s->efuse_ukey0_red);
        return;
    case KEY_SEL_EFUSE_KEY_RED_1:
        asu_aes_load_be_key(s, s->efuse_ukey1_red);
        return;
    default:
        ASU_AES_KZERO(s->cipher.be_key_in);
        return;
    }

    asu_aes_load_le_key(s, &s->kv[sr]);
}

static void asu_aes_load_iv(XlnxAsuAes *s)
{
    asu_aes_reg_to_be(&s->regs[R_AES_IV_IN_0],
                      s->cipher.be_iv_in, ASU_AES_IVLEN);
}

static void asu_aes_load_outregs(XlnxAsuAes *s)
{
    void *iv = &s->regs[R_AES_IV_OUT_0];
    void *mac = &s->regs[R_AES_MAC_OUT_0];

    if (s->cipher.in_error) {
        ASU_AES_IZERO(iv);
        ASU_AES_MZERO(mac);
    } else {
        if (s->cipher.mac_valid) {
            asu_aes_reg_from_be(mac, s->cipher.be_mac_out, ASU_AES_MACLEN);
        }

        asu_aes_reg_from_be(iv, s->cipher.be_iv_out, ASU_AES_IVLEN);
    }
}

static void asu_aes_clear_partial(XlnxAsuAes *s)
{
    ASU_AES_BZERO(s->partial);
    s->partial_bcnt = 0;
}

static void asu_aes_cipher_reset(XlnxAsuAes *s)
{
    asu_aes_cipher(s, ASU_AES_RESET, 0, NULL, NULL);

    ASU_AES_PZERO(&s->cipher);
    s->cipher.fin_phase = true;

    ASU_AES_NZERO(&s->regs[R_AES_IV_OUT_0], ASU_AES_IVLEN);
    ASU_AES_NZERO(&s->regs[R_AES_MAC_OUT_0], ASU_AES_MACLEN);
    s->regs[R_AES_STATUS] = R_AES_STATUS_READY_MASK;

    asu_aes_clear_partial(s);
}

static bool asu_aes_cipher_init(XlnxAsuAes *s)
{
    bool kdm = asu_aes_key_decrypt_mode(s);

    ASU_AES_MZERO(s->cipher.be_mac_out);
    s->cipher.aad_bcnt = 0;
    s->cipher.txt_bcnt = 0;
    s->cipher.aad_used = 0;
    s->cipher.txt_used = 0;
    s->cipher.aad_bmax = UINT64_MAX;
    s->cipher.txt_bmax = UINT64_MAX;
    s->cipher.flags = 0;

    if (kdm) {
        s->cipher.enc = false;
    } else {
        s->cipher.enc = !!ARRAY_FIELD_EX32(s->regs, AES_MODE_CONFIG, ENC_DEC_N);
    }

    s->cipher.mode = ARRAY_FIELD_EX32(s->regs, AES_MODE_CONFIG, ENGINE_MODE);
    switch (s->cipher.mode) {
    case ASU_AES_MODE_ECB:
    case ASU_AES_MODE_CBC:
    case ASU_AES_MODE_CFB:
    case ASU_AES_MODE_OFB:
    case ASU_AES_MODE_CTR:
    case ASU_AES_MODE_GCM:
        break;
    case ASU_AES_MODE_CCM:
        /*
         * Key-decrypt does not allow CCM, because ASU AES CCM engine
         * requires a nonce be placed in 2 places:
         * 1. Loaded into IV, formatted as CTR0 (see SP800-38C).
         * 2. Embedded in B0 that is sent in AAD-phase.
         *
         * While #1 is trivial, #2 is not available for key-decrypt.
         */
        __attribute__((fallthrough));
    case ASU_AES_MODE_CMAC:
        if (!kdm) {
            break;
        }
        /* CMAC is not for confidentiality, thus invalid for key decrypt */
        __attribute__((fallthrough));
    default:
        ASU_AES_GUEST_ERROR(s, "Invalid AES engine mode %d%s",
                            s->cipher.mode, (kdm ? " for key decrypt" : ""));

        s->cipher.in_error = true;
        return true;
    }

    return asu_aes_cipher(s, ASU_AES_INIT, 0, NULL, NULL);
}

static void asu_aes_cipher_aead(XlnxAsuAes *s, bool last,
                                size_t bcnt, const void *aead)
{
    switch (s->cipher.mode) {
    case ASU_AES_MODE_CCM:
    case ASU_AES_MODE_GCM:
        break;
    default:
        /* Reaching here is a BUG */
        error_setg(&error_abort, "Wrong cipher mode %u", s->cipher.mode);
        return;
    }

    s->cipher.fin_phase = last;
    asu_aes_cipher(s, ASU_AES_AEAD, bcnt, aead, NULL);

    s->cipher.aad_bcnt += bcnt;
}

static void asu_aes_cipher_text(XlnxAsuAes *s, bool last, size_t bcnt,
                                const void *in, void *out)
{
    /*
     * Unless in CMAC operation, DONE-irq is NOT raised here; instead,
     * it is raised only after last output has been drained.
     */
    ASU_AES_BUG(!(last || asu_aes_is_blk(bcnt)));

    s->cipher.fin_phase = last;

    if (!s->cipher.in_error) {
        asu_aes_cipher(s, ASU_AES_TEXT, bcnt, in, out);
    }

    if (s->cipher.in_error && out) {
        ASU_AES_NZERO(out, bcnt);
    }

    s->cipher.txt_bcnt += bcnt;
}

static void *asu_aes_cipher_data(XlnxAsuAes *s, bool last, size_t bcnt,
                                 const void *din, void *dout)
{
    uint8_t zpad_in[ASU_AES_BLKLEN];
    const void *pb;
    size_t p_bcnt, w_bcnt;

    p_bcnt = bcnt % ASU_AES_BLKLEN;
    w_bcnt = bcnt - p_bcnt;

    /* Whole block(s) are straightfoward */
    if (w_bcnt) {
        bool w_last = p_bcnt ? false : last;

        if (s->cipher.aad_phase) {
            asu_aes_cipher_aead(s, w_last, w_bcnt, din);
        } else {
            asu_aes_cipher_text(s, w_last, w_bcnt, din, dout);
        }
    }

    if (!p_bcnt) {
        goto done;
    }

    /* Ensure the input buffer is a block padded with 0s */
    if (din == s->partial) {
        ASU_AES_BUG(w_bcnt);
        pb = din;
    } else {
        ASU_AES_BZERO(zpad_in);
        memcpy(zpad_in, din + w_bcnt, p_bcnt);
        pb = zpad_in;
    }

    if (s->cipher.aad_phase) {
        asu_aes_cipher_aead(s, last, p_bcnt, pb);
        dout = NULL;
    } else {
        uint8_t padded_out[ASU_AES_BLKLEN] = { 0 };

        /* Partial text must be last */
        ASU_AES_BUG(!last);

        /*
         * Partial text should never be padded for:
         * -- CMAC and GCM
         *    The amount needs to be precise for correct MAC.
         * -- CCM
         *    The amount needs to match precisely that stored in AAD.B0.Q
         *
         * But, use a whole-block working out-buf anyway to guarantee
         * that the "actual" size is 128 bits.
         */
        switch (s->cipher.mode) {
        case ASU_AES_MODE_CMAC:
            asu_aes_cipher_text(s, true,  p_bcnt, pb, NULL);
            dout = NULL;
            goto done;
        case ASU_AES_MODE_CCM:
        case ASU_AES_MODE_GCM:
            asu_aes_cipher_text(s, true,  p_bcnt, pb, padded_out);
            break;
        default:
            /*
             * **** TBD: PENDING FOR SECURITY REVIEW ********************
             * The current real hardware, in modes other than CMAC/GCM,
             * partial text blocks are padded with zero.
             *
             * This could change after security-review, because NIST
             * recommends the padding be a single leading '1' bit
             * followed by all '0' bit(s).
             * **********************************************************
             */
            asu_aes_cipher_text(s, true,  ASU_AES_BLKLEN, pb, padded_out);
        }

        /* Copy out only the amount 'dout' can hold */
        memcpy(dout + w_bcnt, padded_out, p_bcnt);
    }

 done:
    asu_aes_load_outregs(s);

    if (last && !dout) {
        asu_aes_irq_set_done(s);  /* Since no output produced */
    }

    return dout ? dout + bcnt : NULL;
}

static void asu_aes_out_pushing(void *opaque)
{
    XlnxAsuAes *s = XLNX_ASU_AES(opaque);
    void *buf = s->out.buf;
    size_t bcnt = s->out.bcnt;
    size_t next = s->out.next;
    bool   last = s->out.last;

    if (!s->out.dev || !bcnt || !buf) {
        goto done; /* Just drop the output */
    }

    while (next < s->out.bcnt) {
        if (!stream_can_push(s->out.dev, asu_aes_out_pushing, s)) {
            s->out.next = next;

            /* With undrained data, not ready to accept more data */
            asu_aes_set_ready(s, false);
            return;
        }

        next += stream_push(s->out.dev, buf + next, (bcnt - next), last);
    }

 done:
    /* Clear the output context, except .dev */
    g_free(s->out.buf);
    s->out.buf  = NULL;
    s->out.bcnt = 0;
    s->out.next = 0;
    s->out.last = false;

    if (last) {
        asu_aes_irq_set_done(s);
    } else {
        /* With all output drained and not done, ready to accept more data */
        asu_aes_set_ready(s, true);

        if (s->inp.notify) {
            s->inp.notify(s->inp.notify_opaque);
        }
        ASU_AES_PZERO(&s->inp);
    }
}

static void asu_aes_out_push(XlnxAsuAes *s, bool last, size_t bcnt, void *dout)
{
    if (!dout) {
        return;  /* skip CMAC */
    }

    ASU_AES_BUG(s->out.buf != NULL);

    s->out.buf = dout;
    s->out.bcnt = bcnt;
    s->out.next = 0;
    s->out.last = last;

    asu_aes_out_pushing(s);
}

static bool asu_aes_has_output(XlnxAsuAes *s)
{
    /* Test !aad_phase instead of txt_phase in case of funny GCM last aad */
    return !s->cipher.aad_phase && !asu_aes_cmac_mode(s);
}

static void *asu_aes_flush_partial(XlnxAsuAes *s, bool last, void *dout)
{
    bool has_out;
    size_t p_bcnt = s->partial_bcnt;

    if (!p_bcnt) {
        return dout;
    }

    has_out = asu_aes_has_output(s);
    if (!dout && has_out) {
        void *obuf = g_malloc(p_bcnt);

        asu_aes_cipher_data(s, last, p_bcnt, s->partial, obuf);
        asu_aes_out_push(s, last, p_bcnt, obuf);
    } else {
        asu_aes_cipher_data(s, last, p_bcnt, s->partial, dout);
        if (dout && has_out) {
            dout += p_bcnt;
        }
    }

    asu_aes_clear_partial(s);
    return dout;
}

static size_t asu_aes_stream_sink(StreamSink *obj, uint8_t *din,
                                  size_t in_total, bool eop)
{
    XlnxAsuAes *s = XLNX_ASU_AES(obj);
    void *din_end = din + in_total;
    size_t bcnt, ib_bcnt, ob_bcnt;
    bool is_aad;

    /*
     * First arrival after FIN state is the GO of a new session,
     * i.e., there is no explicit GO command in the controller.
     *
     * Init error, if any, will be handled by discarding incoming
     * data until EOP (end of push).
     */
    if (s->cipher.fin_phase) {
        asu_aes_cipher_init(s);
    }

    /* Any input raises BUSY indicator */
    asu_aes_set_busy(s, true);

    /*
     * In text phase, will not process any incoming data
     * if there is pending output.
     */
    if (s->cipher.txt_phase && s->out.buf) {
        return 0;
    }

    is_aad = asu_aes_aad_mode(s);

    /*
     * ASU AES recognizes AAD phase only in CCM and GCM modes.
     *
     * In CCM mode (SP800-38C), the ASU AES engine expects:
     * -- IV be CTR0 (see A.2)
     * -- B0 (A.2) sent as AAD, even if there is no "AAD" in the
     *    conventional sense (i.e., in the context of mainstream
     *    crypto libs).
     * -- Both AAD and text are multiples of 128 bits, with EOP
     *    to indicate partial block in each phase, resulting in
     *    the last block of a phase padded with 0.
     * -- MAC_OUT is always valid after ciopher has processed
     *    each collection of blocks; thus, EOP is optional in
     *    either phase, if the phase does NOT send a partial block.
     *
     * ASU AES GCM mode (SP800-38D) is quite different from mainstream
     * crypto libs:
     * -- IV be J0 (see step 2 of 7.2 and step 3 of 7.3).
     * -- Partial AAD (aad in the conventional sense of mainstream
     *    crypto libs) and text must be indicated with EOP in their
     *    respective phase; otherwise, EOP is optional.
     * -- To obtain the GCM tag after all AAD and text are sent to
     *    ASU AES, software is required to send a single 128-bit
     *    block, {uint64(aad_len), uint64(text_len)}, with AUTH
     *    being '1'.
     *
     * --------------------
     * Implementation Notes
     * --------------------
     * Before EOP, partial block is collected here, but the actual
     * padding, triggered by EOP, is handled in asu_aes_cipher_data()
     * above.
     *
     * For mode(s) / phase(s) that cipher does not produce output,
     * DONE-irq (which also clears BUSY indicator) should be triggered
     * after the EOP-indicated byte has been processed by the cipher.
     *
     * Otherwise, DONE-irq cannot be raised until output from the cipher
     * for the EOP-indicated byte has been drained by DMA to destination.
     *
     * After cipher has entered the non-AAD phase, it is invalid going
     * back to the AAD phase.  This shall be caught by the cipher, in
     * order for it to cleanly discard the session in error.
     */
    if (!is_aad) {
        if (s->cipher.aad_phase) {
            asu_aes_flush_partial(s, false, NULL); /* aad residual */
        }

        s->cipher.aad_phase = false;
        s->cipher.txt_phase = true;
    } else if (!s->cipher.txt_phase) {
        /* Enter initial AAD phase */
        s->cipher.aad_phase = true;
    } else if (s->cipher.mode == ASU_AES_MODE_GCM) {
        /*
         * Return to AAD phase from text phase.  This is allowed only in GCM
         * to receive the funny block of <uint64(aad_len), uint64(text_len)>
         * block to trigger output of GCM tag.
         */
        if (!s->cipher.aad_phase) {
            asu_aes_flush_partial(s, true, NULL); /* text residual */
        }

        /* Use both phases being true to indicate this special case */
        s->cipher.aad_phase = true;
        s->cipher.txt_phase = true;
    } else {
        /* Ignore the return to AAD by staying with text phase */
        ASU_AES_GUEST_ERROR(s, "TXT => AAD ignored: "
                            "aad_len = %llu, txt_len = %llu",
                            (unsigned long long)s->cipher.aad_bcnt,
                            (unsigned long long)s->cipher.txt_bcnt);
    }

    /*
     * Always send data to cipher as blocks until EOP, with partial
     * collected in s->partial for next round of incoming data.
     */
    bcnt = in_total;
    if (s->partial_bcnt) {
        size_t room = ASU_AES_BLKLEN - s->partial_bcnt;

        if (room > bcnt) {
            memcpy(&(s->partial[s->partial_bcnt]), din, bcnt);
            s->partial_bcnt += bcnt;
            din += bcnt;
            bcnt = 0;

            if (!eop) {
                goto done; /* The incoming data did not fill the partial */
            }
        } else {
            memcpy(&(s->partial[s->partial_bcnt]), din, room);
            s->partial_bcnt += room;
            din += room;
            bcnt -= room;
        }

        /* The partial should contain last byte, be full, or both */
        ASU_AES_BUG(!eop && s->partial_bcnt != ASU_AES_BLKLEN);
    }

    /*
     * Obtain an output buffer large enough for unconsumed block(s),
     * which is possibly proceded by the fully accumulated partial.
     *
     * If incoming data contains last byte, all will be passed through
     * the cipher.
     *
     * At this point, 'bcnt' indicates amount of data remaining in 'din'.
     */
    if (eop) {
        ib_bcnt = bcnt;
    } else {
        /* Partial buffer should be empty or full */
        ASU_AES_BUG(!asu_aes_is_blk(s->partial_bcnt));

        /* Get full blocks from din */
        ib_bcnt = ROUND_DOWN(bcnt, ASU_AES_BLKLEN);
    }

    ob_bcnt = s->partial_bcnt + ib_bcnt;
    if (ob_bcnt) {
        void *obuf = asu_aes_has_output(s) ? g_malloc(ob_bcnt) : NULL;
        void *dout = obuf;

        if (s->partial_bcnt) {
            /*
             * The partial buffer cannot contain the last byte
             * if there is still at least a byte remaining in 'din'.
             */
            bool last = bcnt ? false : eop;

            if (!last) {
                ASU_AES_BUG(s->partial_bcnt != ASU_AES_BLKLEN);
            }

            dout = asu_aes_flush_partial(s, last, dout);
        }

        if (ib_bcnt) {
            dout = asu_aes_cipher_data(s, eop, ib_bcnt, din, dout);
        }

        /*
         * Push output to its destination.  Further input is suspended
         * until this output has been drained.
         */
        asu_aes_out_push(s, eop, ob_bcnt, obuf);
    }

    /*
     * The left-over, if any, will be placed in partial buffer
     * for being combined with future incoming data.
     *
     * If there is left-over:
     * 1. The left-over cannot contain last byte, and
     * 2. The partial buffer should be empty prior to be filled.
     */
    din += ib_bcnt;
    bcnt -= ib_bcnt;
    if (bcnt) {
        ASU_AES_BUG(eop);
        ASU_AES_BUG(s->partial_bcnt != 0);
        ASU_AES_BUG(bcnt >= ASU_AES_BLKLEN);

        memcpy(s->partial, din, bcnt);
        s->partial_bcnt = bcnt;
        din += bcnt;
    }
    ASU_AES_BUG(din != din_end);

 done:
    return in_total;
}

static bool asu_aes_stream_sink_ready(StreamSink *obj,
                                      StreamCanPushNotifyFn notify,
                                      void *notify_opaque)
{
    XlnxAsuAes *s = XLNX_ASU_AES(obj);
    bool ready = true;

    /* Without an output receiver, always ready for input: output discarded */
    if (!s->out.dev) {
        goto done;
    }

    /* While not in text phase, already ready for input */
    if (!s->cipher.txt_phase) {
        goto done;
    }

    /* With undrained output, additional input cannot be processed */
    if (s->out.buf) {
        s->inp.notify = notify;
        s->inp.notify_opaque = notify_opaque;
        ready = false;
    }

 done:
    asu_aes_set_ready(s, ready);
    return ready;
}

static void asu_aes_decrypt_black_key(XlnxAsuAes *s)
{
    const void *black;
    void *red;
    int kd_len, kd_sel;

    if (!asu_aes_key_decrypt_mode(s)) {
        ASU_AES_GUEST_ERROR(s, "Controller not in key decrypt mode: 0x%02x",
                            s->kv[R_AES_KEY_DEC_MODE]);
        return;
    }

    kd_sel = s->kv[R_AES_KEY_TO_BE_DEC_SEL];
    switch (kd_sel) {
    case KEY_DEC_EFUSE_KEY_0:
        black = s->efuse_ukey0_black;
        red = s->efuse_ukey0_red;
        break;
    case KEY_DEC_EFUSE_KEY_1:
        black = s->efuse_ukey1_black;
        red = s->efuse_ukey1_red;
        break;
    default:
        ASU_AES_GUEST_ERROR(s, "Invalid source of key to be decrypted: %u",
                            kd_sel);
        return;
    }

    kd_len = ARRAY_FIELD_EX32(s->kv, AES_KEY_TO_BE_DEC_SIZE, SELECT);
    kd_len = asu_aes_set_klen(NULL, kd_len);
    if (kd_len < 0) {
        ASU_AES_GUEST_ERROR(s, "Invalid size-code for key decrypt: %d",
                            -kd_len);
        return;
    }

    if (!asu_aes_cipher_init(s)) {
        black += ASU_AES_U8_256 - kd_len;
        red += ASU_AES_U8_256 - kd_len;

        s->cipher.fin_phase = true;
        asu_aes_cipher(s, ASU_AES_TEXT, kd_len, black, red);
    }

    if (s->cipher.in_error) {
        ASU_AES_KZERO(red);
    }

}

static uint64_t asu_aes_operation_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    if (FIELD_EX32(val64, AES_OPERATION, IV_LOAD)) {
        asu_aes_load_iv(s);
    }

    if (FIELD_EX32(val64, AES_OPERATION, KEY_LOAD)) {
        asu_aes_load_key(s, s->kv[R_AES_KEY_SEL]);
    }

    return 0;  /* Self-clear */
}

static void asu_aes_soft_rst_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    if (!val64) {
        return;
    }

    asu_aes_cipher_reset(s);
    s->regs[R_AES_STATUS] = 0;
}

static void asu_aes_mode_config_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    /*
     * The use-case for AES_STATUS.BUSY==0 is mostly for guest to know
     * if device has fully consumed the AUTH data.  In real hardware,
     * BUSY => 1 as soon as the device receives the 1st byte of AUTH.
     * Unfortunately, the ASU DMA model may introduce delays. As a
     * result, if this device sets BUSY like real hardware, guest may
     * fail to observe BUSY => 1, and the wait-for-auth-consumed may
     * malfunction.
     *
     * This device implements a workaround by setting BUSY when
     * MODE_CONFIG.AUTH is set to 1.
     */
    if (FIELD_EX32(val64, AES_MODE_CONFIG, AUTH)) {
        ARRAY_FIELD_DP32(s->regs, AES_STATUS, BUSY, 1);
    }
}

static void asu_aes_key_dec_trig_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    if (FIELD_EX32(val64, KEY_DEC_TRIG, VALUE)) {
        asu_aes_decrypt_black_key(s);
        asu_aes_irq_set_done(s);
    }
}

static uint64_t asu_aes_key_clear_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    unsigned i;

    static const struct {
        uint32_t cm;
        unsigned ofs;
    } kc_map[] = {
    #define KC_MAP(m, f) { .cm = R_AES_KEY_CLEAR_ ## m ## _MASK, \
                           .ofs = offsetof(XlnxAsuAes, f), }
        KC_MAP(USER_KEY_0, kv[R_USER_KEY_0_0]),
        KC_MAP(USER_KEY_1, kv[R_USER_KEY_1_0]),
        KC_MAP(USER_KEY_2, kv[R_USER_KEY_2_0]),
        KC_MAP(USER_KEY_3, kv[R_USER_KEY_3_0]),
        KC_MAP(USER_KEY_4, kv[R_USER_KEY_4_0]),
        KC_MAP(USER_KEY_5, kv[R_USER_KEY_5_0]),
        KC_MAP(USER_KEY_6, kv[R_USER_KEY_6_0]),
        KC_MAP(USER_KEY_7, kv[R_USER_KEY_7_0]),
        KC_MAP(PUF_KEY, puf_key),
        KC_MAP(EFUSE_KEY_0, efuse_ukey0_black),
        KC_MAP(EFUSE_KEY_1, efuse_ukey1_black),
        KC_MAP(EFUSE_KEY_RED_0, efuse_ukey0_red),
        KC_MAP(EFUSE_KEY_RED_1, efuse_ukey1_red),
        KC_MAP(AES_KEY_ZEROIZE, cipher.be_key_in),
    #undef KC_MAP
    };

    for (i = 0; i < ARRAY_SIZE(kc_map); i++) {
        if (!(val64 & kc_map[i].cm)) {
            continue;
        }

        ASU_AES_KZERO((void *)s + kc_map[i].ofs);

        if (i < 8) {
            s->kv[R_KEY_LOCK_0 + i] = 0; /* Clear wr-disable as well */
        }
    }

    return 0;  /* Self-clear */
}

static uint64_t asu_aes_key_zeroed_status_postr(RegisterInfo *reg,
                                                uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    uint32_t sta;
    unsigned i, j;

    static const struct {
        uint32_t sm;
        unsigned ofs;
    } kz_map[] = {
    #define KZ_MAP(m, f) { .sm = R_KEY_ZEROED_STATUS_ ## m ## _MASK, \
                           .ofs = offsetof(XlnxAsuAes, f), }
        KZ_MAP(USER_KEY_0, kv[R_USER_KEY_0_0]),
        KZ_MAP(USER_KEY_1, kv[R_USER_KEY_1_0]),
        KZ_MAP(USER_KEY_2, kv[R_USER_KEY_2_0]),
        KZ_MAP(USER_KEY_3, kv[R_USER_KEY_3_0]),
        KZ_MAP(USER_KEY_4, kv[R_USER_KEY_4_0]),
        KZ_MAP(USER_KEY_5, kv[R_USER_KEY_5_0]),
        KZ_MAP(USER_KEY_6, kv[R_USER_KEY_6_0]),
        KZ_MAP(USER_KEY_7, kv[R_USER_KEY_7_0]),
        KZ_MAP(PUF_KEY, puf_key),
        KZ_MAP(EFUSE_KEY_0, efuse_ukey0_black),
        KZ_MAP(EFUSE_KEY_1, efuse_ukey1_black),
        KZ_MAP(EFUSE_RED_KEY_0, efuse_ukey0_red),
        KZ_MAP(EFUSE_RED_KEY_1, efuse_ukey1_red),
        KZ_MAP(AES_KEY_ZEROED, cipher.be_key_in),
    };

    for (sta = 0, i = 0; i < ARRAY_SIZE(kz_map); i++) {
        uint64_t *kp = (void *)s + kz_map[i].ofs;
        uint64_t z8;

        for (z8 = 0, j = 0; j < (256 / 64); j++) {
            z8 |= ldq_he_p(kp + j);
        }

        if (!z8) {
            sta |= kz_map[i].sm;
        }
    }

    s->kv[R_KEY_ZEROED_STATUS] = sta;
    return sta;
}

static void asu_aes_key_crc_sel_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    s->kv[R_AES_USER_KEY_CRC_STATUS] = 0;
}

static void asu_aes_key_crc_value_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);
    unsigned sel = ARRAY_FIELD_EX32(s->kv, AES_USER_SEL_CRC, VALUE);
    unsigned kr0;
    uint32_t calc;
    bool pass = false;

    static const unsigned r_ukey[] = {
        [0] = R_USER_KEY_0_0,
        [1] = R_USER_KEY_1_0,
        [2] = R_USER_KEY_2_0,
        [3] = R_USER_KEY_3_0,
        [4] = R_USER_KEY_4_0,
        [5] = R_USER_KEY_5_0,
        [6] = R_USER_KEY_6_0,
        [7] = R_USER_KEY_7_0,
    };

    if (sel >= ARRAY_SIZE(r_ukey)) {
        goto done;
    }
    kr0 = r_ukey[sel];
    if (!reg) {
        goto done;
    }

    calc = xlnx_efuse_calc_crc(&s->kv[kr0], (ASU_AES_U8_256 / 4), 0);
    pass = (calc == val64);

 done:
    ARRAY_FIELD_DP32(s->kv, AES_USER_KEY_CRC_STATUS, PASS, pass);
    ARRAY_FIELD_DP32(s->kv, AES_USER_KEY_CRC_STATUS, DONE, 1);
}

static uint64_t asu_aes_key_dec_mode_postr(RegisterInfo *reg,
                                           uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    /*
     * A write-only register unless key-vault model's unit-test is enabled.
     *
     * This is for guest to detect if the model-only (i.e., real hardware
     * does not have such feature) unit-test is enabled.
     */
    return s->kv_qtest ? val64 : 0;
}

static void asu_aes_key_transfer_ready_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxAsuAes *s = XLNX_ASU_AES(reg->opaque);

    if (FIELD_EX32(val64, ASU_PMC_KEY_TRANSFER_READY, VAL)) {
        asu_aes_kt_launch(s);
    }
}

static uint64_t asu_aes_read_memory(void *opaque, hwaddr addr,
                                    unsigned size)
{
    /* Trap wr-only registers */
    switch (addr) {
    case A_AES_OPERATION:
    case A_KEY_DEC_TRIG:
    case A_AES_CM:
        return 0;
    default:
        return register_read_memory(opaque, addr, size);
    }
}

static uint64_t asu_aes_kv_read_memory(void *opaque, hwaddr addr,
                                       unsigned size)
{
    /* Trap wr-only registers */
    switch (addr) {
    case A_AES_KEY_CLEAR:
    case A_AES_USER_SEL_CRC_VALUE:
    case A_USER_KEY_0_0 ... A_USER_KEY_7_7:
        return 0;
    default:
        return register_read_memory(opaque, addr, size);
    }
}

static void asu_aes_kv_write_ukeys(void *opaque, hwaddr addr,
                                   uint64_t value, unsigned lr)
{
    RegisterInfoArray *reg_array = opaque;
    XlnxAsuAes *s = XLNX_ASU_AES(reg_array->r[0]->opaque);
    uint32_t lv = s->kv[lr];
    unsigned dr = addr / 4;

    if (FIELD_EX32(lv, KEY_LOCK_0, VALUE)) {
        return;
    }

    if (dr == lr) {
        s->kv[dr] = value & R_KEY_LOCK_0_VALUE_MASK;
    } else {
        s->kv[dr] = value;
    }
}

static void asu_aes_kv_write_memory(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    /* Block write to write-disabled registers */
    switch (addr) {
    case A_KEY_LOCK_0:
    case A_USER_KEY_0_0 ... A_USER_KEY_0_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_0);
        return;
    case A_KEY_LOCK_1:
    case A_USER_KEY_1_0 ... A_USER_KEY_1_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_1);
        return;
    case A_KEY_LOCK_2:
    case A_USER_KEY_2_0 ... A_USER_KEY_2_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_2);
        return;
    case A_KEY_LOCK_3:
    case A_USER_KEY_3_0 ... A_USER_KEY_3_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_3);
        return;
    case A_KEY_LOCK_4:
    case A_USER_KEY_4_0 ... A_USER_KEY_4_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_4);
        return;
    case A_KEY_LOCK_5:
    case A_USER_KEY_5_0 ... A_USER_KEY_5_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_5);
        return;
    case A_KEY_LOCK_6:
    case A_USER_KEY_6_0 ... A_USER_KEY_6_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_6);
        return;
    case A_KEY_LOCK_7:
    case A_USER_KEY_7_0 ... A_USER_KEY_7_7:
        asu_aes_kv_write_ukeys(opaque, addr, value, R_KEY_LOCK_7);
        return;
    default:
        register_write_memory(opaque, addr, value, size);
    }
}

static void asu_aes_reset_memory(RegisterInfo *reg)
{
    if (reg->data && reg->access) {
        *(uint32_t *)(reg->data) = reg->access->reset;
    }
}

static void asu_aes_reset(DeviceState *dev)
{
    XlnxAsuAes *s = XLNX_ASU_AES(dev);
    unsigned int i;

    /* Reset values directly to avoid write-triggered actions */
    for (i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        asu_aes_reset_memory(&s->regs_info[i]);
    }

    for (i = 0; i < ARRAY_SIZE(s->kv_regs_info); i++) {
        asu_aes_reset_memory(&s->kv_regs_info[i]);
    }

    asu_aes_update_irq(s);

    /* Clear cipher context */
    asu_aes_cipher_reset(s);

    /* Clear transferred keys */
    ASU_AES_KZERO(s->efuse_ukey0_black);
    ASU_AES_KZERO(s->efuse_ukey1_black);
    ASU_AES_KZERO(s->efuse_ukey0_red);
    ASU_AES_KZERO(s->efuse_ukey1_red);
    ASU_AES_KZERO(s->puf_key);
}

static void asu_aes_realize(DeviceState *dev, Error **errp)
{
    /* Delete this if you don't need it */
}

static const RegisterAccessInfo asu_aes_regs_info[] = {
    {   .name = "AES_STATUS",  .addr = A_AES_STATUS,
        .rsvd = 0xffff1ffc,
        .ro = 0xffffffff,
    },{ .name = "AES_OPERATION",  .addr = A_AES_OPERATION,
        .pre_write = asu_aes_operation_prew,
    },{ .name = "AES_SOFT_RST",  .addr = A_AES_SOFT_RST,
        .reset = 0x1,
        .post_write = asu_aes_soft_rst_postw,
    },{ .name = "AES_IV_IN_0",  .addr = A_AES_IV_IN_0,
    },{ .name = "AES_IV_IN_1",  .addr = A_AES_IV_IN_1,
    },{ .name = "AES_IV_IN_2",  .addr = A_AES_IV_IN_2,
    },{ .name = "AES_IV_IN_3",  .addr = A_AES_IV_IN_3,
    },{ .name = "AES_IV_MASK_IN_0",  .addr = A_AES_IV_MASK_IN_0,
    },{ .name = "AES_IV_MASK_IN_1",  .addr = A_AES_IV_MASK_IN_1,
    },{ .name = "AES_IV_MASK_IN_2",  .addr = A_AES_IV_MASK_IN_2,
    },{ .name = "AES_IV_MASK_IN_3",  .addr = A_AES_IV_MASK_IN_3,
    },{ .name = "AES_IV_OUT_0",  .addr = A_AES_IV_OUT_0,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_OUT_1",  .addr = A_AES_IV_OUT_1,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_OUT_2",  .addr = A_AES_IV_OUT_2,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_OUT_3",  .addr = A_AES_IV_OUT_3,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_MASK_OUT_0",  .addr = A_AES_IV_MASK_OUT_0,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_MASK_OUT_1",  .addr = A_AES_IV_MASK_OUT_1,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_MASK_OUT_2",  .addr = A_AES_IV_MASK_OUT_2,
        .ro = 0xffffffff,
    },{ .name = "AES_IV_MASK_OUT_3",  .addr = A_AES_IV_MASK_OUT_3,
        .ro = 0xffffffff,
    },{ .name = "KEY_DEC_TRIG",  .addr = A_KEY_DEC_TRIG,
        .post_write = asu_aes_key_dec_trig_postw,
    },{ .name = "AES_CM",  .addr = A_AES_CM,
        .reset = 0x7,
    },{ .name = "AES_SPLIT_CFG",  .addr = A_AES_SPLIT_CFG,
    },{ .name = "AES_MODE_CONFIG",  .addr = A_AES_MODE_CONFIG,
        .rsvd = 0x1fb0,
        .post_write = asu_aes_mode_config_postw,
    },{ .name = "AES_MAC_OUT_0",  .addr = A_AES_MAC_OUT_0,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_OUT_1",  .addr = A_AES_MAC_OUT_1,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_OUT_2",  .addr = A_AES_MAC_OUT_2,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_OUT_3",  .addr = A_AES_MAC_OUT_3,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_MASK_OUT_0",  .addr = A_AES_MAC_MASK_OUT_0,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_MASK_OUT_1",  .addr = A_AES_MAC_MASK_OUT_1,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_MASK_OUT_2",  .addr = A_AES_MAC_MASK_OUT_2,
        .ro = 0xffffffff,
    },{ .name = "AES_MAC_MASK_OUT_3",  .addr = A_AES_MAC_MASK_OUT_3,
        .ro = 0xffffffff,
    },{ .name = "AES_DATA_SWAP",  .addr = A_AES_DATA_SWAP,
    },{ .name = "AES_INTERRUPT_STATUS",  .addr = A_AES_INTERRUPT_STATUS,
        .w1c = 0x1,
        .post_write = asu_aes_irq_status_postw,
    },{ .name = "AES_INTERRUPT_MASK",  .addr = A_AES_INTERRUPT_MASK,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "AES_INTERRUPT_ENABLE",  .addr = A_AES_INTERRUPT_ENABLE,
        .pre_write = asu_aes_irq_enable_prew,
    },{ .name = "AES_INTERRUPT_DISABLE",  .addr = A_AES_INTERRUPT_DISABLE,
        .pre_write = asu_aes_irq_disable_prew,
    },{ .name = "AES_INTERRUPT_TRIGGER",  .addr = A_AES_INTERRUPT_TRIGGER,
        .pre_write = asu_aes_irq_trigger_prew,
    }
};

static const RegisterAccessInfo asu_aes_kv_regs_info[] = {
    {   .name = "AES_KEY_SEL",  .addr = A_AES_KEY_SEL,
    },{ .name = "AES_KEY_CLEAR",  .addr = A_AES_KEY_CLEAR,
        .rsvd = 0xffffc000,
        .pre_write = asu_aes_key_clear_prew,
    },{ .name = "KEY_ZEROED_STATUS",  .addr = A_KEY_ZEROED_STATUS,
        .rsvd = 0xffffc000,
        .ro = 0xffffffff,
        .post_read = asu_aes_key_zeroed_status_postr,
    },{ .name = "AES_USER_SEL_CRC",  .addr = A_AES_USER_SEL_CRC,
        .post_write = asu_aes_key_crc_sel_postw,
    },{ .name = "AES_USER_SEL_CRC_VALUE",  .addr = A_AES_USER_SEL_CRC_VALUE,
        .post_write = asu_aes_key_crc_value_postw,
    },{ .name = "AES_USER_KEY_CRC_STATUS",  .addr = A_AES_USER_KEY_CRC_STATUS,
        .ro = 0x3,
    },{ .name = "KEY_MASK_0",  .addr = A_KEY_MASK_0,
    },{ .name = "KEY_MASK_1",  .addr = A_KEY_MASK_1,
    },{ .name = "KEY_MASK_2",  .addr = A_KEY_MASK_2,
    },{ .name = "KEY_MASK_3",  .addr = A_KEY_MASK_3,
    },{ .name = "KEY_MASK_4",  .addr = A_KEY_MASK_4,
    },{ .name = "KEY_MASK_5",  .addr = A_KEY_MASK_5,
    },{ .name = "KEY_MASK_6",  .addr = A_KEY_MASK_6,
    },{ .name = "KEY_MASK_7",  .addr = A_KEY_MASK_7,
    },{ .name = "KEY_LOCK_0",  .addr = A_KEY_LOCK_0,
    },{ .name = "KEY_LOCK_1",  .addr = A_KEY_LOCK_1,
    },{ .name = "KEY_LOCK_2",  .addr = A_KEY_LOCK_2,
    },{ .name = "KEY_LOCK_3",  .addr = A_KEY_LOCK_3,
    },{ .name = "KEY_LOCK_4",  .addr = A_KEY_LOCK_4,
    },{ .name = "KEY_LOCK_5",  .addr = A_KEY_LOCK_5,
    },{ .name = "KEY_LOCK_6",  .addr = A_KEY_LOCK_6,
    },{ .name = "KEY_LOCK_7",  .addr = A_KEY_LOCK_7,
    },{ .name = "USER_KEY_0_0",  .addr = A_USER_KEY_0_0,
    },{ .name = "USER_KEY_0_1",  .addr = A_USER_KEY_0_1,
    },{ .name = "USER_KEY_0_2",  .addr = A_USER_KEY_0_2,
    },{ .name = "USER_KEY_0_3",  .addr = A_USER_KEY_0_3,
    },{ .name = "USER_KEY_0_4",  .addr = A_USER_KEY_0_4,
    },{ .name = "USER_KEY_0_5",  .addr = A_USER_KEY_0_5,
    },{ .name = "USER_KEY_0_6",  .addr = A_USER_KEY_0_6,
    },{ .name = "USER_KEY_0_7",  .addr = A_USER_KEY_0_7,
    },{ .name = "USER_KEY_1_0",  .addr = A_USER_KEY_1_0,
    },{ .name = "USER_KEY_1_1",  .addr = A_USER_KEY_1_1,
    },{ .name = "USER_KEY_1_2",  .addr = A_USER_KEY_1_2,
    },{ .name = "USER_KEY_1_3",  .addr = A_USER_KEY_1_3,
    },{ .name = "USER_KEY_1_4",  .addr = A_USER_KEY_1_4,
    },{ .name = "USER_KEY_1_5",  .addr = A_USER_KEY_1_5,
    },{ .name = "USER_KEY_1_6",  .addr = A_USER_KEY_1_6,
    },{ .name = "USER_KEY_1_7",  .addr = A_USER_KEY_1_7,
    },{ .name = "USER_KEY_2_0",  .addr = A_USER_KEY_2_0,
    },{ .name = "USER_KEY_2_1",  .addr = A_USER_KEY_2_1,
    },{ .name = "USER_KEY_2_2",  .addr = A_USER_KEY_2_2,
    },{ .name = "USER_KEY_2_3",  .addr = A_USER_KEY_2_3,
    },{ .name = "USER_KEY_2_4",  .addr = A_USER_KEY_2_4,
    },{ .name = "USER_KEY_2_5",  .addr = A_USER_KEY_2_5,
    },{ .name = "USER_KEY_2_6",  .addr = A_USER_KEY_2_6,
    },{ .name = "USER_KEY_2_7",  .addr = A_USER_KEY_2_7,
    },{ .name = "USER_KEY_3_0",  .addr = A_USER_KEY_3_0,
    },{ .name = "USER_KEY_3_1",  .addr = A_USER_KEY_3_1,
    },{ .name = "USER_KEY_3_2",  .addr = A_USER_KEY_3_2,
    },{ .name = "USER_KEY_3_3",  .addr = A_USER_KEY_3_3,
    },{ .name = "USER_KEY_3_4",  .addr = A_USER_KEY_3_4,
    },{ .name = "USER_KEY_3_5",  .addr = A_USER_KEY_3_5,
    },{ .name = "USER_KEY_3_6",  .addr = A_USER_KEY_3_6,
    },{ .name = "USER_KEY_3_7",  .addr = A_USER_KEY_3_7,
    },{ .name = "USER_KEY_4_0",  .addr = A_USER_KEY_4_0,
    },{ .name = "USER_KEY_4_1",  .addr = A_USER_KEY_4_1,
    },{ .name = "USER_KEY_4_2",  .addr = A_USER_KEY_4_2,
    },{ .name = "USER_KEY_4_3",  .addr = A_USER_KEY_4_3,
    },{ .name = "USER_KEY_4_4",  .addr = A_USER_KEY_4_4,
    },{ .name = "USER_KEY_4_5",  .addr = A_USER_KEY_4_5,
    },{ .name = "USER_KEY_4_6",  .addr = A_USER_KEY_4_6,
    },{ .name = "USER_KEY_4_7",  .addr = A_USER_KEY_4_7,
    },{ .name = "USER_KEY_5_0",  .addr = A_USER_KEY_5_0,
    },{ .name = "USER_KEY_5_1",  .addr = A_USER_KEY_5_1,
    },{ .name = "USER_KEY_5_2",  .addr = A_USER_KEY_5_2,
    },{ .name = "USER_KEY_5_3",  .addr = A_USER_KEY_5_3,
    },{ .name = "USER_KEY_5_4",  .addr = A_USER_KEY_5_4,
    },{ .name = "USER_KEY_5_5",  .addr = A_USER_KEY_5_5,
    },{ .name = "USER_KEY_5_6",  .addr = A_USER_KEY_5_6,
    },{ .name = "USER_KEY_5_7",  .addr = A_USER_KEY_5_7,
    },{ .name = "USER_KEY_6_0",  .addr = A_USER_KEY_6_0,
    },{ .name = "USER_KEY_6_1",  .addr = A_USER_KEY_6_1,
    },{ .name = "USER_KEY_6_2",  .addr = A_USER_KEY_6_2,
    },{ .name = "USER_KEY_6_3",  .addr = A_USER_KEY_6_3,
    },{ .name = "USER_KEY_6_4",  .addr = A_USER_KEY_6_4,
    },{ .name = "USER_KEY_6_5",  .addr = A_USER_KEY_6_5,
    },{ .name = "USER_KEY_6_6",  .addr = A_USER_KEY_6_6,
    },{ .name = "USER_KEY_6_7",  .addr = A_USER_KEY_6_7,
    },{ .name = "USER_KEY_7_0",  .addr = A_USER_KEY_7_0,
    },{ .name = "USER_KEY_7_1",  .addr = A_USER_KEY_7_1,
    },{ .name = "USER_KEY_7_2",  .addr = A_USER_KEY_7_2,
    },{ .name = "USER_KEY_7_3",  .addr = A_USER_KEY_7_3,
    },{ .name = "USER_KEY_7_4",  .addr = A_USER_KEY_7_4,
    },{ .name = "USER_KEY_7_5",  .addr = A_USER_KEY_7_5,
    },{ .name = "USER_KEY_7_6",  .addr = A_USER_KEY_7_6,
    },{ .name = "USER_KEY_7_7",  .addr = A_USER_KEY_7_7,
    },{ .name = "AES_KEY_SIZE",  .addr = A_AES_KEY_SIZE,
        .reset = 0x2,
    },{ .name = "AES_KEY_TO_BE_DEC_SIZE",  .addr = A_AES_KEY_TO_BE_DEC_SIZE,
        .reset = 0x2,
    },{ .name = "AES_KEY_DEC_MODE",  .addr = A_AES_KEY_DEC_MODE,
        .post_read = asu_aes_key_dec_mode_postr,
    },{ .name = "AES_KEY_TO_BE_DEC_SEL",  .addr = A_AES_KEY_TO_BE_DEC_SEL,
    },{ .name = "ASU_PMC_KEY_TRANSFER_READY",
        .addr = A_ASU_PMC_KEY_TRANSFER_READY,
        .post_write = asu_aes_key_transfer_ready_postw,
    },{ .name = "EFUSE_KEY_0_BLACK_OR_RED",  .addr = A_EFUSE_KEY_0_BLACK_OR_RED,
    },{ .name = "EFUSE_KEY_1_BLACK_OR_RED",  .addr = A_EFUSE_KEY_1_BLACK_OR_RED,
    },{ .name = "AES_PL_KEY_SEL",  .addr = A_AES_PL_KEY_SEL,
    },{ .name = "KV_INTERRUPT_STATUS",  .addr = A_KV_INTERRUPT_STATUS,
        .w1c = 0x1,
        .post_write = asu_aes_kv_irq_status_postw,
    },{ .name = "KV_INTERRUPT_MASK",  .addr = A_KV_INTERRUPT_MASK,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "KV_INTERRUPT_ENABLE",  .addr = A_KV_INTERRUPT_ENABLE,
        .pre_write = asu_aes_kv_irq_enable_prew,
    },{ .name = "KV_INTERRUPT_DISABLE",  .addr = A_KV_INTERRUPT_DISABLE,
        .pre_write = asu_aes_kv_irq_disable_prew,
    },{ .name = "KV_INTERRUPT_TRIGGER",  .addr = A_KV_INTERRUPT_TRIGGER,
        .pre_write = asu_aes_kv_irq_trigger_prew,
    }
};

static const MemoryRegionOps asu_aes_ops = {
    .read = asu_aes_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps asu_aes_kv_ops = {
    .read = asu_aes_kv_read_memory,
    .write = asu_aes_kv_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static MemoryRegion *asu_aes_mr_rename(MemoryRegion *mr, const char *suffix)
{
    /* Save enough to call memory_region_init_io */
    const void *ops = mr->ops;
    void *opaque = mr->opaque;
    Object *owner = memory_region_owner(mr);
    uint64_t mr_size = memory_region_size(mr);
    g_autofree char *new_name = NULL;

    new_name = g_strjoin(NULL, memory_region_name(mr), suffix, NULL);

    /* Finalize it */
    object_unparent(OBJECT(mr));

    /* Recreate it with new name */
    memory_region_init_io(mr, owner, ops, opaque, new_name, mr_size);

    return mr;
}

static void asu_aes_finalize(Object *obj)
{
    XlnxAsuAes *s = XLNX_ASU_AES(obj);

    g_free(s->out.buf);
    asu_aes_cipher(s, ASU_AES_RESET, 0, NULL, NULL);
}

static void asu_aes_init(Object *obj)
{
    XlnxAsuAes *s = XLNX_ASU_AES(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *aes_reg_array;
    RegisterInfoArray *kv_reg_array;
    MemoryRegion *aes_mr, *kv_mr;
    uint64_t io_sz;

    aes_reg_array =
        register_init_block32(DEVICE(obj), asu_aes_regs_info,
                              ARRAY_SIZE(asu_aes_regs_info),
                              s->regs_info, s->regs,
                              &asu_aes_ops,
                              XLNX_ASU_AES_ERR_DEBUG,
                              ASU_AES_R_MAX * 4);

    kv_reg_array =
        register_init_block32(DEVICE(obj), asu_aes_kv_regs_info,
                              ARRAY_SIZE(asu_aes_kv_regs_info),
                              s->kv_regs_info, s->kv,
                              &asu_aes_kv_ops,
                              XLNX_ASU_AES_KV_ERR_DEBUG,
                              ASU_AES_KV_R_MAX * 4);

    aes_mr = asu_aes_mr_rename(&aes_reg_array->mem, "-engine");
    kv_mr = asu_aes_mr_rename(&kv_reg_array->mem, "-key-vault");
    io_sz = KEY_VAULT_MMIO_OFFSET + memory_region_size(kv_mr);

    memory_region_init(&s->iomem, obj, TYPE_XLNX_ASU_AES, io_sz);
    memory_region_add_subregion(&s->iomem, 0, aes_mr);
    memory_region_add_subregion(&s->iomem, KEY_VAULT_MMIO_OFFSET, kv_mr);
    sysbus_init_mmio(sbd, &s->iomem);

    /* To bus interrupt controller */
    sysbus_init_irq(sbd, &s->irq_aes_interrupt);
    sysbus_init_irq(sbd, &s->irq_kv_interrupt);
}

#define ASU_AES_ARRAY_VMS(n) \
    VMSTATE_UINT32_ARRAY(n, XlnxAsuAes, sizeof((XlnxAsuAes *)0->n) / 4)

static const VMStateDescription vmstate_asu_aes = {
    .name = TYPE_XLNX_ASU_AES,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(cipher.be_key_in, XlnxAsuAes),
        VMSTATE_BUFFER(cipher.be_iv_in, XlnxAsuAes),
        VMSTATE_BUFFER(efuse_ukey0_black, XlnxAsuAes),
        VMSTATE_BUFFER(efuse_ukey1_black, XlnxAsuAes),
        VMSTATE_BUFFER(efuse_ukey0_red, XlnxAsuAes),
        VMSTATE_BUFFER(efuse_ukey1_red, XlnxAsuAes),
        VMSTATE_BUFFER(puf_key, XlnxAsuAes),
        VMSTATE_UINT32_ARRAY(regs, XlnxAsuAes, ASU_AES_R_MAX),
        VMSTATE_UINT32_ARRAY(kv, XlnxAsuAes, ASU_AES_KV_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property asu_aes_properties[] = {
    DEFINE_PROP_BOOL("kv-qtest",
                     XlnxAsuAes, kv_qtest, false),
    DEFINE_PROP_BOOL("noisy-gerr",
                     XlnxAsuAes, noisy_gerr, false),
    DEFINE_PROP_LINK("stream-connected-aes",
                     XlnxAsuAes, out.dev,
                     TYPE_STREAM_SINK, StreamSink *),
    DEFINE_PROP_LINK("pmxc-aes", XlnxAsuAes,
                    pmxc_aes, TYPE_PMXC_KEY_TRANSFER,
                    pmxcKT *),
    DEFINE_PROP_END_OF_LIST(),
};

static void asu_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
    pmxcKTClass *ktc = PMXC_KT_CLASS(klass);

    dc->realize = asu_aes_realize;
    dc->reset = asu_aes_reset;
    dc->vmsd = &vmstate_asu_aes;
    device_class_set_props(dc, asu_aes_properties);

    ssc->push = asu_aes_stream_sink;
    ssc->can_push = asu_aes_stream_sink_ready;

    ktc->done = asu_aes_int_pmxc_kt_done;
    ktc->send_key = asu_aes_int_receive_key;
}

static const TypeInfo asu_aes_info = {
    .name          = TYPE_XLNX_ASU_AES,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxAsuAes),
    .class_init    = asu_aes_class_init,
    .instance_init = asu_aes_init,
    .instance_finalize = asu_aes_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { TYPE_PMXC_KEY_TRANSFER },
        { }
    }
};

static void asu_aes_register_types(void)
{
    type_register_static(&asu_aes_info);
}

type_init(asu_aes_register_types)
