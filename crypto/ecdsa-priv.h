/*
 * QEMU Crypto ECDSA algorithm implementation details
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Author: Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CRYPTO_ECDSA_PRIV_H
#define CRYPTO_ECDSA_PRIV_H

#include "crypto/ecdsa.h"

typedef struct QCryptoEcdsaDriver {
    void (*init)(QCryptoEcdsa *ecdsa);
    void (*free)(QCryptoEcdsa *ecdsa);

    QCryptoEcdsaStatus (*set_priv_key)(QCryptoEcdsa *ecdsa, const uint8_t *key,
                                       size_t len, Error **errp);
    QCryptoEcdsaStatus (*set_pub_key)(QCryptoEcdsa *ecdsa,
                                      const uint8_t *x, size_t x_len,
                                      const uint8_t *y, size_t y_len,
                                      Error **errp);
    QCryptoEcdsaStatus (*set_sig)(QCryptoEcdsa *ecdsa,
                                  const uint8_t *r, size_t r_len,
                                  const uint8_t *s, size_t s_len,
                                  Error **errp);
    QCryptoEcdsaStatus (*set_random)(QCryptoEcdsa *ecdsa,
                                     const uint8_t *random,
                                     size_t len, Error **errp);
    QCryptoEcdsaStatus (*set_hash)(QCryptoEcdsa *ecdsa, const uint8_t *hash,
                                   size_t len, Error **errp);

    QCryptoEcdsaStatus (*get_pub_key)(QCryptoEcdsa *ecdsa,
                                      uint8_t *x, size_t x_len,
                                      uint8_t *y, size_t y_len,
                                      Error **errp);
    QCryptoEcdsaStatus (*get_sig)(QCryptoEcdsa *ecdsa,
                                  uint8_t *r, size_t r_len,
                                  uint8_t *s, size_t s_len,
                                  Error **errp);

    QCryptoEcdsaStatus (*sign)(QCryptoEcdsa *ecdsa, Error **errp);
    QCryptoEcdsaStatus (*verify)(QCryptoEcdsa *ecdsa, Error **errp);
    QCryptoEcdsaStatus (*compute_pub_key)(QCryptoEcdsa *ecdsa, Error **errp);
} QCryptoEcdsaDriver;

extern QCryptoEcdsaDriver qcrypto_ecdsa_driver;

#endif
