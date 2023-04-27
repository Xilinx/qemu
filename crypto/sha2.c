/*
 * sha256 hash function.
 *
 * Copyright (C) Niels Möller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "crypto/sha2.h"

#define DEBUG(n)
#define SHA256_DEBUG 0
const uint8_t *
_nettle_sha256_compress_n(uint32_t *state, const uint32_t *table,
			  size_t blocks, const uint8_t *input);

/* Some macro borrowed from macros.h  */

#define WRITE_UINT32(p, i)			\
do {						\
  (p)[0] = ((i) >> 24) & 0xff;			\
  (p)[1] = ((i) >> 16) & 0xff;			\
  (p)[2] = ((i) >> 8) & 0xff;			\
  (p)[3] = (i) & 0xff;				\
} while(0)

#define WRITE_UINT64(p, i)			\
do {						\
  (p)[0] = ((i) >> 56) & 0xff;			\
  (p)[1] = ((i) >> 48) & 0xff;			\
  (p)[2] = ((i) >> 40) & 0xff;			\
  (p)[3] = ((i) >> 32) & 0xff;			\
  (p)[4] = ((i) >> 24) & 0xff;			\
  (p)[5] = ((i) >> 16) & 0xff;			\
  (p)[6] = ((i) >> 8) & 0xff;			\
  (p)[7] = (i) & 0xff;				\
} while(0)
/* Reads a 32-bit integer, in network, big-endian, byte order */
#define READ_UINT32(p)				\
(  (((uint32_t) (p)[0]) << 24)			\
 | (((uint32_t) (p)[1]) << 16)			\
 | (((uint32_t) (p)[2]) << 8)			\
 |  ((uint32_t) (p)[3]))
#define ROTL32(n,x) (((x)<<(n)) | ((x)>>((-(n)&31))))

/* Pads the block to a block boundary with the bit pattern 1 0*,
   leaving size octets for the length field at the end. If needed,
   compresses the block and starts a new one. */
#define MD_PAD(ctx, size, f)						\
  do {									\
    unsigned __md_i;							\
    __md_i = (ctx)->index;						\
									\
    /* Set the first char of padding to 0x80. This is safe since there	\
       is always at least one byte free */				\
									\
    assert(__md_i < sizeof((ctx)->block));				\
    (ctx)->block[__md_i++] = 0x80;					\
									\
    if (__md_i > (sizeof((ctx)->block) - (size)))			\
      { /* No room for length in this block. Process it and		\
	   pad with another one */					\
	memset((ctx)->block + __md_i, 0, sizeof((ctx)->block) - __md_i); \
									\
	f((ctx), (ctx)->block);						\
	__md_i = 0;							\
      }									\
    memset((ctx)->block + __md_i, 0,					\
	   sizeof((ctx)->block) - (size) - __md_i);			\
									\
  } while (0)

/* Borrowed from nettle (md-internal.h).  */

#define MD_FILL_OR_RETURN(ctx, length, data)			\
  do {								\
    unsigned __md_left = sizeof((ctx)->block) - (ctx)->index;	\
    if ((length) < __md_left)					\
      {								\
	memcpy((ctx)->block + (ctx)->index, (data), (length));	\
	(ctx)->index += (length);				\
	return;							\
      }								\
    memcpy((ctx)->block + (ctx)->index, (data), __md_left);	\
    (data) += __md_left;					\
    (length) -= __md_left;					\
  } while(0)

/* Borrowed from nettle (sha256-compress-n.c).  */

/* A block, treated as a sequence of 32-bit words. */
#define SHA256_DATA_LENGTH 16

/* The SHA256 functions. The Choice function is the same as the SHA1
   function f1, and the majority function is the same as the SHA1 f3
   function. They can be optimized to save one boolean operation each
   - thanks to Rich Schroeppel, rcs@cs.arizona.edu for discovering
   this */

/* #define Choice(x,y,z) ( ( (x) & (y) ) | ( ~(x) & (z) ) ) */
#define Choice(x,y,z)   ( (z) ^ ( (x) & ( (y) ^ (z) ) ) ) 
/* #define Majority(x,y,z) ( ((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)) ) */
#define Majority(x,y,z) ( ((x) & (y)) ^ ((z) & ((x) ^ (y))) )

#define S0(x) (ROTL32(30,(x)) ^ ROTL32(19,(x)) ^ ROTL32(10,(x))) 
#define S1(x) (ROTL32(26,(x)) ^ ROTL32(21,(x)) ^ ROTL32(7,(x)))

#define s0(x) (ROTL32(25,(x)) ^ ROTL32(14,(x)) ^ ((x) >> 3))
#define s1(x) (ROTL32(15,(x)) ^ ROTL32(13,(x)) ^ ((x) >> 10))

