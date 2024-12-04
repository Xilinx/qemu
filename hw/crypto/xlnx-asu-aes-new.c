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
#include "hw/crypto/xlnx-asu-kv.h"
#include "hw/stream.h"
#include "trace.h"

typedef enum AsuAesMode {
    ASU_AES_CBC = 0x0,
    ASU_AES_CFB = 0x1,
    ASU_AES_OFB = 0x2,
    ASU_AES_CTR = 0x3,
    ASU_AES_ECB = 0x4,
    ASU_AES_CCM = 0x5,
    ASU_AES_GCM = 0x6,
    ASU_AES_CMAC = 0x8,
    ASU_AES_GHASH = 0xe,

    ASU_AES_INVALID_MODE = -1,
} AsuAesMode;

const char *ASU_AES_MODE_STR[] = {
    [ASU_AES_CBC] = "aes-cbc",
    [ASU_AES_CFB] = "aes-cfb",
    [ASU_AES_OFB] = "aes-ofb",
    [ASU_AES_CTR] = "aes-ctr",
    [ASU_AES_ECB] = "aes-ecb",
    [ASU_AES_CCM] = "aes-ccm",
    [ASU_AES_GCM] = "aes-gcm",
    [ASU_AES_CMAC] = "aes-cmac",
    [ASU_AES_GHASH] = "aes-ghash",
};

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

static inline bool asu_aes_in_auth_phase(XilinxAsuAesState *s)
{
    return FIELD_EX32(s->mode_cfg, MODE_CONFIG, AUTH);
}

static inline AsuAesMode get_current_mode(XilinxAsuAesState *s)
{
    uint32_t v;

    v = FIELD_EX32(s->mode_cfg, MODE_CONFIG, ENGINE_MODE);

    switch (v) {
    case ASU_AES_CBC:
    case ASU_AES_CFB:
    case ASU_AES_OFB:
    case ASU_AES_CTR:
    case ASU_AES_ECB:
    case ASU_AES_CCM:
    case ASU_AES_GCM:
    case ASU_AES_CMAC:
    case ASU_AES_GHASH:
        return v;

    default:
        return ASU_AES_INVALID_MODE;
    }
}

static inline bool current_mode_is_streaming(XilinxAsuAesState *s)
{
    switch (get_current_mode(s)) {
    case ASU_AES_CBC:
    case ASU_AES_CFB:
    case ASU_AES_OFB:
    case ASU_AES_CTR:
    case ASU_AES_ECB:
        return true;

    case ASU_AES_CCM:
    case ASU_AES_GCM:
        return !asu_aes_in_auth_phase(s);

    default:
        return false;
    }
}

static inline bool key_split_enabled(XilinxAsuAesState *s)
{
    return s->cm_enabled && FIELD_EX32(s->split_cfg, SPLIT_CFG, KEY_SPLIT);
}

static inline bool fifo_in_is_full(XilinxAsuAesState *s)
{
    return s->fifo_in_num == ARRAY_SIZE(s->fifo_in);
}

static inline size_t fifo_in_num_free(XilinxAsuAesState *s)
{
    return ARRAY_SIZE(s->fifo_in) - s->fifo_in_num;
}

static inline uint8_t *fifo_get_current(XilinxAsuAesState *s)
{
    return s->fifo_in;
}

static inline bool fifo_in_push(XilinxAsuAesState *s, const uint8_t *buf,
                                size_t len)
{
    uint8_t *fifo = fifo_get_current(s);

    g_assert(s->fifo_in_num + len <= ARRAY_SIZE(s->fifo_in));

    memcpy(fifo + s->fifo_in_num, buf, len);
    s->fifo_in_num += len;

    return fifo_in_is_full(s);
}

/*
 * Pad the fifo to obtain a 128 bits data. Return true if the current mode is
 * GHASH. In that case the controller generates the appropriate multiple of 128
 * bits padding following this rule:
 *      padded iv = iv || 0^s || 0^64 || len_64(iv)
 *          with s = 128 * ceil(len(iv) / 128) - len(iv)
 * Since this rule generates an extra 128 bits block, the
 * fifo_in_pad_ghash_final_block must be called if this function returns true
 * to obtain the final padding block.
 */
static inline bool fifo_in_pad(XilinxAsuAesState *s)
{
    AsuAesMode mode = get_current_mode(s);
    uint8_t *fifo = fifo_get_current(s);
    bool ghash_padding = (mode == ASU_AES_GHASH);

    s->pad_amount = fifo_in_num_free(s);

    if (!s->pad_amount) {
        return ghash_padding;
    }

    if ((mode == ASU_AES_CMAC) && (fifo == s->fifo_in)) {
        /*
         * in CMAC mode, the padding is done with 10....0.
         */
        fifo[s->fifo_in_num++] = 0x80;
    }

    memset(fifo + s->fifo_in_num, 0, fifo_in_num_free(s));
    s->fifo_in_num = ARRAY_SIZE(s->fifo_in);

    return ghash_padding;
}

