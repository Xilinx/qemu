/*
 * The sha2 family of hash functions.
 *
 * Copyright (C) Niels Möller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SHA2_H
#define SHA2_H

/* Borrowed from nettle (sha2.h).  */

/* Name mangling */
#define sha224_init nettle_sha224_init
#define sha224_digest nettle_sha224_digest
#define sha256_init nettle_sha256_init
#define sha256_update nettle_sha256_update
#define sha256_digest nettle_sha256_digest
#define sha256_compress nettle_sha256_compress
#define sha384_init nettle_sha384_init
#define sha384_digest nettle_sha384_digest
#define sha512_init nettle_sha512_init
#define sha512_update nettle_sha512_update
#define sha512_digest nettle_sha512_digest
#define sha512_compress nettle_sha512_compress
#define sha512_224_init   nettle_sha512_224_init
#define sha512_224_digest nettle_sha512_224_digest
#define sha512_256_init   nettle_sha512_256_init
#define sha512_256_digest nettle_sha512_256_digest

/* For backwards compatibility */
#define SHA224_DATA_SIZE SHA256_BLOCK_SIZE
#define SHA256_DATA_SIZE SHA256_BLOCK_SIZE
#define SHA512_DATA_SIZE SHA512_BLOCK_SIZE
#define SHA384_DATA_SIZE SHA512_BLOCK_SIZE

/* SHA256 */

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE 64

/* Digest is kept internally as 8 32-bit words. */
#define _SHA256_DIGEST_LENGTH 8

struct sha256_ctx
{
  uint32_t state[_SHA256_DIGEST_LENGTH];    /* State variables */
  uint64_t count;                           /* 64-bit block count */
  unsigned int index;                       /* index into buffer */
  uint8_t block[SHA256_BLOCK_SIZE];          /* SHA256 data buffer */
};

void
sha256_init(struct sha256_ctx *ctx);

void
sha256_update(struct sha256_ctx *ctx,
	      size_t length,
	      const uint8_t *data);

void
sha256_digest(struct sha256_ctx *ctx,
	      size_t length,
	      uint8_t *digest);

void
sha256_digest_no_pad(struct sha256_ctx *ctx,
                     size_t length,
                     uint8_t *digest);

void
sha256_compress(uint32_t *state, const uint8_t *input);

/* SHA224, a truncated SHA256 with different initial state. */

#define SHA224_DIGEST_SIZE 28
#define SHA224_BLOCK_SIZE SHA256_BLOCK_SIZE
#define sha224_ctx sha256_ctx

void
sha224_init(struct sha256_ctx *ctx);

#define sha224_update nettle_sha256_update

void
sha224_digest(struct sha256_ctx *ctx,
	      size_t length,
	      uint8_t *digest);


/* SHA512 */

#define SHA512_DIGEST_SIZE 64
#define SHA512_BLOCK_SIZE 128

/* Digest is kept internally as 8 64-bit words. */
#define _SHA512_DIGEST_LENGTH 8

struct sha512_ctx
{
  uint64_t state[_SHA512_DIGEST_LENGTH];    /* State variables */
  uint64_t count_low, count_high;           /* 128-bit block count */
  unsigned int index;                       /* index into buffer */
  uint8_t block[SHA512_BLOCK_SIZE];          /* SHA512 data buffer */
};

void
sha512_init(struct sha512_ctx *ctx);

void
sha512_update(struct sha512_ctx *ctx,
	      size_t length,
	      const uint8_t *data);

void
sha512_digest(struct sha512_ctx *ctx,
	      size_t length,
	      uint8_t *digest);

void
sha512_compress(uint64_t *state, const uint8_t *input);

/* SHA384, a truncated SHA512 with different initial state. */

#define SHA384_DIGEST_SIZE 48
#define SHA384_BLOCK_SIZE SHA512_BLOCK_SIZE
#define sha384_ctx sha512_ctx

void
sha384_init(struct sha512_ctx *ctx);

#define sha384_update nettle_sha512_update

void
sha384_digest(struct sha512_ctx *ctx,
	      size_t length,
	      uint8_t *digest);


/* SHA512_224 and SHA512_256, two truncated versions of SHA512 
   with different initial states. */

#define SHA512_224_DIGEST_SIZE 28
#define SHA512_224_BLOCK_SIZE SHA512_BLOCK_SIZE
#define sha512_224_ctx sha512_ctx

void
sha512_224_init(struct sha512_224_ctx *ctx);

#define sha512_224_update nettle_sha512_update

void
sha512_224_digest(struct sha512_224_ctx *ctx,
                  size_t length,
                  uint8_t *digest);

#define SHA512_256_DIGEST_SIZE 32
#define SHA512_256_BLOCK_SIZE SHA512_BLOCK_SIZE
#define sha512_256_ctx sha512_ctx

void
sha512_256_init(struct sha512_256_ctx *ctx);

#define sha512_256_update nettle_sha512_update

void
sha512_256_digest(struct sha512_256_ctx *ctx,
                  size_t length,
                  uint8_t *digest);

#endif /* SHA2_H */
