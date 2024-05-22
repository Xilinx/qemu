/*
 * QEMU Crypto ECDSA algorithm
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Author: Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "crypto/ecdsa-priv.h"

QCryptoEcdsa *qcrypto_ecdsa_new(QCryptoEcdsaCurve curve)
{
    QCryptoEcdsa *ret;

    ret = g_new0(QCryptoEcdsa, 1);
    ret->curve = curve;
    qcrypto_ecdsa_driver.init(ret);

    return ret;
}

void qcrypto_ecdsa_free(QCryptoEcdsa *ecdsa)
{
    qcrypto_ecdsa_driver.free(ecdsa);
    g_free(ecdsa);
}

QCryptoEcdsaStatus qcrypto_ecdsa_set_priv_key(QCryptoEcdsa *ecdsa,
                                              const uint8_t *key,
                                              size_t len, Error **errp)
{
    return qcrypto_ecdsa_driver.set_priv_key(ecdsa, key, len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_set_pub_key(QCryptoEcdsa *ecdsa,
                                             const uint8_t *x, size_t x_len,
                                             const uint8_t *y, size_t y_len,
                                             Error **errp)
{
    return qcrypto_ecdsa_driver.set_pub_key(ecdsa, x, x_len, y, y_len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_set_sig(QCryptoEcdsa *ecdsa,
                                         const uint8_t *r, size_t r_len,
                                         const uint8_t *s, size_t s_len,
                                         Error **errp)
{
    return qcrypto_ecdsa_driver.set_sig(ecdsa, r, r_len, s, s_len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_set_random(QCryptoEcdsa *ecdsa,
                                            const uint8_t *key,
                                            size_t len, Error **errp)
{
    return qcrypto_ecdsa_driver.set_random(ecdsa, key, len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_set_hash(QCryptoEcdsa *ecdsa,
                                          const uint8_t *hash, size_t len,
                                          Error **errp)
{
    return qcrypto_ecdsa_driver.set_hash(ecdsa, hash, len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_get_pub_key(QCryptoEcdsa *ecdsa,
                                             uint8_t *x, size_t x_len,
                                             uint8_t *y, size_t y_len,
                                             Error **errp)
{
    return qcrypto_ecdsa_driver.get_pub_key(ecdsa, x, x_len,
                                            y, y_len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_get_sig(QCryptoEcdsa *ecdsa,
                                         uint8_t *r, size_t r_len,
                                         uint8_t *s, size_t s_len,
                                         Error **errp)
{
    return qcrypto_ecdsa_driver.get_sig(ecdsa, r, r_len,
                                        s, s_len, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_sign(QCryptoEcdsa *ecdsa, Error **errp)
{
    return qcrypto_ecdsa_driver.sign(ecdsa, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_verify(QCryptoEcdsa *ecdsa, Error **errp)
{
    return qcrypto_ecdsa_driver.verify(ecdsa, errp);
}

QCryptoEcdsaStatus qcrypto_ecdsa_compute_pub_key(QCryptoEcdsa *ecdsa,
                                                 Error **errp)
{
    return qcrypto_ecdsa_driver.compute_pub_key(ecdsa, errp);
}
