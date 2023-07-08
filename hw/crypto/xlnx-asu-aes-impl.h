/*
 * QEMU model of the Xilinx ASU AES computation engine implementation.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef XLNX_ASU_AES_IMPL_H
#define XLNX_ASU_AES_IMPL_H

#include "hw/crypto/xlnx-asu-aes.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

enum {
    /* Confidentiality only modes: plain-text <=> cipher text */
    ASU_AES_MODE_CBC = 0,
    ASU_AES_MODE_CFB = 1,
    ASU_AES_MODE_OFB = 2,
    ASU_AES_MODE_CTR = 3,
    ASU_AES_MODE_ECB = 4,
    /* Confidentiality+Authenticity modes */
    ASU_AES_MODE_CCM = 5,
    ASU_AES_MODE_GCM = 6,
    /* Autheticity only mode */
    ASU_AES_MODE_CMAC = 8,

    /* Operation */
    ASU_AES_RESET = 0x100,
    ASU_AES_INIT,
    ASU_AES_AEAD,
    ASU_AES_TEXT,

    /* Other constants */
    ASU_AES_U8_256 = 256 / 8,
    ASU_AES_U8_128 = 128 / 8,
    ASU_AES_BLKLEN = sizeof_field(XlnxAsuAes, partial),
    ASU_AES_MACLEN = sizeof_field(XlnxAsuAes, cipher.be_mac_out),
    ASU_AES_IVLEN  = sizeof_field(XlnxAsuAes, cipher.be_iv_in),
};

/**
 * xlnx_asu_aes_cipher_t:
 *
 * Execute specified @op cipher operations, using auxiliary
 * data and states in device @s, for @len bytes of data in
 * @din, with output of @len bytes of data in @dout.
 *
 * For ASU_AES_AEAD and ASU_AES_TEXT, @din actual size is
 * ROUND_UP(len, 16), with the extra space all padded with 0s.
 *
 * For ASU_AES_TEXT, @dout actual size is ROUND_UP(len, 16)
 * and is safe to write over the extra space.
 *
 * Returns: true if error occurred.
 */
typedef bool (*xlnx_asu_aes_cipher_t)(XlnxAsuAes *s, unsigned op, size_t len,
                                      const void *din, void *dout);

/**
 * xlnx_asu_aes_cipher_bind:
 *
 * Bind @cipher into the ASU-AES controller.
 */
void xlnx_asu_aes_cipher_bind(xlnx_asu_aes_cipher_t cipher);

/*
 * Catch bug-caused (and not guest error) conditions,
 * even in released code.
 */
#define ASU_AES_BUG(c) \
    do {                                              \
        if (c) {                                      \
            error_setg(&error_abort, "Bug: %s", #c);  \
        }                                             \
    } while (0)

/*
 * Log guest usage error.
 */
#define ASU_AES_GUEST_ERROR(s, FMT, ...) do \
    {   g_autofree char *_dev = object_get_canonical_path(OBJECT(s));     \
        qemu_log_mask(LOG_GUEST_ERROR, "%s: " FMT, _dev, ## __VA_ARGS__); \
        if (s->noisy_gerr) {                                              \
            error_report("%s: " FMT, _dev, ## __VA_ARGS__);               \
        }                                                                 \
    } while (0)

/*
 * Helpers
 */
#define ASU_AES_NZERO(p, n) memset((p),   0, (n))
#define ASU_AES_BZERO(b)    memset((b),   0, sizeof(b))
#define ASU_AES_PZERO(p)    memset((p),   0, sizeof(*(p)))
#define ASU_AES_IZERO(i)    memset((i),   0, ASU_AES_IVLEN)
#define ASU_AES_MZERO(i)    memset((i),   0, ASU_AES_MACLEN)
#define ASU_AES_KZERO(k)    memset((k),   0, ASU_AES_U8_256)
#define ASU_AES_KCOPY(d, s) memcpy((d), (s), ASU_AES_U8_256)

/**
 * asu_aes_set_klen:
 *
 * Clear a key in cipher-context then set its length, which can
 * be used later to get filled in.
 *
 * Return: A copy of length is returned; -@sel if @sel is invalid.
 */
static inline int asu_aes_set_klen(void *cipher_key, int sel)
{
    switch (sel) {
    case 0:
    case 2:
        break;
    default:
        return -sel;
    }

    sel = ASU_AES_U8_128 * (1 + sel / 2);
    if (cipher_key) {
        ASU_AES_KZERO(cipher_key);
        *(uint8_t *)(cipher_key + ASU_AES_U8_256) = sel;
    }

    return sel;
}

/**
 * asu_aes_klen:
 *
 * Return: the length, in bytes, of a key in cipher-context.
 */
static inline unsigned asu_aes_klen(const void *cipher_key)
{
    return *(const uint8_t *)(cipher_key + ASU_AES_U8_256);
}

/**
 * asu_aes_kptr:
 *
 * Return: pointer to the 1st byte of a key in cipher-context.
 */
static inline void *asu_aes_kptr(const void *cipher_key)
{
    cipher_key += ASU_AES_U8_256 - asu_aes_klen(cipher_key);

    return (void *)(uintptr_t)cipher_key;
}

/**
 * asu_aes_kdup:
 *
 * Copy all bits plus len of a key in cipher-context.
 */
static inline void asu_aes_kdup(void *dst, const void *src)
{
    memcpy(dst, src, ASU_AES_U8_256 + 1);
}

/**
 * asu_aes_k256:
 *
 * Return: true if length of a key in cipher-context is 256 bits.
 */
static inline bool asu_aes_k256(const void *cipher_key)
{
    return asu_aes_klen(cipher_key) == ASU_AES_U8_256;
}

/**
 * asu_aes_k128:
 *
 * Return: true if length of a key in cipher-context is 128 bits.
 */
static inline bool asu_aes_k128(const void *cipher_key)
{
    return asu_aes_klen(cipher_key) == ASU_AES_U8_128;
}

/**
 * asu_aes_klen:
 *
 * Return: the length, in bytes, of the big-endian cipher key from @s
 */
static inline unsigned asu_aes_key_in_len(XlnxAsuAes *s)
{
    return asu_aes_klen(s->cipher.be_key_in);
}

/**
 * asu_aes_key:
 *
 * Return: the start of big-endian key from @s
 */
static inline void *asu_aes_key_in(XlnxAsuAes *s)
{
    return asu_aes_kptr(s->cipher.be_key_in);
}

/**
 * asu_aes_no_aad:
 *
 * Return: true if the device @s current cipher imode does not allow AAD.
 */
static inline bool asu_aes_no_aad(XlnxAsuAes *s)
{
    switch (s->cipher.mode) {
    case ASU_AES_MODE_CCM:
    case ASU_AES_MODE_GCM:
        return false;
    default:
        return true;
    }
}

/**
 * asu_aes_is_blk:
 *
 * Return: true if given length is integer multiple of block size
 */
static inline bool asu_aes_is_blk(uint64_t v)
{
    return !(v % ASU_AES_BLKLEN);
}

#endif
