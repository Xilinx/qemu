/*
 * QEMU model of the Xilinx ASU AES computation engine
 * implemented in gcrypt.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "xlnx-asu-aes-impl.h"

#include "qemu/log.h"
#include "qemu/error-report.h"

#include <gcrypt.h>

#define GCRY_GUEST_ERROR(s, e, FMT, ...) do \
    {   if (e) {                                                        \
            ASU_AES_GUEST_ERROR((s), FMT " failure: %s",                \
                                ## __VA_ARGS__, gcry_strerror(e));      \
        } else {                                                        \
            ASU_AES_GUEST_ERROR((s), FMT, ## __VA_ARGS__);              \
        }                                                               \
    } while (0)

#define GCRY_CODE_ERROR(FN, s, FMT, ...) do {                           \
        g_autofree char *_dev = object_get_canonical_path(OBJECT(s));   \
        error_report("%s:%u:%s - %s() for %s failed: " FMT, __FILE__,   \
                     __LINE__, __func__, #FN, _dev, ## __VA_ARGS__);    \
    } while (0)

#define GCRY_CALL_ERROR(e, FN, s)                                       \
    ({  bool bad = !!(e);                                               \
        if (bad) {                                                      \
            GCRY_CODE_ERROR(FN, (s), "%s", gcry_strerror(e));           \
        }                                                               \
        bad;                                                            \
     })

#define GCRY_FAILED (true)
#define GCRY_OK (false)

static bool asu_gcry_in_gcm(XlnxAsuAes *s)
{
    return s->cipher.mode == ASU_AES_MODE_GCM;
}

static bool asu_gcry_in_ccm(XlnxAsuAes *s)
{
    return s->cipher.mode == ASU_AES_MODE_CCM;
}

static bool asu_gcry_in_cmac(XlnxAsuAes *s)
{
    return s->cipher.mode == ASU_AES_MODE_CMAC;
}

/*
 * gcrypt API provides NIST SP800-38B (ASU_AES_MODE_CMAC) as mac,
 * instead of being a mode in its AES cipher API.
 */
static bool asu_gcry_cmac_release(XlnxAsuAes *s)
{
    gcry_mac_close((gcry_mac_hd_t)s->cipher.cntx);
    s->cipher.cntx = NULL;

    return false;
}

static bool asu_gcry_cmac_init(XlnxAsuAes *s)
{
    int algo = GCRY_MAC_CMAC_AES;
    unsigned maclen;

    gcry_mac_hd_t h;
    gcry_error_t e;

    if (s->cipher.cntx) {
        GCRY_CODE_ERROR(asu_gcry_cmac_init, s, "Staled handle");
    }

    e = gcry_mac_open(&h, algo, 0, NULL);
    if (GCRY_CALL_ERROR(e, gcry_mac_open, s)) {
        return GCRY_FAILED;
    }
    s->cipher.cntx = h;

    maclen = gcry_mac_get_algo_maclen(algo);
    if (maclen != ASU_AES_MACLEN) {
        GCRY_CODE_ERROR(gcry_mac_get_algo_maclen, s,
                        "GCRY_MAC_CMAC_AES: - "
                        "maclen(%u) != sizeof(mac_out:%u)",
                        maclen, ASU_AES_MACLEN);
        return GCRY_FAILED;
    }

    e = gcry_mac_setkey(h, asu_aes_key_in(s), asu_aes_key_in_len(s));
    if (GCRY_CALL_ERROR(e, gcry_mac_setkey, s)) {
        return GCRY_FAILED;
    }

    return GCRY_OK;
}