/*
 * Pad the fifo with the "0^64 || len_64(iv)" part of the GHASH padding.
 */
static inline void fifo_in_pad_ghash_final_block(XilinxAsuAesState *s)
{
    uint8_t *fifo = fifo_get_current(s);

    g_assert(get_current_mode(s) == ASU_AES_GHASH);
    g_assert(fifo_in_num_free(s) == ASU_AES_BLOCK_SIZE);

    memcpy(fifo, s->aes_ctx.s0_gcmlen, ASU_AES_BLOCK_SIZE);

    s->fifo_in_num = ASU_AES_BLOCK_SIZE;
}

static inline void fifo_in_clear(XilinxAsuAesState *s)
{
    s->fifo_in_num = 0;
}

static void update_irq(XilinxAsuAesState *s)
{
    qemu_set_irq(s->irq, s->irq_sta && !s->irq_mask);
}

static inline void raise_done_irq(XilinxAsuAesState *s)
{
    trace_xilinx_asu_aes_raise_irq();
    s->irq_sta = true;
    update_irq(s);
}

static inline void block_copy(AsuAesBlock dst, const AsuAesBlock src)
{
    memcpy(dst, src, AES_BLOCK_SIZE);
}

/* r = a ^ b */
static inline void block_xor(AsuAesBlock r,
                             const AsuAesBlock a,
                             const AsuAesBlock b)
{
    const Int128 *a128 = (const Int128 *) a;
    const Int128 *b128 = (const Int128 *) b;
    Int128 *r128 = (Int128 *) r;

    *r128 = int128_xor(*a128, *b128);
}

static inline void block_xor_u8(AsuAesBlock a, uint8_t b)
{
    a[15] ^= b;
}

/* a = a << n */
static inline void block_shl(AsuAesBlock a, int n)
{
    Int128 *a128 = (Int128 *)a;

#if !HOST_BIG_ENDIAN
    *a128 = bswap128(*a128);
#endif
    *a128 = int128_lshift(*a128, n);
#if !HOST_BIG_ENDIAN
    *a128 = bswap128(*a128);
#endif
}

/* a = a >> n */
static inline void block_shr(AsuAesBlock a, int n)
{
    Int128 *a128 = (Int128 *)a;

#if !HOST_BIG_ENDIAN
    *a128 = bswap128(*a128);
#endif
    *a128 = int128_urshift(*a128, n);
#if !HOST_BIG_ENDIAN
    *a128 = bswap128(*a128);
#endif
}

/* r = a + b */
static inline void block_add_i(AsuAesBlock r, const AsuAesBlock a, int b)
{
    Int128 *r128 = (Int128 *)r;
    Int128 *a128 = (Int128 *)a;

#if !HOST_BIG_ENDIAN
    *r128 = bswap128(*a128);
    int128_addto(r128, int128_makes64(b));
    *r128 = bswap128(*r128);
#else
    *r128 = int128_add(*a128, int128_makes(b));
#endif
}

/* a[8..15] = a[8..15] + b */
static inline void block_add_lo64_u(AsuAesBlock a, unsigned int b)
{
    uint64_t *a64 = (uint64_t *) &a[ASU_AES_BLOCK_SIZE / 2];

    *a64 = be64_to_cpu(*a64);
    *a64 += b;
    *a64 = cpu_to_be64(*a64);
}

/* a[0..7] = a[0..7] + b */
static inline void block_add_hi64_u(AsuAesBlock a, unsigned int b)
{
    uint64_t *a64 = (uint64_t *) a;

    *a64 = be64_to_cpu(*a64);
    *a64 += b;
    *a64 = cpu_to_be64(*a64);
}

/* return a[8..15] as uint64_t in cpu order */
static inline uint64_t block_extract_lo64(AsuAesBlock a)
{
    uint64_t *a64 = (uint64_t *) &a[ASU_AES_BLOCK_SIZE / 2];

    return be64_to_cpu(*a64);
}

static inline void block_inc(AsuAesBlock a)
{
    block_add_i(a, a, 1);
}

static inline bool block_msb(const AsuAesBlock a)
{
    return a[0] >> 7;
}

static inline bool block_lsb(const AsuAesBlock a)
{
    return a[ASU_AES_BLOCK_SIZE - 1] & 0x1;
}

