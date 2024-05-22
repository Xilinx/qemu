/*
 * QEMU Crypto ECDSA algorithm
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Author: Luc Michel <luc.michel@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CRYPTO_ECDSA_H
#define CRYPTO_ECDSA_H

typedef enum QCryptoEcdsaCurve {
    QCRYPTO_ECDSA_NIST_P256,
    QCRYPTO_ECDSA_NIST_P384,
} QCryptoEcdsaCurve;

typedef struct QCryptoEcdsa {
    QCryptoEcdsaCurve curve;
    void *driver;
} QCryptoEcdsa;

typedef enum QCryptoEcdsaStatus {
    QCRYPTO_ECDSA_OK = 0,
    QCRYPTO_ECDSA_UNKNOWN_ERROR,
    /* private key is not available in the QCryptoEcdsa context */
    QCRYPTO_ECDSA_PRIV_KEY_NOT_AVAILABLE,
    /* private key is not in [1; n-1] */
    QCRYPTO_ECDSA_PRIV_KEY_OUT_OF_RANGE,
    /* hash is not available in the QCryptoEcdsa context */
    QCRYPTO_ECDSA_HASH_NOT_AVAILABLE,
    /* signature is not available in the QCryptoEcdsa context */
    QCRYPTO_ECDSA_SIG_NOT_AVAILABLE,
    /* r is not in [1; n-1] */
    QCRYPTO_ECDSA_SIG_R_OUT_OF_RANGE,
    /* s is not in [1; n-1] */
    QCRYPTO_ECDSA_SIG_S_OUT_OF_RANGE,
    /* signature verification failure */
    QCRYPTO_ECDSA_SIG_MISMATCH,
    /* x is not in [1; p-1] */
    QCRYPTO_ECDSA_PUB_KEY_X_OUT_OF_RANGE,
    /* x is not in [1; p-1] */
    QCRYPTO_ECDSA_PUB_KEY_Y_OUT_OF_RANGE,
    /* public key is not on the curve */
    QCRYPTO_ECDSA_PUB_KEY_NOT_ON_CURVE,
    /* public key affine projection is at infinity */
    QCRYPTO_ECDSA_PUB_KEY_PROJ_AT_INF,
    /* public key is not available in the QCryptoEcdsa context */
    QCRYPTO_ECDSA_PUB_KEY_NOT_AVAILABLE,
    /* random k is not in [1; n-1] */
    QCRYPTO_ECDSA_K_OUT_OF_RANGE,
} QCryptoEcdsaStatus;

/**
 * qcrypto_ecdsa_get_curve_data_size:
 * @c: the curve
 *
 * Get the data size associated with the curve @c. This size is the minimum
 * size to store the different ECDSA objects: private key, random value k,
 * public key and signatire components.
 *
 * Returns: the data size associated with @c.
 */
static inline size_t qcrypto_ecdsa_get_curve_data_size(QCryptoEcdsaCurve c)
{
    switch (c) {
    case QCRYPTO_ECDSA_NIST_P256:
        return 32;

    case QCRYPTO_ECDSA_NIST_P384:
        return 48;

    default:
        g_assert_not_reached();
    }
}

/**
 * qcrypto_ecdsa_new:
 * @c: the curve
 *
 * Create a new ECDSA context on curve @c
 *
 * Returns: the created context, or %NULL on error.
 */
QCryptoEcdsa *qcrypto_ecdsa_new(QCryptoEcdsaCurve curve);

/**
 * qcrypto_ecdsa_free:
 * @ecdsa: the QCryptoEcdsa context
 *
 * Destroy an ECDSA context.
 *
 */
void qcrypto_ecdsa_free(QCryptoEcdsa *ecdsa);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QCryptoEcdsa, qcrypto_ecdsa_free)