static bool asu_gcry_cmac_text(XlnxAsuAes *s, size_t len, const void *din)
{
    gcry_mac_hd_t h = s->cipher.cntx;
    gcry_error_t e;
    size_t maclen;

    if (!h) {
        GCRY_CODE_ERROR(asu_gcry_cmac_write, s, "No handle");
        return GCRY_FAILED;
    }

    if (len) {
        e = gcry_mac_write(h, din, len);
        if (GCRY_CALL_ERROR(e, gcry_mac_write, s)) {
            return GCRY_FAILED;
        }
    }

    if (s->cipher.fin_phase) {
        e = gcry_mac_read(h, s->cipher.be_mac_out, &maclen);
        if (GCRY_CALL_ERROR(e, gcry_mac_read, s)) {
            return GCRY_FAILED;
        }

        s->cipher.mac_valid = true;
    } else {
        s->cipher.mac_valid = false;
    }

    return GCRY_OK;
}

static bool asu_gcry_cmac(XlnxAsuAes *s, unsigned op, size_t len,
                          const void *din)
{
    bool e;

    switch (op) {
    case ASU_AES_RESET:
        e = asu_gcry_cmac_release(s);
        break;
    case ASU_AES_INIT:
        e = asu_gcry_cmac_init(s);
        break;
    case ASU_AES_TEXT:
        e = asu_gcry_cmac_text(s, len, din);
        break;
    default:
        GCRY_CODE_ERROR(asu_gcry_cmac, s, "Unsupported op %u", op);
        e = GCRY_FAILED;
    }

    if (e || s->cipher.fin_phase) {
        asu_gcry_cmac_release(s);
    }

    return e;
}

/*
 * Map to gcrypt AES cipher modes.
 */
static const int asu_gcry_aes_mode[] = {
    [ASU_AES_MODE_CBC] = GCRY_CIPHER_MODE_CBC,
    [ASU_AES_MODE_CFB] = GCRY_CIPHER_MODE_CFB,
    [ASU_AES_MODE_OFB] = GCRY_CIPHER_MODE_OFB,
    [ASU_AES_MODE_CTR] = GCRY_CIPHER_MODE_CTR,
    [ASU_AES_MODE_ECB] = GCRY_CIPHER_MODE_ECB,
    [ASU_AES_MODE_CCM] = GCRY_CIPHER_MODE_CCM,
    [ASU_AES_MODE_GCM] = GCRY_CIPHER_MODE_GCM,
};

/*
 * Use helper to obtain IV-out the hard way, because there is no
 * public gcrypt API to get it.
 */
#include "xlnx-asu-aes-util-ivout.c.inc"

/*
 * Use helper to extract CCM config.
 */
#include "xlnx-asu-aes-util-ccm.c.inc"

static bool asu_gcry_aes_release(XlnxAsuAes *s)
{
    gcry_cipher_close((gcry_cipher_hd_t)s->cipher.cntx);
    s->cipher.cntx = NULL;

    return GCRY_OK;
}

static int asu_gcry_aes_algo(const void *cipher_key)
{
    return asu_aes_k128(cipher_key) ? GCRY_CIPHER_AES128 : GCRY_CIPHER_AES256;
}

static bool asu_gcry_aes_blk(XlnxAsuAes *s, bool enc, void *b, const void *ck)
{
    int algo = asu_gcry_aes_algo(ck);
    int klen = asu_aes_klen(ck);
    const void *key = asu_aes_kptr(ck);

    gcry_error_t e;
    gcry_cipher_hd_t h;

    e = gcry_cipher_open(&h, algo, GCRY_CIPHER_MODE_ECB, 0);
    if (GCRY_CALL_ERROR(e, gcry_cipher_open, s)) {
        h = NULL;
        goto failed;
    }

    e = gcry_cipher_setkey(h, key, klen);
    if (GCRY_CALL_ERROR(e, gcry_cipher_setkey, s)) {
        goto failed;
    }

    if (enc) {
        e = gcry_cipher_encrypt(h, b, ASU_AES_BLKLEN, NULL, 0);
        if (GCRY_CALL_ERROR(e, gcry_cipher_encrypt, s)) {
            goto failed;
        }
    } else {
        e = gcry_cipher_decrypt(h, b, ASU_AES_BLKLEN, NULL, 0);
        if (GCRY_CALL_ERROR(e, gcry_cipher_decrypt, s)) {
            goto failed;
        }
    }

    gcry_cipher_close(h);
    return false;

 failed:
    gcry_cipher_close(h);
    return true;
}