/* a = a & (1 << (bytes * 8) - 1) */
static inline void block_mask_lsb(AsuAesBlock a, size_t bytes)
{
    memset(a + ASU_AES_BLOCK_SIZE - bytes, 0, bytes);
}

static inline void asu_aes_do_encrypt(XilinxAsuAesState *s,
                                      uint8_t *out,
                                      const uint8_t *in)
{
    AES_KEY key;
    int ret;

    ret = AES_set_encrypt_key(s->aes_ctx.key, s->aes_ctx.key_size * 8,
                              &key);
    g_assert(ret == 0);

    AES_encrypt(in, out, &key);
}

static inline void asu_aes_do_decrypt(XilinxAsuAesState *s,
                                      uint8_t *out,
                                      const uint8_t *in)
{
    AES_KEY key;
    int ret;

    ret = AES_set_decrypt_key(s->aes_ctx.key, s->aes_ctx.key_size * 8,
                              &key);
    g_assert(ret == 0);

    AES_decrypt(in, out, &key);
}

/*
 * out = ghash(a, b) = (a ^ b) . h
 * with h = aes(0^128, k) and the "." operator the multiplication in GF(2^128)
 * using the field polynomial 1 + x + x^2 + x^7 + x^128.
 */
static void ghash(XilinxAsuAesState *s, uint8_t *out,
                  const AsuAesBlock a, const AsuAesBlock b)
{
    size_t i;
    AsuAesBlock v;
    uint8_t h[16];

    block_xor(v, a, b);

    memset(out, 0, ASU_AES_BLOCK_SIZE);
    asu_aes_do_encrypt(s, h, out);

    for (i = 0; i < ASU_AES_BLOCK_SIZE; i++) {
        uint8_t bit;

        for (bit = 0x80; bit; bit >>= 1) {
            bool lsb;

            if (h[i] & bit) {
                block_xor(out, out, v);
            }

            lsb = block_lsb(v);
            block_shr(v, 1);

            if (lsb) {
                v[0] ^= 0xe1;
            }
        }
    }
}

static void cmac_derive_key(AsuAesBlock l)
{
    bool msb = block_msb(l);

    block_shl(l, 1);

    if (msb) {
        block_xor_u8(l, 0x87);
    }
}

static void cmac_preprocess_last_block(XilinxAsuAesState *s, AsuAesBlock in)
{
    AsuAesBlock l;

    memset(l, 0, sizeof(l));
    asu_aes_do_encrypt(s, l, l);

    /* K1 */
    cmac_derive_key(l);

    if (s->pad_amount) {
        /* K2 */
        cmac_derive_key(l);
    }

    block_xor(s->aes_ctx.out, in, l);
}

static uint8_t *asu_aes_preprocess(XilinxAsuAesState *s, AsuAesBlock in,
                                   AsuAesMode mode, bool enc)
{
    switch (mode) {
    case ASU_AES_ECB:
        return in;

    case ASU_AES_CFB:
    case ASU_AES_OFB:
    case ASU_AES_CTR:
        return s->aes_ctx.iv;

    case ASU_AES_CBC:
        if (enc) {
            block_xor(s->aes_ctx.out, in, s->aes_ctx.iv);
            return s->aes_ctx.out;
        } else {
            return in;
        }

    case ASU_AES_CMAC:
        if (s->eop) {
            cmac_preprocess_last_block(s, in);
            block_xor(s->aes_ctx.out, s->aes_ctx.out, s->aes_ctx.iv);
        } else {
            block_xor(s->aes_ctx.out, in, s->aes_ctx.iv);
        }

        return s->aes_ctx.out;

    case ASU_AES_CCM:
    case ASU_AES_GCM:
        /*
         * CCM is a CTR + CBC-MAC and GCM a CTR + GMAC.
         * No encryption/decryption during the auth phase.
         */
        if (asu_aes_in_auth_phase(s)) {
            return NULL;
        } else {
            return s->aes_ctx.iv;
        }

    case ASU_AES_GHASH:
        return NULL;

    default:
        g_assert_not_reached();
    }
}

static void asu_aes_process(XilinxAsuAesState *s, const AsuAesBlock in,
                            AsuAesMode mode, bool enc)
{
    bool do_encrypt;

    switch (mode) {
    case ASU_AES_ECB:
    case ASU_AES_CBC:
        do_encrypt = enc;
        break;

    case ASU_AES_CFB:
    case ASU_AES_OFB:
    case ASU_AES_CTR:
    case ASU_AES_CMAC:
    case ASU_AES_CCM:
    case ASU_AES_GCM:
        /* Those modes encrypt the IV, even when decrypting */
        do_encrypt = true;
        break;

    default:
        g_assert_not_reached();
    }

    if (do_encrypt) {
        asu_aes_do_encrypt(s, s->aes_ctx.out, in);
    } else {
        asu_aes_do_decrypt(s, s->aes_ctx.out, in);
    }
}