/* The initial expanding function.  The hash function is defined over an
   64-word expanded input array W, where the first 16 are copies of the input
   data, and the remaining 64 are defined by

        W[ t ] = s1(W[t-2]) + W[t-7] + s0(W[i-15]) + W[i-16]

   This implementation generates these values on the fly in a circular
   buffer - thanks to Colin Plumb, colin@nyx10.cs.du.edu for this
   optimization.
*/

#define EXPAND(W,i) \
( W[(i) & 15 ] += (s1(W[((i)-2) & 15]) + W[((i)-7) & 15] + s0(W[((i)-15) & 15])) )

/* The prototype SHA sub-round.  The fundamental sub-round is:

        T1 = h + S1(e) + Choice(e,f,g) + K[t] + W[t]
	T2 = S0(a) + Majority(a,b,c)
	a' = T1+T2
	b' = a
	c' = b
	d' = c
	e' = d + T1
	f' = e
	g' = f
	h' = g

   but this is implemented by unrolling the loop 8 times and renaming
   the variables
   ( h, a, b, c, d, e, f, g ) = ( a, b, c, d, e, f, g, h ) each
   iteration. */

/* It's crucial that DATA is only used once, as that argument will
 * have side effects. */
#define ROUND(a,b,c,d,e,f,g,h,k,data) do {	\
    h += S1(e) + Choice(e,f,g) + k + data;	\
    d += h;					\
    h += S0(a) + Majority(a,b,c);		\
  } while (0)

const uint8_t *
_nettle_sha256_compress_n(uint32_t *state, const uint32_t *table,
			  size_t blocks, const uint8_t *input)
{
  uint32_t A, B, C, D, E, F, G, H;     /* Local vars */

  A = state[0];
  B = state[1];
  C = state[2];
  D = state[3];
  E = state[4];
  F = state[5];
  G = state[6];
  H = state[7];

  for (; blocks > 0; blocks--)
    {
      uint32_t data[SHA256_DATA_LENGTH];
      unsigned i;
      const uint32_t *k;
      uint32_t *d;
      for (i = 0; i < SHA256_DATA_LENGTH; i++, input+= 4)
	{
	  data[i] = READ_UINT32(input);
	}

      /* Heavy mangling */
      /* First 16 subrounds that act on the original data */

      DEBUG(-1);
      for (i = 0, d = data, k = table; i<16; i+=8, k += 8, d+= 8)
	{
	  ROUND(A, B, C, D, E, F, G, H, k[0], d[0]); DEBUG(i);
	  ROUND(H, A, B, C, D, E, F, G, k[1], d[1]); DEBUG(i+1);
	  ROUND(G, H, A, B, C, D, E, F, k[2], d[2]);
	  ROUND(F, G, H, A, B, C, D, E, k[3], d[3]);
	  ROUND(E, F, G, H, A, B, C, D, k[4], d[4]);
	  ROUND(D, E, F, G, H, A, B, C, k[5], d[5]);
	  ROUND(C, D, E, F, G, H, A, B, k[6], d[6]); DEBUG(i+6);
	  ROUND(B, C, D, E, F, G, H, A, k[7], d[7]); DEBUG(i+7);
	}
  
      for (; i<64; i += 16, k+= 16)
	{
	  ROUND(A, B, C, D, E, F, G, H, k[ 0], EXPAND(data,  0)); DEBUG(i);
	  ROUND(H, A, B, C, D, E, F, G, k[ 1], EXPAND(data,  1)); DEBUG(i+1);
	  ROUND(G, H, A, B, C, D, E, F, k[ 2], EXPAND(data,  2)); DEBUG(i+2);
	  ROUND(F, G, H, A, B, C, D, E, k[ 3], EXPAND(data,  3)); DEBUG(i+3);
	  ROUND(E, F, G, H, A, B, C, D, k[ 4], EXPAND(data,  4)); DEBUG(i+4);
	  ROUND(D, E, F, G, H, A, B, C, k[ 5], EXPAND(data,  5)); DEBUG(i+5);
	  ROUND(C, D, E, F, G, H, A, B, k[ 6], EXPAND(data,  6)); DEBUG(i+6);
	  ROUND(B, C, D, E, F, G, H, A, k[ 7], EXPAND(data,  7)); DEBUG(i+7);
	  ROUND(A, B, C, D, E, F, G, H, k[ 8], EXPAND(data,  8)); DEBUG(i+8);
	  ROUND(H, A, B, C, D, E, F, G, k[ 9], EXPAND(data,  9)); DEBUG(i+9);
	  ROUND(G, H, A, B, C, D, E, F, k[10], EXPAND(data, 10)); DEBUG(i+10);
	  ROUND(F, G, H, A, B, C, D, E, k[11], EXPAND(data, 11)); DEBUG(i+11);
	  ROUND(E, F, G, H, A, B, C, D, k[12], EXPAND(data, 12)); DEBUG(i+12);
	  ROUND(D, E, F, G, H, A, B, C, k[13], EXPAND(data, 13)); DEBUG(i+13);
	  ROUND(C, D, E, F, G, H, A, B, k[14], EXPAND(data, 14)); DEBUG(i+14);
	  ROUND(B, C, D, E, F, G, H, A, k[15], EXPAND(data, 15)); DEBUG(i+15);
	}

      /* Update state */
      state[0] = A = state[0] + A;
      state[1] = B = state[1] + B;
      state[2] = C = state[2] + C;
      state[3] = D = state[3] + D;
      state[4] = E = state[4] + E;
      state[5] = F = state[5] + F;
      state[6] = G = state[6] + G;
      state[7] = H = state[7] + H;
#if SHA256_DEBUG
      fprintf(stderr, "99: %8x %8x %8x %8x %8x %8x %8x %8x\n",
	      state[0], state[1], state[2], state[3],
	      state[4], state[5], state[6], state[7]);
#endif
    }
  return input;
}