static bool asu_gcry_ctr_decrypt(XlnxAsuAes *s, void *ctr)
{
    return asu_gcry_aes_blk(s, false, ctr, s->cipher.be_key_out);
}

static bool asu_gcry_ccm_setup(XlnxAsuAes *s, uint64_t plen, uint64_t alen,
                               unsigned tlen, int nlen, const void *nonce)
{
    gcry_cipher_hd_t h = s->cipher.cntx;
    gcry_error_t e;
    uint64_t cfg[3];
    size_t taglen;

    if (nlen <= 0) {
        GCRY_GUEST_ERROR(s, 0, "CCM aad.b0.qlen is invalid: %d", -nlen);
        return GCRY_FAILED;
    }

    e = gcry_cipher_setiv(h, nonce, nlen);
    if (GCRY_CALL_ERROR(e, gcry_cipher_setiv, s)) {
        return GCRY_FAILED;
    }

    cfg[0] = plen;
    cfg[1] = alen;
    cfg[2] = tlen;
    e = gcry_cipher_ctl(h, GCRYCTL_SET_CCM_LENGTHS, cfg, sizeof(cfg));
    if (GCRY_CALL_ERROR(e, gcry_cipher_ctl, s)) {
        return GCRY_FAILED;
    }

    e = gcry_cipher_info(h, GCRYCTL_GET_TAGLEN, NULL, &taglen);
    if (GCRY_CALL_ERROR(e, gcry_cipher_info, s)) {
        return GCRY_FAILED;
    }

    if (taglen != tlen) {
        GCRY_CODE_ERROR(gcry_cipher_info, s,
                        "mode: %u - "
                        "GET_TAGLEN(%zu) != tlen(%u)",
                        s->cipher.mode, taglen, tlen);
        return GCRY_FAILED;
    }

    return GCRY_OK;
}

static size_t asu_gcry_aes_ccm_prepared(XlnxAsuAes *s,
                                        size_t len, const void *din)
{
    ssize_t next;

    /*
     * For CCM, need to extract <nonce, alen, plen> from B0 and
     * B1, because gcrypt requires them to be set explicitly.
     */
    if (!asu_gcry_in_ccm(s)) {
        return 0;
    }

    next = asu_aes_ccm_parse(s, len, din, asu_gcry_ccm_setup);
    if (next >= 0) {
        return next;
    }

    GCRY_GUEST_ERROR(s, 0, "CCM B0/B1 encoding error");
    s->cipher.in_error = true;
    return len;
}

static int asu_gcry_aes_mac_length(XlnxAsuAes *s)
{
    gcry_cipher_hd_t h = s->cipher.cntx;
    gcry_error_t e;
    size_t tlen;

    switch (s->cipher.mode) {
    case ASU_AES_MODE_CCM:
        break;
    case ASU_AES_MODE_GCM:
        return ASU_AES_MACLEN;
    default:
        return 0;
    }

    /* Unavailable due to already in error state */
    if (s->cipher.in_error) {
        return -1;
    }

    /* Unavaiable due to missing AAD B0 */
    e = gcry_cipher_info(h, GCRYCTL_GET_TAGLEN, NULL, &tlen);
    if (GCRY_CALL_ERROR(e, gcry_cipher_info, s)) {
        return -2;
    }

    /* Unavaiable due to aad and/or text amount not as configured */
    if (s->cipher.aad_used != s->cipher.aad_bmax) {
        GCRY_GUEST_ERROR(s, 0, "CCM AAD amount not as configured: "
                         "given 0x%llx, need 0x%llx",
                         (unsigned long long)s->cipher.aad_used,
                         (unsigned long long)s->cipher.aad_bmax);
        return -2;
    }

    if (s->cipher.txt_used != s->cipher.txt_bmax) {
        GCRY_GUEST_ERROR(s, 0, "CCM TEXT amount not as configured: "
                         "give 0x%llx, need 0x%llx",
                         (unsigned long long)s->cipher.txt_used,
                         (unsigned long long)s->cipher.txt_bmax);
        return -2;
    }

    return (int)tlen;
}

