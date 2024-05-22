/*
 * QEMU Crypto ECDSA algorithm - libgcrypt implementation
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Author: Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include <gcrypt.h>
#include "crypto/ecdsa-priv.h"

G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gcry_sexp_t, gcry_sexp_release, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gcry_mpi_t, gcry_mpi_release, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gcry_mpi_point_t, gcry_mpi_point_release, NULL)

#define GCRYPT_NO_FAIL(s)                                           \
    do {                                                            \
        gcry_error_t err_;                                          \
        err_ = (s);                                                 \
        if (err_) {                                                 \
            error_setg(errp,                                        \
                       "%s:%d: unexpected libgcrypt failure: %s", \
                       __FILE__, __LINE__, gcry_strerror(err_));    \
            return QCRYPTO_ECDSA_UNKNOWN_ERROR;                     \
        }                                                           \
    } while (0)

static const char *GCRYPT_CURVE_MAP[] = {
    [QCRYPTO_ECDSA_NIST_P256] = "nistp256",
    [QCRYPTO_ECDSA_NIST_P384] = "nistp384",
};

typedef struct QCryptoEcdsaGcrypt {
    gcry_ctx_t ctx;
    gcry_mpi_t d; /* private key */
    gcry_mpi_t x, y; /* public key */
    gcry_mpi_t k; /* random value used when signing */
    gcry_mpi_t h; /* hash to sign or verify */
    gcry_mpi_t r, s; /* signature */

} QCryptoEcdsaGcrypt;

static inline QCryptoEcdsaGcrypt *get_priv(QCryptoEcdsa *ecdsa)
{
    return (QCryptoEcdsaGcrypt *) ecdsa->driver;
}

static inline QCryptoEcdsaStatus mpi_scan_unsigned(gcry_mpi_t *out,
                                                   const uint8_t *in,
                                                   size_t len,
                                                   Error **errp)
{
    GCRYPT_NO_FAIL(gcry_mpi_scan(out, GCRYMPI_FMT_USG,
                                 in, len, NULL));
    return QCRYPTO_ECDSA_OK;
}

/* Pad printed MPI with 0s */
static inline QCryptoEcdsaStatus mpi_print_unsigned(uint8_t *out, size_t len,
                                                    gcry_mpi_t in, Error **errp)
{
    size_t mpi_len, written;

    mpi_len = gcry_mpi_get_nbits(in);

    if (mpi_len % 8) {
        mpi_len = (mpi_len / 8) + 1;
    } else {
        mpi_len = mpi_len / 8;
    }

    memset(out, 0, len - mpi_len);
    GCRYPT_NO_FAIL(gcry_mpi_print(GCRYMPI_FMT_USG, (void *) out + len - mpi_len,
                                  mpi_len, &written, in));
    g_assert(mpi_len == written);

    return QCRYPTO_ECDSA_OK;
}

/*
 * Return true if i is in [1; param-1], with param a scalar parameter of the
 * curve.
 */
static inline bool mpi_in_range(const QCryptoEcdsaGcrypt *priv,
                                gcry_mpi_t i, const char *param_name)
{
    g_auto(gcry_mpi_t) param;

    param = gcry_mpi_ec_get_mpi(param_name, priv->ctx, 0);

    if (gcry_mpi_cmp_ui(i, 0) <= 0) {
        return false;
    } else if (gcry_mpi_cmp(i, param) >= 0) {
        return false;
    }

    return true;
}

static bool sexp_extract_mpi(const gcry_sexp_t exp, const char *token,
                             gcry_mpi_t *out, Error **errp)
{
    g_auto(gcry_sexp_t) sub_exp;

    sub_exp = gcry_sexp_find_token(exp, token, 0);

    if (sub_exp == NULL) {
        error_setg(errp, "Unexpected libgcrypt error: token %s "
                   "not found in sexp", token);
        return false;
    }

    *out = gcry_sexp_nth_mpi(sub_exp, 1, GCRYMPI_FMT_USG);

    if (*out == NULL) {
        error_setg(errp, "Unexpected libgcrypt error while extracting token %s "
                   "as an MPI from sexp", token);
        return false;
    }

    return true;
}

static void gcrypt_ecdsa_init(QCryptoEcdsa *ecdsa)
{
    QCryptoEcdsaGcrypt *priv;

    ecdsa->driver = g_new0(QCryptoEcdsaGcrypt, 1);
    priv = get_priv(ecdsa);

    g_assert(!gcry_mpi_ec_new(&priv->ctx, NULL,
                              GCRYPT_CURVE_MAP[ecdsa->curve]));
}