static void asu_aes_postprocess(XilinxAsuAesState *s, AsuAesBlock in,
                                AsuAesMode mode, bool enc)
{
    uint8_t *out = s->aes_ctx.out;
    uint8_t *mac = s->aes_ctx.mac;

    switch (mode) {
    case ASU_AES_ECB:
        break;

    case ASU_AES_CFB:
        block_xor(out, out, in);
        block_copy(s->aes_ctx.iv, enc ? out : in);
        break;

    case ASU_AES_CBC:
        if (enc) {
            block_copy(s->aes_ctx.iv, out);
        } else {
            block_xor(out, out, s->aes_ctx.iv);
            block_copy(s->aes_ctx.iv, in);
        }
        break;

    case ASU_AES_OFB:
        block_copy(s->aes_ctx.iv, out);
        block_xor(out, out, in);
        break;

    case ASU_AES_CTR:
        block_xor(out, out, in);
        block_inc(s->aes_ctx.iv);
        break;

    case ASU_AES_CMAC:
        block_copy(s->aes_ctx.iv, out);
        break;

    case ASU_AES_CCM:
        if (!asu_aes_in_auth_phase(s)) {
            block_xor(out, out, in);
            block_inc(s->aes_ctx.iv);
            if (!enc && s->pad_amount) {
                /*
                 * The cipher was padded.
                 * We must clear the corresponding bits
                 */
                block_mask_lsb(out, s->pad_amount);
            }

            block_xor(mac, mac, enc ? in : out);
        } else {
            block_xor(mac, mac, in);
        }

        asu_aes_do_encrypt(s, mac, mac);
        break;

    case ASU_AES_GCM:
        if (!asu_aes_in_auth_phase(s)) {
            block_xor(out, out, in);
            block_inc(s->aes_ctx.iv);

            if (enc && s->pad_amount) {
                block_mask_lsb(out, s->pad_amount);
            }

            ghash(s, mac, mac, enc ? out : in);
            block_add_lo64_u(s->aes_ctx.s0_gcmlen,
                             (ASU_AES_BLOCK_SIZE - s->pad_amount) * 8);
        } else {
            ghash(s, mac, mac, in);
            block_add_hi64_u(s->aes_ctx.s0_gcmlen,
                             (ASU_AES_BLOCK_SIZE - s->pad_amount) * 8);
        }
        break;

    case ASU_AES_GHASH:
        ghash(s, mac, mac, in);
        block_add_lo64_u(s->aes_ctx.s0_gcmlen,
                         (ASU_AES_BLOCK_SIZE - s->pad_amount) * 8);
        break;

    default:
        g_assert_not_reached();
    }
}

static void asu_aes_go(XilinxAsuAesState *s, AsuAesBlock in, size_t len)
{
    AsuAesMode mode;
    bool enc;
    const uint8_t *aes_in_buf;

    g_assert(len == ASU_AES_BLOCK_SIZE);
    g_assert(s->aes_ctx.key_size == 16
             || s->aes_ctx.key_size == 24
             || s->aes_ctx.key_size == 32);

    enc = FIELD_EX32(s->mode_cfg, MODE_CONFIG, ENC_DEC_N);
    mode = get_current_mode(s);

    if (mode == ASU_AES_INVALID_MODE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_XILINX_ASU_AES ": invalid ENGINE_MODE %d\n",
                      mode);
        return;
    }

    aes_in_buf = asu_aes_preprocess(s, in, mode, enc);

    if (aes_in_buf != NULL) {
        asu_aes_process(s, aes_in_buf, mode, enc);
    }

    asu_aes_postprocess(s, in, mode, enc);

    trace_xilinx_asu_aes_process_block(ASU_AES_MODE_STR[mode],
                                       enc ? "encrypt" : "decrypt");
}