static bool asu_gcry_aes_mac_latch(XlnxAsuAes *s)
{
    gcry_cipher_hd_t h = s->cipher.cntx;
    gcry_error_t e;
    void *tag;
    int tlen;

    tlen = asu_gcry_aes_mac_length(s);
    if (!tlen) {
        s->cipher.mac_valid = false;
        return GCRY_OK;
    }

    ASU_AES_BZERO(s->cipher.be_mac_out);
    s->cipher.mac_valid = true;

    if (tlen < 0) {
        return tlen == -1 ? GCRY_OK : GCRY_FAILED;
    }

    tag = s->cipher.be_mac_out + sizeof(s->cipher.be_mac_out) - tlen;

    e = gcry_cipher_gettag(h, tag, tlen);
    if (GCRY_CALL_ERROR(e, gcry_cipher_gettag, s)) {
        return GCRY_FAILED;
    }

    return GCRY_OK;
}

static bool asu_gcry_aes_gcm_latched(XlnxAsuAes *s,
                                     size_t len, const void *din)
{
    uint64_t aad_len, txt_len;

    /*
     * For GCM, need to detect the unconventional approach used by
     * ASU AES GCM engine to trigger calculation of GCM-tag, of
     * sending a 128-bit block of {uint64(aad_len), int64(txt_len)}.
     *
     * Gcrypt-GCM keeps track of both lengths internally, and used
     * to calculate the GCM-tag when gettag is called.
     */
    if (!asu_gcry_in_gcm(s)) {
        return false;
    }

    /* Must be a whole aad-block sent with EOP indication */
    if (len != ASU_AES_BLKLEN || !s->cipher.fin_phase) {
        return false;
    }

    /* Must match aad length processed */
    aad_len = ldq_be_p(din);
    if (aad_len != s->cipher.aad_bcnt) {
        return false;
    }

    /* Must match text length processed */
    txt_len = ldq_be_p(din + 8);
    if (txt_len != s->cipher.txt_bcnt) {
        return false;
    }

    if (!s->cipher.in_error && asu_gcry_aes_mac_latch(s)) {
        s->cipher.in_error = true;
    }

    /* Error or success, it is a FIN */
    asu_gcry_aes_release(s);
    return true;
}

static bool asu_gcry_aes_gcm_iv_load(XlnxAsuAes *s)
{
    gcry_cipher_hd_t h = s->cipher.cntx;
    gcry_error_t e;

    /*
     * In GCM mode, ASU-AES IV registers are actually expected to
     * be loaded with 128-bit J0 (see sp800-38d, 7.1, step 2),
     * something gcrypt does not take.
     *
     * So, for now, just support 96-bit IV.
     */
    if (ldl_be_p(s->cipher.be_iv_in + (96 / 8)) != 1) {
        GCRY_CODE_ERROR(s, 0, "GCM J0 is not a 96-bit IV and unsupported");
        return GCRY_FAILED;
    }

    e = gcry_cipher_setiv(h, s->cipher.be_iv_in, (96 / 8));
    if (GCRY_CALL_ERROR(e, gcry_cipher_setiv, s)) {
        return GCRY_FAILED;
    }

    return GCRY_OK;
}

