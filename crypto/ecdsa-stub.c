/*
 * QEMU Crypto ECDSA algorithm - stub
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

static void ecdsa_stub_init(QCryptoEcdsa *ecdsa)
{
    ecdsa->driver = NULL;
}

static void ecdsa_stub_free(QCryptoEcdsa *ecdsa)
{
}

static QCryptoEcdsaStatus ecdsa_stub_set_priv_key(QCryptoEcdsa *ecdsa,
                                                  const uint8_t *key,
                                                  size_t len, Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_set_pub_key(QCryptoEcdsa *ecdsa,
                                                 const uint8_t *x,
                                                 size_t x_len,
                                                 const uint8_t *y,
                                                 size_t y_len,
                                                 Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_set_sig(QCryptoEcdsa *ecdsa,
                                             const uint8_t *r, size_t r_len,
                                             const uint8_t *s, size_t s_len,
                                             Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_set_random(QCryptoEcdsa *ecdsa,
                                                const uint8_t *random,
                                                size_t len,
                                                Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_set_hash(QCryptoEcdsa *ecdsa,
                                              const uint8_t *hash, size_t len,
                                              Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_get_pub_key(QCryptoEcdsa *ecdsa,
                                                 uint8_t *x, size_t x_len,
                                                 uint8_t *y, size_t y_len,
                                                 Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_get_sig(QCryptoEcdsa *ecdsa,
                                             uint8_t *r, size_t r_len,
                                             uint8_t *s, size_t s_len,
                                             Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_sign(QCryptoEcdsa *ecdsa, Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_verify(QCryptoEcdsa *ecdsa, Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

static QCryptoEcdsaStatus ecdsa_stub_compute_pub_key(QCryptoEcdsa *ecdsa,
                                                     Error **errp)
{
    error_setg(errp, "No ECDSA support in this build");
    return QCRYPTO_ECDSA_UNKNOWN_ERROR;
}

QCryptoEcdsaDriver qcrypto_ecdsa_driver = {
    .init = ecdsa_stub_init,
    .free = ecdsa_stub_free,
    .set_priv_key = ecdsa_stub_set_priv_key,
    .set_pub_key = ecdsa_stub_set_pub_key,
    .set_sig = ecdsa_stub_set_sig,
    .set_random = ecdsa_stub_set_random,
    .set_hash = ecdsa_stub_set_hash,
    .get_pub_key = ecdsa_stub_get_pub_key,
    .get_sig = ecdsa_stub_get_sig,
    .sign = ecdsa_stub_sign,
    .verify = ecdsa_stub_verify,
    .compute_pub_key = ecdsa_stub_compute_pub_key,
};