static void finalize_mac(XilinxAsuAesState *s)
{
    bool auth = asu_aes_in_auth_phase(s);
    bool auth_no_data = FIELD_EX32(s->mode_cfg, MODE_CONFIG,
                                   AUTH_WITH_NO_PAYLOAD);
    AsuAesMode mode = get_current_mode(s);

    if (mode == ASU_AES_GHASH) {
        /*
         * The GHASH operation result is available in mac_out, and is also
         * copied in the IV.
         */
        block_copy(s->aes_ctx.iv, s->aes_ctx.mac);
        block_copy((uint8_t *)s->mac_out, s->aes_ctx.mac);
        return;
    }

    if (!s->eop) {
        return;
    }

    if (mode == ASU_AES_CMAC) {
        /*
         * CMAC is handled as a streamed cipher operation, without sending the
         * data into the sink. The result lies into out, simply copy it in
         * mac_out.
         */
        block_copy((uint8_t *)s->mac_out, s->aes_ctx.out);
        return;
    }

    /*
     * Finalize the MAC if we have eop, and we are not in auth phase
     * (auth phase emits its own eop), or if we know we won't have any
     * payload.
     */
    if (auth && !auth_no_data) {
        return;
    }

    switch (mode) {
    case ASU_AES_CCM:
        /* Final CCM mac operation is a xor with S0 */
        block_xor((uint8_t *)s->mac_out,
                  s->aes_ctx.mac, s->aes_ctx.s0_gcmlen);
        break;

    case ASU_AES_GCM:
        {
            AsuAesBlock j0;
            int len_c;

            /*
             * For GCM, the MAC finalization consists in:
             *    mac = ghash(mac, len(A) || len(C))
             *    mac = mac ^ E_k(J0)
             * J0 is the initial counter value. This value is not stored by the
             * controller. We must recover it from the current counter value and
             * len(C).
             */
            ghash(s, s->aes_ctx.mac, s->aes_ctx.mac, s->aes_ctx.s0_gcmlen);
            len_c = block_extract_lo64(s->aes_ctx.s0_gcmlen);

            /*
             * len_c is the size of the cipher in bits. Convert it to the
             * number of AES-GCM iterations done for this computation. Round it
             * up the nearest multiple of 128 as the last block might be
             * partial.
             */
            if (len_c & 127) {
                len_c = (len_c & ~127) + 128;
            }

            len_c /= 128;

            /* len_c + 1 because J1 is the counter value for the first block */
            block_add_i(j0, s->aes_ctx.iv, -(len_c + 1));
            asu_aes_do_encrypt(s, j0, j0);
            block_xor((uint8_t *)s->mac_out, s->aes_ctx.mac, j0);
        }
        break;

    default:
        break;
    }
}

static void send_aes_payload(XilinxAsuAesState *s);

static void sink_notify_cb(void *opaque)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(opaque);

    if (s->ready) {
        /* spurious notify */
        return;
    }

    send_aes_payload(s);
}

static void send_aes_payload(XilinxAsuAesState *s)
{
    if (!stream_can_push(s->sink, sink_notify_cb, s)) {
        s->ready = false;
        return;
    }

    trace_xilinx_asu_aes_send(ASU_AES_BLOCK_SIZE, s->eop);

    stream_push(s->sink, s->aes_ctx.out, ASU_AES_BLOCK_SIZE, s->eop);

    s->ready = true;

    if (s->src_notify_cb) {
        StreamCanPushNotifyFn cb = s->src_notify_cb;

        s->src_notify_cb = NULL;
        cb(s->src_notify_opaque);
    }
}

static void flush_fifo_in(XilinxAsuAesState *s)
{
    g_assert(s->ready);
    g_assert(fifo_in_is_full(s));

    asu_aes_go(s, s->fifo_in, sizeof(s->fifo_in));
    fifo_in_clear(s);

    if (s->eop) {
        raise_done_irq(s);
    }

    if (current_mode_is_streaming(s)) {
        send_aes_payload(s);
    }

    finalize_mac(s);
}

static inline void load_block_with_mask(XilinxAsuAesState *s, AsuAesBlock dst,
                                        const uint32_t *src,
                                        const uint32_t *mask)
{
    if (s->cm_enabled) {
        size_t i;

        for (i = 0; i < ASU_AES_BLOCK_SIZE / sizeof(uint32_t); i++) {
            uint32_t *d = (uint32_t *)(dst + i * sizeof(uint32_t));

            *d = src[i] ^ mask[i];
        }
    } else {
        memcpy(dst, src, ASU_AES_BLOCK_SIZE);
    }
}