static bool asu_gcry_aes_init(XlnxAsuAes *s)
{
    int mode = s->cipher.mode;
    int algo = asu_gcry_aes_algo(s->cipher.be_key_in);
    size_t taglen;

    gcry_cipher_hd_t h;
    gcry_error_t e;

    /* Clear out previous session, if any */
    asu_gcry_aes_release(s);

    if (mode >= ARRAY_SIZE(asu_gcry_aes_mode)) {
        mode = 0;
    } else {
        mode = asu_gcry_aes_mode[mode];
    }
    if (!mode) {
        GCRY_GUEST_ERROR(s, 0, "Unsupported cipher mode %u", s->cipher.mode);
        return GCRY_FAILED;
    }

    if (s->cipher.cntx) {
        GCRY_CODE_ERROR(asu_gcry_aes_init, s, "Staled handle");
    }

    e = gcry_cipher_open(&h, algo, mode, 0);
    if (GCRY_CALL_ERROR(e, gcry_cipher_open, s)) {
        return GCRY_FAILED;
    }
    s->cipher.cntx = h;

    if (asu_gcry_in_gcm(s)) {
        e = gcry_cipher_info(h, GCRYCTL_GET_TAGLEN, NULL, &taglen);
        if (GCRY_CALL_ERROR(e, gcry_cipher_info, s)) {
            return GCRY_FAILED;
        }

        if (taglen != ASU_AES_MACLEN) {
            GCRY_CODE_ERROR(gcry_cipher_info, s,
                            "mode: %u - "
                            "GET_TAGLEN(%zu) != sizeof(mac_out:%u)",
                            s->cipher.mode, taglen, ASU_AES_MACLEN);
            return GCRY_FAILED;
        }
    }

    /* Key must be set prior to IV */
    e = gcry_cipher_setkey(h, asu_aes_key_in(s), asu_aes_key_in_len(s));
    if (GCRY_CALL_ERROR(e, gcry_cipher_setkey, s)) {
        return GCRY_FAILED;
    }

    switch (s->cipher.mode) {
    case ASU_AES_MODE_CCM:
        /*
         * CCM IV is actually nonce, and its length is required yet
         * message dependent.  ASU AES CCM-engine expects the nonce
         * embedded in B0 in AAD phase (see asu_gcry_aes_aead() below).
         */
        break;
    case ASU_AES_MODE_GCM:
        if (asu_gcry_aes_gcm_iv_load(s)) {
            return GCRY_FAILED;
        }
        break;
    case ASU_AES_MODE_CTR:
        e = gcry_cipher_setctr(h, s->cipher.be_iv_in, ASU_AES_IVLEN);
        if (GCRY_CALL_ERROR(e, gcry_cipher_setctr, s)) {
            return GCRY_FAILED;
        }
        break;
    default:
        e = gcry_cipher_setiv(h, s->cipher.be_iv_in, ASU_AES_IVLEN);
        if (GCRY_CALL_ERROR(e, gcry_cipher_setiv, s)) {
            return GCRY_FAILED;
        }
    }

    /* Remember the in-use key and klen, mainly for counter recovery */
    asu_aes_kdup(s->cipher.be_key_out, s->cipher.be_key_in);
    return GCRY_OK;
}

static bool asu_gcry_aes_aead(XlnxAsuAes *s, size_t len, const void *din)
{
    gcry_cipher_hd_t h = s->cipher.cntx;
    gcry_error_t e;
    size_t pos;

    if (s->cipher.in_error) {
        return GCRY_OK;
    }

    ASU_AES_BUG(asu_aes_no_aad(s));

    if (!h) {
        GCRY_CODE_ERROR(asu_gcry_aes_aead, s, "No handle");
        return GCRY_FAILED;
    }

    if (asu_gcry_aes_gcm_latched(s, len, din)) {
        return s->cipher.in_error ? GCRY_FAILED : GCRY_OK;
    }

    pos = asu_gcry_aes_ccm_prepared(s, len, din);
    if (pos == len) {
        return s->cipher.in_error ? GCRY_FAILED : GCRY_OK;
    }
    din += pos;
    len -= pos;

    e = gcry_cipher_authenticate(h, din, len);
    if (GCRY_CALL_ERROR(e, gcry_cipher_authenticate, s)) {
        return GCRY_FAILED;
    }
    s->cipher.aad_used += len;

    if (!s->cipher.txt_phase && s->cipher.txt_bmax != 0) {
        /* Mask off the FIN used only to deliver partial block of aad */
        s->cipher.fin_phase = false;
    }

    if (!s->cipher.fin_phase) {
        return GCRY_OK;
    }

    e = gcry_cipher_final(h);
    if (GCRY_CALL_ERROR(e, gcry_cipher_final, s)) {
        return GCRY_FAILED;
    }

    return asu_gcry_aes_mac_latch(s);
}