static void gcrypt_ecdsa_free(QCryptoEcdsa *ecdsa)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);

    gcry_mpi_release(priv->d);
    gcry_mpi_release(priv->k);
    gcry_mpi_release(priv->h);
    gcry_mpi_release(priv->r);
    gcry_mpi_release(priv->s);
    gcry_mpi_release(priv->x);
    gcry_mpi_release(priv->y);
    gcry_ctx_release(priv->ctx);

    g_free(ecdsa->driver);
}

static QCryptoEcdsaStatus gcrypt_ecdsa_set_priv_key(QCryptoEcdsa *ecdsa,
                                                    const uint8_t *key,
                                                    size_t len, Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);
    QCryptoEcdsaStatus ret;

    if (priv->d) {
        gcry_mpi_release(priv->d);
        priv->d = NULL;
    }

    ret = mpi_scan_unsigned(&priv->d, key, len, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    if (!mpi_in_range(priv, priv->d, "n")) {
        error_setg(errp, "private key is not in [1; n-1]");
        gcry_mpi_release(priv->d);
        priv->d = NULL;
        return QCRYPTO_ECDSA_PRIV_KEY_OUT_OF_RANGE;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_set_pub_key(QCryptoEcdsa *ecdsa,
                                                   const uint8_t *x,
                                                   size_t x_len,
                                                   const uint8_t *y,
                                                   size_t y_len,
                                                   Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);
    QCryptoEcdsaStatus ret;
    g_auto(gcry_mpi_point_t) q = NULL;

    if (priv->x) {
        gcry_mpi_release(priv->x);
        priv->x = NULL;
    }

    if (priv->y) {
        gcry_mpi_release(priv->y);
        priv->y = NULL;
    }

    ret = mpi_scan_unsigned(&priv->x, x, x_len, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        gcry_mpi_release(priv->x);
        priv->x = NULL;
        return ret;
    }

    ret = mpi_scan_unsigned(&priv->y, y, y_len, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        gcry_mpi_release(priv->x);
        priv->x = NULL;
        return ret;
    }

    if (!mpi_in_range(priv, priv->x, "p")) {
        error_setg(errp, "public key x is not in [1; p-1]");
        gcry_mpi_release(priv->x);
        priv->x = NULL;
        return QCRYPTO_ECDSA_PUB_KEY_X_OUT_OF_RANGE;
    }

    if (!mpi_in_range(priv, priv->y, "p")) {
        error_setg(errp, "public key y is not in [1; p-1]");
        gcry_mpi_release(priv->x);
        priv->x = NULL;
        return QCRYPTO_ECDSA_PUB_KEY_Y_OUT_OF_RANGE;
    }

    q = gcry_mpi_point_set(NULL, priv->x, priv->y, GCRYMPI_CONST_ONE);

    if (!gcry_mpi_ec_curve_point(q, priv->ctx)) {
        error_setg(errp, "The public key is not on the curve");
        gcry_mpi_release(priv->x);
        priv->x = NULL;
        return QCRYPTO_ECDSA_PUB_KEY_NOT_ON_CURVE;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_set_sig(QCryptoEcdsa *ecdsa,
                                               const uint8_t *r, size_t r_len,
                                               const uint8_t *s, size_t s_len,
                                               Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);
    QCryptoEcdsaStatus ret;

    if (priv->r) {
        gcry_mpi_release(priv->r);
        priv->r = NULL;
    }

    if (priv->s) {
        gcry_mpi_release(priv->s);
        priv->s = NULL;
    }

    ret = mpi_scan_unsigned(&priv->r, r, r_len, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        gcry_mpi_release(priv->r);
        priv->r = NULL;
        return ret;
    }

    if (!mpi_in_range(priv, priv->r, "n")) {
        error_setg(errp, "signature r value is not in [1; n-1]");
        gcry_mpi_release(priv->r);
        priv->r = NULL;
        return QCRYPTO_ECDSA_SIG_R_OUT_OF_RANGE;
    }

    ret = mpi_scan_unsigned(&priv->s, s, s_len, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        gcry_mpi_release(priv->r);
        priv->r = NULL;
        return ret;
    }

    if (!mpi_in_range(priv, priv->s, "n")) {
        error_setg(errp, "signature s value is not in [1; n-1]");
        gcry_mpi_release(priv->s);
        priv->s = NULL;
        return QCRYPTO_ECDSA_SIG_S_OUT_OF_RANGE;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_set_random(QCryptoEcdsa *ecdsa,
                                                  const uint8_t *random,
                                                  size_t len,
                                                  Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);
    QCryptoEcdsaStatus ret;

    if (priv->k) {
        gcry_mpi_release(priv->k);
        priv->k = NULL;
    }

    ret = mpi_scan_unsigned(&priv->k, random, len, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        gcry_mpi_release(priv->k);
        priv->k = NULL;
        return ret;
    }

    if (!mpi_in_range(priv, priv->d, "n")) {
        error_setg(errp, "random k is not in [1; n-1]");
        gcry_mpi_release(priv->k);
        priv->k = NULL;
        return QCRYPTO_ECDSA_K_OUT_OF_RANGE;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_set_hash(QCryptoEcdsa *ecdsa,
                                                const uint8_t *hash, size_t len,
                                                Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);

    if (priv->h) {
        gcry_mpi_release(priv->h);
        priv->h = NULL;
    }

    return mpi_scan_unsigned(&priv->h, hash, len, errp);
}

static QCryptoEcdsaStatus gcrypt_ecdsa_get_pub_key(QCryptoEcdsa *ecdsa,
                                                   uint8_t *x, size_t x_len,
                                                   uint8_t *y, size_t y_len,
                                                   Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);
    QCryptoEcdsaStatus ret;

    if (!priv->x || !priv->y) {
        error_setg(errp, "no public key available in QCryptoEcdsa state");
        return QCRYPTO_ECDSA_PUB_KEY_NOT_AVAILABLE;
    }

    ret = mpi_print_unsigned(x, x_len, priv->x, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    ret = mpi_print_unsigned(y, y_len, priv->y, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_get_sig(QCryptoEcdsa *ecdsa,
                                               uint8_t *r, size_t r_len,
                                               uint8_t *s, size_t s_len,
                                               Error **errp)
{
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);
    QCryptoEcdsaStatus ret;

    if (!priv->r || !priv->s) {
        error_setg(errp, "no signature available in QCryptoEcdsa state");
        return QCRYPTO_ECDSA_SIG_NOT_AVAILABLE;
    }

    ret = mpi_print_unsigned(r, r_len, priv->r, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    ret = mpi_print_unsigned(s, s_len, priv->s, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_sign(QCryptoEcdsa *ecdsa, Error **errp)
{
    g_auto(gcry_sexp_t) digest_sexp = NULL, key_sexp = NULL, sig_sexp = NULL;
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);

    if (!priv->h) {
        error_setg(errp, "hash not set");
        return QCRYPTO_ECDSA_HASH_NOT_AVAILABLE;
    }

    if (!priv->d) {
        error_setg(errp, "private key not set");
        return QCRYPTO_ECDSA_PRIV_KEY_NOT_AVAILABLE;
    }

    if (priv->k) {
        GCRYPT_NO_FAIL(gcry_sexp_build(&digest_sexp, NULL,
                                       "(data (flags raw) (hash sha384 %M) (label %M))",
                                       priv->h, priv->k));
    } else {
        GCRYPT_NO_FAIL(gcry_sexp_build(&digest_sexp, NULL,
                                       "(data (flags raw) (hash sha384 %M))",
                                       priv->h));
    }

    GCRYPT_NO_FAIL(
        gcry_sexp_build(&key_sexp, NULL,
                        "(private-key (ecc (curve %s) (d %M)))",
                        GCRYPT_CURVE_MAP[ecdsa->curve], priv->d));

    GCRYPT_NO_FAIL(gcry_pk_sign(&sig_sexp, digest_sexp, key_sexp));

    if (!sexp_extract_mpi(sig_sexp, "r", &priv->r, errp)) {
        return QCRYPTO_ECDSA_UNKNOWN_ERROR;
    }

    if (!sexp_extract_mpi(sig_sexp, "s", &priv->s, errp)) {
        return QCRYPTO_ECDSA_UNKNOWN_ERROR;
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_verify(QCryptoEcdsa *ecdsa, Error **errp)
{
    g_auto(gcry_sexp_t) digest = NULL, key = NULL, sig = NULL;
    g_autofree uint8_t *buf = NULL;
    size_t len = qcrypto_ecdsa_get_curve_data_size(ecdsa->curve);
    gcry_error_t gcrypt_res;
    QCryptoEcdsaStatus ret;

    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);

    if (!priv->h) {
        error_setg(errp, "hash not set");
        return QCRYPTO_ECDSA_HASH_NOT_AVAILABLE;
    }

    if (!priv->x || !priv->y) {
        error_setg(errp, "public key not set");
        return QCRYPTO_ECDSA_PUB_KEY_NOT_AVAILABLE;
    }

    if (!priv->r || !priv->s) {
        error_setg(errp, "signature not set");
        return QCRYPTO_ECDSA_SIG_NOT_AVAILABLE;
    }

    /*
     * We give a hashed value to libgcrypt. The following sha384 is irrelevant
     * and ignored by libgcrypt.
     */
    GCRYPT_NO_FAIL(gcry_sexp_build(&digest, NULL,
                                   "(data (flags raw) (hash sha384 %M))",
                                   priv->h));

    /*
     * libgcrypt expects the public key in the uncompressed
     * format [0x04, x, y].
     */
    buf = g_malloc(1 + 2 * len);
    buf[0] = 0x04;

    ret = mpi_print_unsigned(buf + 1, len, priv->x, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    ret = mpi_print_unsigned(buf + 1 + len, len, priv->y, errp);
    if (ret != QCRYPTO_ECDSA_OK) {
        return ret;
    }

    GCRYPT_NO_FAIL(gcry_sexp_build(&key, NULL,
                                   "(public-key (ecc (curve %s) (q %b)))",
                                   GCRYPT_CURVE_MAP[ecdsa->curve],
                                   1 + 2 * len, buf));
    GCRYPT_NO_FAIL(gcry_sexp_build(&sig, NULL,
                                   "(sig-val (ecdsa (r %M) (s %M)))",
                                   priv->r, priv->s));

    gcrypt_res = gcry_pk_verify(sig, digest, key);

    if (gcrypt_res == GPG_ERR_BAD_SIGNATURE) {
        return QCRYPTO_ECDSA_SIG_MISMATCH;
    } else if (gcrypt_res) {
        GCRYPT_NO_FAIL(gcrypt_res);
    }

    return QCRYPTO_ECDSA_OK;
}

static QCryptoEcdsaStatus gcrypt_ecdsa_compute_pub_key(QCryptoEcdsa *ecdsa,
                                                       Error **errp)
{
    g_auto(gcry_mpi_point_t) g, pub;
    QCryptoEcdsaGcrypt *priv = get_priv(ecdsa);

    /* pub = priv x G */
    g = gcry_mpi_ec_get_point("g", priv->ctx, 0);
    pub = gcry_mpi_point_new(0);
    gcry_mpi_ec_mul(pub, priv->d, g, priv->ctx);

    if (!gcry_mpi_ec_curve_point(pub, priv->ctx)) {
        error_setg(errp, "public key is not on the curve");
        return QCRYPTO_ECDSA_PUB_KEY_NOT_ON_CURVE;
    }

    if (!priv->x) {
        priv->x = gcry_mpi_new(0);
    }

    if (!priv->y) {
        priv->y = gcry_mpi_new(0);
    }

    if (gcry_mpi_ec_get_affine(priv->x, priv->y, pub, priv->ctx)) {
        error_setg(errp, "public key affine projection is at infinity");
        return QCRYPTO_ECDSA_PUB_KEY_PROJ_AT_INF;
    }

    return QCRYPTO_ECDSA_OK;
}

QCryptoEcdsaDriver qcrypto_ecdsa_driver = {
    .init = gcrypt_ecdsa_init,
    .free = gcrypt_ecdsa_free,
    .set_priv_key = gcrypt_ecdsa_set_priv_key,
    .set_pub_key = gcrypt_ecdsa_set_pub_key,
    .set_sig = gcrypt_ecdsa_set_sig,
    .set_random = gcrypt_ecdsa_set_random,
    .set_hash = gcrypt_ecdsa_set_hash,
    .get_pub_key = gcrypt_ecdsa_get_pub_key,
    .get_sig = gcrypt_ecdsa_get_sig,
    .sign = gcrypt_ecdsa_sign,
    .verify = gcrypt_ecdsa_verify,
    .compute_pub_key = gcrypt_ecdsa_compute_pub_key,
};