static void do_operation(XilinxAsuAesState *s, uint32_t val)
{
    if (FIELD_EX32(val, OPERATION, KEY_LOAD)) {
        size_t key_size;

        key_size = xilinx_asu_kv_get_selected_key(s->kv, s->aes_ctx.key,
                                                  sizeof(s->aes_ctx.key));

        if (!key_size) {
            /*
             * The vault is misconfigured. This is undefined behaviour.
             * Keep a 128 bits key size with a null key in this case.
             */
            s->aes_ctx.key_size = 16;
            memset(s->aes_ctx.key, 0, s->aes_ctx.key_size);
        } else {
            s->aes_ctx.key_size = key_size;
        }

        if (key_split_enabled(s)) {
            uint8_t key_mask[32];
            size_t key_mask_size, i;

            key_mask_size = xilinx_asu_kv_get_key_mask(s->kv, key_mask,
                                                       sizeof(key_mask));
            g_assert(key_size == key_mask_size);

            for (i = 0; i < key_mask_size; i++) {
                s->aes_ctx.key[i] ^= key_mask[i];
            }

        }

        trace_xilinx_asu_aes_load_key(key_size);
        s->aes_ctx.dirty = true;
    }

    if (FIELD_EX32(val, OPERATION, IV_LOAD)) {
        load_block_with_mask(s, s->aes_ctx.iv, s->iv_in, s->iv_mask_in);
        trace_xilinx_asu_aes_load_iv();
    }

    if (FIELD_EX32(val, OPERATION, INTMAC_LOAD)) {
        load_block_with_mask(s, s->aes_ctx.mac,
                             s->int_mac_in, s->int_mac_mask_in);
        memcpy(s->aes_ctx.s0_gcmlen, s->gcmlen_in, sizeof(s->gcmlen_in));
    }

    if (FIELD_EX32(val, OPERATION, S0_LOAD)) {
        load_block_with_mask(s, s->aes_ctx.s0_gcmlen,
                             s->s0_in, s->s0_mask_in);
    }
}

static bool is_zeroized(XilinxAsuAesState *s)
{
    return !s->aes_ctx.dirty;
}

static void do_zeroize(XilinxAsuAesState *s)
{
    memset(&s->aes_ctx, 0, sizeof(s->aes_ctx));
    s->aes_ctx.key_size = 16; /* default to a 128 bits null key */
}

static void do_soft_rst(XilinxAsuAesState *s, bool rst)
{
    s->ready = !rst;
    s->reset = rst;

    if (rst) {
        do_zeroize(s);
        fifo_in_clear(s);

        if (s->src_notify_cb) {
            /*
             * Drain waiting packets and unblock the source stream. The packets
             * will be dropped in asu_aes_recv.
             */
            s->src_notify_cb(s->src_notify_opaque);
            s->src_notify_cb = NULL;
        }
    }
}

static void mode_config_write(XilinxAsuAesState *s, uint32_t val)
{
    bool auth_starting;

    auth_starting = !FIELD_EX32(s->mode_cfg, MODE_CONFIG, AUTH)
        && FIELD_EX32(val, MODE_CONFIG, AUTH);

    s->mode_cfg = val;

    if (!auth_starting) {
        return;
    }

    switch (get_current_mode(s)) {
    case ASU_AES_CCM:
        memset(s->aes_ctx.mac, 0, sizeof(s->aes_ctx.mac));
        asu_aes_do_encrypt(s, s->aes_ctx.s0_gcmlen, s->aes_ctx.iv);
        block_inc(s->aes_ctx.iv);
        break;

    case ASU_AES_GCM:
        memset(s->aes_ctx.mac, 0, sizeof(s->aes_ctx.mac));
        memset(s->aes_ctx.s0_gcmlen, 0, sizeof(s->aes_ctx.mac));
        block_inc(s->aes_ctx.iv);
        break;

    case ASU_AES_GHASH:
        memset(s->aes_ctx.mac, 0, sizeof(s->aes_ctx.mac));
        memset(s->aes_ctx.s0_gcmlen, 0, sizeof(s->aes_ctx.mac));
        break;

    default:
        break;
    }
}

#define BLOCK_READ32_BSWAP(a, idx) \
    bswap32(((uint32_t *)a)[(sizeof(a) / sizeof(uint32_t)) - (idx + 1)])