static bool asu_gcry_aes_text(XlnxAsuAes *s, size_t len,
                              const void *din, void *dout)
{
    gcry_cipher_hd_t h = s->cipher.cntx;
    gcry_error_t e;

    if (s->cipher.in_error) {
        memset(dout, 0, len);
        return GCRY_OK;
    }

    if (!h) {
        GCRY_CODE_ERROR(asu_gcry_aes_text, s, "No handle for %s",
                        (s->cipher.enc ? "enc" : "dec"));
        return GCRY_FAILED;
    }

    if (s->cipher.fin_phase) {
        e = gcry_cipher_final(h);
        if (GCRY_CALL_ERROR(e, gcry_cipher_final, s)) {
            return GCRY_FAILED;
        }
    }

    if (s->cipher.enc) {
        e = gcry_cipher_encrypt(h, dout, len, din, len);
        if (GCRY_CALL_ERROR(e, gcry_cipher_encrypt, s)) {
            return GCRY_FAILED;
        }
    } else {
        e = gcry_cipher_decrypt(h, dout, len, din, len);
        if (GCRY_CALL_ERROR(e, gcry_cipher_decrypt, s)) {
            return GCRY_FAILED;
        }
    }
    s->cipher.txt_used += len;

    asu_aes_ivout(s, len, din, dout, asu_gcry_ctr_decrypt);

    if (!s->cipher.fin_phase) {
        return GCRY_OK;
    }

    /*
     * GCM must defer getting the auth-tag until the funny AUTH-block
     * is received (see asu_gcry_aes_aead() above).
     */
    if (asu_gcry_in_gcm(s)) {
        s->cipher.fin_phase = false;
        return GCRY_OK;
    }

    return asu_gcry_aes_mac_latch(s);
}

static bool asu_gcry_aes(XlnxAsuAes *s, unsigned op, size_t len,
                         const void *din, void *dout)
{
    bool e;

    switch (op) {
    case ASU_AES_RESET:
        e = asu_gcry_aes_release(s);
        break;
    case ASU_AES_INIT:
        e = asu_gcry_aes_init(s);
        break;
    case ASU_AES_AEAD:
        e = asu_gcry_aes_aead(s, len, din);
        break;
    case ASU_AES_TEXT:
        e = asu_gcry_aes_text(s, len, din, dout);
        break;
    default:
        GCRY_CODE_ERROR(asu_gcry_aes, s, "Unsupported op %u", op);
        e = GCRY_FAILED;
    }

    /*
     * For GCM, FIN is truly reached only after the funny AUTH-block
     * is received.
     */
    if (!e && asu_gcry_in_gcm(s)) {
        return GCRY_OK;
    }

    if (e || s->cipher.fin_phase) {
        asu_gcry_aes_release(s);
    }

    return e;
}

static bool asu_gcry_cipher(XlnxAsuAes *s, unsigned op, size_t len,
                            const void *din, void *dout)
{
    bool e;

    switch (op) {
    case ASU_AES_RESET:
    case ASU_AES_INIT:
        s->cipher.in_error = false;
    }

    if (asu_gcry_in_cmac(s)) {
        e = asu_gcry_cmac(s, op, len, din);
    } else {
        e = asu_gcry_aes(s, op, len, din, dout);
    }

    if (e) {
        s->cipher.in_error = true;
    }

    return e;
}

static void __attribute__((constructor)) asu_gcry_cipher_bind(void)
{
    /* Initialize libgcrypt, so that FIPS mode can be detected */
    gcry_check_version(NULL);

    /* Install cipher into ASU-AES controller */
    xlnx_asu_aes_cipher_bind(asu_gcry_cipher);
}