/* Borrowed from nettle (write-be32.c).  */

static void
_nettle_write_be32(size_t length, uint8_t *dst,
		   const uint32_t *src)
{
  size_t i;
  size_t words;
  unsigned leftover;
  
  words = length / 4;
  leftover = length % 4;

  for (i = 0; i < words; i++, dst += 4)
    WRITE_UINT32(dst, src[i]);

  if (leftover)
    {
      uint32_t word;
      unsigned j = leftover;
      
      word = src[i];
      
      switch (leftover)
	{
	default:
	  abort();
	case 3:
	  dst[--j] = (word >> 8) & 0xff;
	  /* Fall through */
	case 2:
	  dst[--j] = (word >> 16) & 0xff;
	  /* Fall through */
	case 1:
	  dst[--j] = (word >> 24) & 0xff;
	}
    }
}

/* Borrowed from nettle (sha256.c).  */

/* Generated by the shadata program. */
static const uint32_t
K[64] =
{
  0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 
  0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 
  0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 
  0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL, 
  0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 
  0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 
  0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 
  0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL, 
  0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 
  0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL, 
  0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 
  0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 
  0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 
  0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL, 
  0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 
  0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL, 
};

void
sha256_compress(uint32_t *state, const uint8_t *input)
{
  _nettle_sha256_compress_n(state, K, 1, input);
}

#define COMPRESS(ctx, data) (sha256_compress((ctx)->state, (data)))

/* Initialize the SHA values */

void
sha256_init(struct sha256_ctx *ctx)
{
  /* Initial values, also generated by the shadata program. */
  static const uint32_t H0[_SHA256_DIGEST_LENGTH] =
  {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL, 
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL, 
  };

  memcpy(ctx->state, H0, sizeof(H0));

  /* Initialize bit count */
  ctx->count = 0;
  
  /* Initialize buffer */
  ctx->index = 0;
}

void
sha256_update(struct sha256_ctx *ctx,
	      size_t length, const uint8_t *data)
{
  size_t blocks;
  if (ctx->index > 0)
    {
      /* Try to fill partial block */
      MD_FILL_OR_RETURN (ctx, length, data);
      sha256_compress (ctx->state, ctx->block);
      ctx->count++;
    }

  blocks = length >> 6;
  data = _nettle_sha256_compress_n (ctx->state, K, blocks, data);
  ctx->count += blocks;
  length &= 63;

  memcpy (ctx->block, data, length);
  ctx->index = length;
}

static void
sha256_write_digest(struct sha256_ctx *ctx,
		    size_t length,
		    uint8_t *digest)
{
  uint64_t bit_count;

  assert(length <= SHA256_DIGEST_SIZE);

  MD_PAD(ctx, 8, COMPRESS);

  /* There are 512 = 2^9 bits in one block */  
  bit_count = (ctx->count << 9) | (ctx->index << 3);

  /* This is slightly inefficient, as the numbers are converted to
     big-endian format, and will be converted back by the compression
     function. It's probably not worth the effort to fix this. */
  WRITE_UINT64(ctx->block + (SHA256_BLOCK_SIZE - 8), bit_count);
  sha256_compress(ctx->state, ctx->block);

  _nettle_write_be32(length, digest, ctx->state);
}

void
sha256_digest(struct sha256_ctx *ctx,
	      size_t length,
	      uint8_t *digest)
{
  sha256_write_digest(ctx, length, digest);
  sha256_init(ctx);
}

void
sha256_digest_no_pad(struct sha256_ctx *ctx,
                     size_t length,
                     uint8_t *digest)
{
  _nettle_write_be32(length, digest, ctx->state);
  sha256_init(ctx);
}