static uint64_t xilinx_asu_aes_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(opaque);
    uint64_t ret;
    size_t idx;

    switch (addr) {
    case A_IV_IN_0 ... A_IV_IN_3:
        idx = (addr - A_IV_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->iv_in, idx);
        break;

    case A_IV_MASK_IN_0 ... A_IV_MASK_IN_3:
        idx = (addr - A_IV_MASK_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->iv_mask_in, idx);
        break;

    case A_IV_OUT_0 ... A_IV_OUT_3:
        idx = (addr - A_IV_OUT_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->aes_ctx.iv, idx);
        break;

    case A_IV_MASK_OUT_0 ... A_IV_MASK_OUT_3:
        ret = 0;
        break;

    case A_MAC_OUT_0 ... A_MAC_OUT_3:
        idx = (addr - A_MAC_OUT_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->mac_out, idx);
        break;

    case A_MAC_MASK_OUT_0 ... A_MAC_MASK_OUT_3:
        ret = 0;
        break;

    case A_INT_MAC_IN_0 ... A_INT_MAC_IN_3:
        idx = (addr - A_INT_MAC_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->int_mac_in, idx);
        break;

    case A_INT_MAC_MASK_IN_0 ... A_INT_MAC_MASK_IN_3:
        idx = (addr - A_INT_MAC_MASK_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->int_mac_mask_in, idx);
        break;

    case A_INT_MAC_OUT_0 ... A_INT_MAC_OUT_3:
        idx = (addr - A_INT_MAC_OUT_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->aes_ctx.mac, idx);
        break;

    case A_INT_MAC_MASK_OUT_0 ... A_INT_MAC_MASK_OUT_3:
        ret = 0;
        break;

    case A_S0_IN_0 ... A_S0_IN_3:
        idx = (addr - A_S0_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->s0_in, idx);
        break;

    case A_S0_MASK_IN_0 ... A_S0_MASK_IN_3:
        idx = (addr - A_S0_MASK_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->s0_mask_in, idx);
        break;

    case A_S0_OUT_0 ... A_S0_OUT_3:
        idx = (addr - A_S0_OUT_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->aes_ctx.s0_gcmlen, idx);
        break;

    case A_S0_MASK_OUT_0 ... A_S0_MASK_OUT_3:
        ret = 0;
        break;

    case A_GCMLEN_IN_0 ... A_GCMLEN_IN_3:
        idx = (addr - A_GCMLEN_IN_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->gcmlen_in, idx);
        break;

    case A_GCMLEN_OUT_0 ... A_GCMLEN_OUT_3:
        idx = (addr - A_GCMLEN_OUT_0) / sizeof(uint32_t);
        ret = BLOCK_READ32_BSWAP(s->aes_ctx.s0_gcmlen, idx);
        break;

    case A_STATUS:
        ret = FIELD_DP32(0, STATUS, BUSY, s->reset);
        ret = FIELD_DP32(ret, STATUS, READY, s->ready);
        break;

    case A_SPLIT_CFG:
        ret = s->split_cfg;
        break;

    case A_MODE_CONFIG:
        ret = s->mode_cfg;
        break;

    case A_SOFT_RST:
        ret = s->reset;
        break;

    case A_INTERRUPT_MASK:
        ret = s->irq_mask;
        break;

    case A_INTERRUPT_STATUS:
        ret = s->irq_sta;
        break;

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

#undef BLOCK_READ32_BSWAP

#define BLOCK_WRITE32_BSWAP(a, idx, val) \
    ((uint32_t *)a)[(sizeof(a) / sizeof(uint32_t)) - ((idx) + 1)] = bswap32(val)

static void xilinx_asu_aes_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(opaque);
    size_t idx;

    trace_xilinx_asu_aes_write(addr, value, size);

    switch (addr) {
    case A_IV_IN_0 ... A_IV_IN_3:
        idx = (addr - A_IV_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->iv_in, idx, value);
        break;

    case A_IV_MASK_IN_0 ... A_IV_MASK_IN_3:
        idx = (addr - A_IV_MASK_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->iv_mask_in, idx, value);
        break;

    case A_INT_MAC_IN_0 ... A_INT_MAC_IN_3:
        idx = (addr - A_INT_MAC_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->int_mac_in, idx, value);
        break;

    case A_INT_MAC_MASK_IN_0 ... A_INT_MAC_MASK_IN_3:
        idx = (addr - A_INT_MAC_MASK_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->int_mac_mask_in, idx, value);
        break;

    case A_S0_IN_0 ... A_S0_IN_3:
        idx = (addr - A_S0_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->s0_in, idx, value);
        break;

    case A_S0_MASK_IN_0 ... A_S0_MASK_IN_3:
        idx = (addr - A_S0_MASK_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->s0_mask_in, idx, value);
        break;

    case A_GCMLEN_IN_0 ... A_GCMLEN_IN_3:
        idx = (addr - A_GCMLEN_IN_0) / sizeof(uint32_t);
        BLOCK_WRITE32_BSWAP(s->gcmlen_in, idx, value);
        break;

    case A_CM:
        s->cm_enabled = (value == R_CM_ENABLE_MASK);
        break;

    case A_SPLIT_CFG:
        s->split_cfg = value & SPLIT_CFG_WRITE_MASK;
        break;

    case A_MODE_CONFIG:
        mode_config_write(s, value & MODE_CONFIG_WRITE_MASK);
        break;

    case A_SOFT_RST:
        do_soft_rst(s, value & 0x1);
        break;

    case A_INTERRUPT_STATUS:
        s->irq_sta &= ~(value & 0x1);
        update_irq(s);
        break;

    case A_INTERRUPT_ENABLE:
        s->irq_mask &= ~(value & 0x1);
        update_irq(s);
        break;

    case A_INTERRUPT_DISABLE:
        s->irq_mask |= value & 0x1;
        update_irq(s);
        break;

    case A_INTERRUPT_TRIGGER:
        if (value & 0x1) {
            raise_done_irq(s);
        }
        break;

    case A_OPERATION:
        do_operation(s, value);
        break;

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

#undef BLOCK_WRITE32_BSWAP


static const MemoryRegionOps xilinx_asu_aes_ops = {
    .read = xilinx_asu_aes_read,
    .write = xilinx_asu_aes_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static size_t asu_aes_recv(StreamSink *obj, uint8_t *data,
                           size_t len, bool eop)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(obj);
    size_t to_push, pushed = 0;

    trace_xilinx_asu_aes_recv(len, eop);

    if (s->reset) {
        /* drop the packet */
        trace_xilinx_asu_aes_drop(len, eop);
        return len;
    }

    g_assert(s->ready);

    while (len) {
        bool ghash_padding = false;

        to_push = MIN(len, fifo_in_num_free(s));
        s->eop = false;
        s->pad_amount = 0;

        fifo_in_push(s, data, to_push);

        if (len == to_push) {
            /*
             * Last piece of the received packet. Reflect the value of EOP in
             * the packet we're going to send in our sink stream. If eop is
             * actually set, pad the FIFO.
             */
            s->eop = eop;
            if (eop) {
                ghash_padding = fifo_in_pad(s);
            }
        }

        if (fifo_in_is_full(s)) {
            flush_fifo_in(s);

            if (ghash_padding) {
                /*
                 * Insert the extra padding block.
                 */
                fifo_in_pad_ghash_final_block(s);
                g_assert(fifo_in_is_full(s));
                flush_fifo_in(s);
            }
        }

        len -= to_push;
        data += to_push;
        pushed += to_push;
    }

    return pushed;
}

static bool asu_aes_can_recv(StreamSink *obj,
                             StreamCanPushNotifyFn notify,
                             void *notify_opaque)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(obj);

    if (!s->reset && !s->ready) {
        s->src_notify_cb = notify;
        s->src_notify_opaque = notify_opaque;
        return false;
    }

    return true;
}

static void xilinx_asu_aes_reset_enter(Object *obj, ResetType type)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(obj);

    memset(s->iv_in, 0, sizeof(s->iv_in));
    memset(s->iv_mask_in, 0, sizeof(s->iv_mask_in));
    memset(s->mac_out, 0, sizeof(s->mac_out));
    memset(s->int_mac_in, 0, sizeof(s->int_mac_in));
    memset(s->int_mac_mask_in, 0, sizeof(s->int_mac_mask_in));
    memset(s->s0_in, 0, sizeof(s->s0_in));
    memset(s->s0_mask_in, 0, sizeof(s->s0_mask_in));
    memset(s->gcmlen_in, 0, sizeof(s->gcmlen_in));
    s->mode_cfg = 0;
    s->split_cfg = 0;
    s->irq_mask = true;
    s->irq_sta = false;
    s->cm_enabled = true;
}