/**
 * qcrypto_ecdsa_set_priv_key:
 * @ecdsa: the QCryptoEcdsa context
 * @key: the private key to set in the context
 * @len: the @key len
 * @errp: pointer to error object
 *
 * Set the private key to use for subsequent operations on @ecdsa
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_PRIV_KEY_OUT_OF_RANGE if @key is invalid,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_set_priv_key(QCryptoEcdsa *ecdsa,
                                              const uint8_t *key,
                                              size_t len, Error **errp);

/**
 * qcrypto_ecdsa_set_pub_key:
 * @ecdsa: the QCryptoEcdsa context
 * @x: the public key x value to set in the context
 * @x_len: the @x len
 * @y: the public key y value to set in the context
 * @y_len: the @y len
 * @errp: pointer to error object
 *
 * Set the public key to use for subsequent operations on @ecdsa
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_PUB_KEY_X_OUT_OF_RANGE if @x is invalid,
 *   - %QCRYPTO_ECDSA_PUB_KEY_Y_OUT_OF_RANGE if @y is invalid,
 *   - %QCRYPTO_ECDSA_PUB_KEY_NOT_ON_CURVE if (@x, @y) is not on the curve of
 *     the underlying @ecdsa context,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_set_pub_key(QCryptoEcdsa *ecdsa,
                                             const uint8_t *x, size_t x_len,
                                             const uint8_t *y, size_t y_len,
                                             Error **errp);

/**
 * qcrypto_ecdsa_set_sig:
 * @ecdsa: the QCryptoEcdsa context
 * @r: the signature r value to set in the context
 * @r_len: the @r len
 * @s: the signature s value to set in the context
 * @s_len: the @s len
 * @errp: pointer to error object
 *
 * Set the signature to use for subsequent operations on @ecdsa
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_SIG_R_OUT_OF_RANGE if @r is invalid,
 *   - %QCRYPTO_ECDSA_SIG_S_OUT_OF_RANGE if @s is invalid,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_set_sig(QCryptoEcdsa *ecdsa,
                                         const uint8_t *r, size_t r_len,
                                         const uint8_t *s, size_t s_len,
                                         Error **errp);

/**
 * qcrypto_ecdsa_set_random:
 * @ecdsa: the QCryptoEcdsa context
 * @random: the random value to set in the context
 * @len: the @random len
 * @errp: pointer to error object
 *
 * Set the random value k to use for subsequent operations on @ecdsa
 * Note that the same k value should never be used twice with the same private
 * key. Doing so can reveal the private key.
 *
 * If left unset, a random value is picked automatically. Setting a custom k
 * value is only useful for reproducibility of a signature operation, e.g.,
 * when modelling an ECDSA device to which the guest provides this random value.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_K_OUT_OF_RANGE if @k is invalid,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_set_random(QCryptoEcdsa *ecdsa,
                                            const uint8_t *random, size_t len,
                                            Error **errp);

/**
 * qcrypto_ecdsa_set_hash:
 * @ecdsa: the QCryptoEcdsa context
 * @hash: the hash to set in the context
 * @len: the @hash len
 * @errp: pointer to error object
 *
 * Set the hash to use for subsequent operations on @ecdsa. This is the value
 * being signed on verified during the corresponding operations.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_SIG_R_OUT_OF_RANGE if @r is invalid,
 *   - %QCRYPTO_ECDSA_SIG_S_OUT_OF_RANGE if @s is invalid,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_set_hash(QCryptoEcdsa *ecdsa,
                                          const uint8_t *hash, size_t len,
                                          Error **errp);

/**
 * qcrypto_ecdsa_get_pub_key:
 * @ecdsa: the QCryptoEcdsa context
 * @x: the buffer to store the public key x value in
 * @x_len: the @x len
 * @y: the buffer to store the public key y value in
 * @y_len: the @y len
 * @errp: pointer to error object
 *
 * Extract the public key from the @ecdsa context. This can be the public key
 * set by a preceding qcrypto_ecdsa_set_pub_key call, or the result of a
 * qcrypto_ecdsa_compute_pub_key operation.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_PUB_KEY_NOT_AVAILABLE if @ecdsa has no public key,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_get_pub_key(QCryptoEcdsa *ecdsa,
                                             uint8_t *x, size_t x_len,
                                             uint8_t *y, size_t y_len,
                                             Error **errp);

/**
 * qcrypto_ecdsa_get_sig:
 * @ecdsa: the QCryptoEcdsa context
 * @r: the buffer to store the signature r value in
 * @r_len: the @r len
 * @s: the buffer to store the signature s value in
 * @s_len: the @s len
 * @errp: pointer to error object
 *
 * Extract the signature from the @ecdsa context. This can be the signature
 * set by a preceding qcrypto_ecdsa_set_sig call, or the result of a
 * qcrypto_ecdsa_sign operation.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_SIG_NOT_AVAILABLE if @ecdsa has no signature,
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_get_sig(QCryptoEcdsa *ecdsa,
                                         uint8_t *r, size_t r_len,
                                         uint8_t *s, size_t s_len,
                                         Error **errp);

/**
 * qcrypto_ecdsa_sign:
 * @ecdsa: the QCryptoEcdsa context
 * @errp: pointer to error object
 *
 * Perform a sign operation on the @ecdsa context. The private key and
 * hash should have been set in the context prior to this call. The random
 * value k is optional. If unset in @ecdsa, a random value is picked
 * automatically.
 *
 * On success, the signature can be retrieved using the qcrypto_ecdsa_get_sig
 * function.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK on success,
 *   - %QCRYPTO_ECDSA_HASH_NOT_AVAILABLE if no hash has been set in @context
 *   - %QCRYPTO_ECDSA_PRIV_KEY_NOT_AVAILABLE if no private has been set in
 *   @context
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_sign(QCryptoEcdsa *ecdsa, Error **errp);

/**
 * qcrypto_ecdsa_verify:
 * @ecdsa: the QCryptoEcdsa context
 * @errp: pointer to error object
 *
 * Perform a signature verify operation on the @ecdsa context. The public key,
 * hash and the signature to verify should have been set in the context prior
 * to this call.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK if the operation succeed and the signature is valid,
 *   - %QCRYPTO_ECDSA_SIG_MISMATCH if the signature verification failed,
 *   - %QCRYPTO_ECDSA_HASH_NOT_AVAILABLE fs no hash has been set in @context
 *   - %QCRYPTO_ECDSA_PUB_KEY_NOT_AVAILABLE if no public has been set in
 *   @context
 *   - %QCRYPTO_ECDSA_SIG_NOT_AVAILABLE if no signature has been set in @context
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_verify(QCryptoEcdsa *ecdsa, Error **errp);

/**
 * qcrypto_ecdsa_compute_pub_key:
 * @ecdsa: the QCryptoEcdsa context
 * @errp: pointer to error object
 *
 * Retrieve the public key corresponding to a private key. The private should
 * have been set in the @ecdsa context prior to this call. The resulting public
 * key can be retrieved using the qcrypto_ecdsa_get_pub_key function.
 *
 * Returns:
 *   - %QCRYPTO_ECDSA_OK if the operation succeed and the signature is valid,
 *   - %QCRYPTO_ECDSA_PRIV_KEY_NOT_AVAILABLE if no private has been set in
 *   @context
 *   - %QCRYPTO_ECDSA_PUB_KEY_NOT_ON_CURVE if the resulting public key is not
 *   on the curve
 *   - %QCRYPTO_ECDSA_PUB_KEY_PROJ_AT_INF if the resulting public key affine
 *   projection is at infinity
 *   - %QCRYPTO_ECDSA_UNKNOWN_ERROR otherwise.
 */
QCryptoEcdsaStatus qcrypto_ecdsa_compute_pub_key(QCryptoEcdsa *ecdsa,
                                                 Error **errp);

#endif