static void xilinx_asu_aes_reset_hold(Object *obj)
{
    XilinxAsuAesState *s = XILINX_ASU_AES(obj);

    do_soft_rst(s, true);
    update_irq(s);
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
    DEFINE_PROP_LINK("stream-connected-aes",
                     XilinxAsuAesState, sink,
                     TYPE_STREAM_SINK, StreamSink *),
    DEFINE_PROP_LINK("keyvault",
                     XilinxAsuAesState, kv,
                     TYPE_XILINX_ASU_KV, XilinxAsuKvState *),
    DEFINE_PROP_END_OF_LIST()
};

static void xilinx_asu_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
    XilinxAsuAesClass *xaac = XILINX_ASU_AES_CLASS(klass);

    dc->realize = xilinx_asu_aes_realize;
    rc->phases.enter = xilinx_asu_aes_reset_enter;
    rc->phases.hold = xilinx_asu_aes_reset_hold;
    ssc->push = asu_aes_recv;
    ssc->can_push = asu_aes_can_recv;
    xaac->do_zeroize = do_zeroize;
    xaac->is_zeroized = is_zeroized;
    device_class_set_props(dc, xilinx_asu_aes_properties);
}

static const TypeInfo xilinx_asu_aes_info = {
    .name = TYPE_XILINX_ASU_AES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxAsuAesState),
    .class_init = xilinx_asu_aes_class_init,
    .class_size = sizeof(XilinxAsuAesClass),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    },
};

static void xilinx_asu_aes_register_types(void)
{
    type_register_static(&xilinx_asu_aes_info);
}

type_init(xilinx_asu_aes_register_types)
