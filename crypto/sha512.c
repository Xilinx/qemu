/*
 * sha512 hash function.
 *
 * Copyright (C) Niels Möller
 * Copyright (C) Joachim Strömbergson
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "crypto/sha2.h"

#define HAVE_NATIVE_sha512_compress 0
#define DEBUG(n)
#define SHA512_DEBUG 0
void
_nettle_sha512_compress(uint64_t *state, const uint8_t *input,
                        const uint64_t *k);

/* Borrowed from nettle (macros.h).  */

/* Reads a 64-bit integer, in network, big-endian, byte order */
#define READ_UINT64(p)				\
(  (((uint64_t) (p)[0]) << 56)			\
 | (((uint64_t) (p)[1]) << 48)			\
 | (((uint64_t) (p)[2]) << 40)			\
 | (((uint64_t) (p)[3]) << 32)			\
 | (((uint64_t) (p)[4]) << 24)			\
 | (((uint64_t) (p)[5]) << 16)			\
 | (((uint64_t) (p)[6]) << 8)			\
 |  ((uint64_t) (p)[7]))

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

#define ROTL64(n,x) (((x)<<(n)) | ((x)>>((-(n))&63)))

/* Currently used by sha512 (and sha384) only. */
#define MD_INCR(ctx) ((ctx)->count_high += !++(ctx)->count_low)

/* Takes the compression function f as argument. NOTE: also clobbers
   length and data. */
#define MD_UPDATE(ctx, length, data, f, incr)				\
  do {									\
    if ((ctx)->index)							\
      {									\
	/* Try to fill partial block */					\
	unsigned __md_left = sizeof((ctx)->block) - (ctx)->index;	\
	if ((length) < __md_left)					\
	  {								\
	    memcpy((ctx)->block + (ctx)->index, (data), (length));	\
	    (ctx)->index += (length);					\
	    goto __md_done; /* Finished */				\
	  }								\
	else								\
	  {								\
	    memcpy((ctx)->block + (ctx)->index, (data), __md_left);	\
									\
	    f((ctx), (ctx)->block);					\
	    (incr);							\
									\
	    (data) += __md_left;					\
	    (length) -= __md_left;					\
	  }								\
      }									\
    while ((length) >= sizeof((ctx)->block))				\
      {									\
	f((ctx), (data));						\
	(incr);								\
									\
	(data) += sizeof((ctx)->block);					\
	(length) -= sizeof((ctx)->block);				\
      }									\
    memcpy ((ctx)->block, (data), (length));				\
    (ctx)->index = (length);						\
  __md_done:								\
    ;									\
  } while (0)

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

/* Borrowed from nettle (sha512-compress.c)  */

/* A block, treated as a sequence of 64-bit words. */
#define SHA512_DATA_LENGTH 16

/* For fat builds */
#if HAVE_NATIVE_sha512_compress
void
_nettle_sha512_compress_c (uint64_t *state, const uint8_t *input, const uint64_t *k);
#define _nettle_sha512_compress _nettle_sha512_compress_c
#endif

/* The SHA512 functions. The Choice function is the same as the SHA1
   function f1, and the majority function is the same as the SHA1 f3
   function, and the same as for SHA256. */

#define Choice(x,y,z)   ( (z) ^ ( (x) & ( (y) ^ (z) ) ) ) 
#define Majority(x,y,z) ( ((x) & (y)) ^ ((z) & ((x) ^ (y))) )

#define S0(x) (ROTL64(36,(x)) ^ ROTL64(30,(x)) ^ ROTL64(25,(x))) 
#define S1(x) (ROTL64(50,(x)) ^ ROTL64(46,(x)) ^ ROTL64(23,(x)))

#define s0(x) (ROTL64(63,(x)) ^ ROTL64(56,(x)) ^ ((x) >> 7))
#define s1(x) (ROTL64(45,(x)) ^ ROTL64(3,(x)) ^ ((x) >> 6))

/* The initial expanding function. The hash function is defined over
   an 64-word expanded input array W, where the first 16 are copies of
   the input data, and the remaining 64 are defined by

        W[ t ] = s1(W[t-2]) + W[t-7] + s0(W[i-15]) + W[i-16]

   This implementation generates these values on the fly in a circular
   buffer.
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
   iteration. This code is then replicated 8, using the next 8 values
   from the W[] array each time */

/* It's crucial that DATA is only used once, as that argument will
 * have side effects. */
#define ROUND(a,b,c,d,e,f,g,h,k,data) do {	\
  h += S1(e) + Choice(e,f,g) + k + data;	\
  d += h;					\
  h += S0(a) + Majority(a,b,c);			\
} while (0)

void
_nettle_sha512_compress(uint64_t *state, const uint8_t *input, const uint64_t *k)
{
  uint64_t data[SHA512_DATA_LENGTH];
  uint64_t A, B, C, D, E, F, G, H;     /* Local vars */
  unsigned i;
  uint64_t *d;

  for (i = 0; i < SHA512_DATA_LENGTH; i++, input += 8)
    {
      data[i] = READ_UINT64(input);
    }

  /* Set up first buffer and local data buffer */
  A = state[0];
  B = state[1];
  C = state[2];
  D = state[3];
  E = state[4];
  F = state[5];
  G = state[6];
  H = state[7];
  
  /* Heavy mangling */
  /* First 16 subrounds that act on the original data */

  DEBUG(-1);
  for (i = 0, d = data; i<16; i+=8, k += 8, d+= 8)
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
  
  for (; i<80; i += 16, k+= 16)
    {
      ROUND(A, B, C, D, E, F, G, H, k[ 0], EXPAND(data,  0)); DEBUG(i);
      ROUND(H, A, B, C, D, E, F, G, k[ 1], EXPAND(data,  1)); DEBUG(i+1);
      ROUND(G, H, A, B, C, D, E, F, k[ 2], EXPAND(data,  2)); DEBUG(i+2);
      ROUND(F, G, H, A, B, C, D, E, k[ 3], EXPAND(data,  3));
      ROUND(E, F, G, H, A, B, C, D, k[ 4], EXPAND(data,  4));
      ROUND(D, E, F, G, H, A, B, C, k[ 5], EXPAND(data,  5));
      ROUND(C, D, E, F, G, H, A, B, k[ 6], EXPAND(data,  6));
      ROUND(B, C, D, E, F, G, H, A, k[ 7], EXPAND(data,  7));
      ROUND(A, B, C, D, E, F, G, H, k[ 8], EXPAND(data,  8));
      ROUND(H, A, B, C, D, E, F, G, k[ 9], EXPAND(data,  9));
      ROUND(G, H, A, B, C, D, E, F, k[10], EXPAND(data, 10));
      ROUND(F, G, H, A, B, C, D, E, k[11], EXPAND(data, 11));
      ROUND(E, F, G, H, A, B, C, D, k[12], EXPAND(data, 12));
      ROUND(D, E, F, G, H, A, B, C, k[13], EXPAND(data, 13));
      ROUND(C, D, E, F, G, H, A, B, k[14], EXPAND(data, 14)); DEBUG(i+14);
      ROUND(B, C, D, E, F, G, H, A, k[15], EXPAND(data, 15)); DEBUG(i+15);
    }

  /* Update state */
  state[0] += A;
  state[1] += B;
  state[2] += C;
  state[3] += D;
  state[4] += E;
  state[5] += F;
  state[6] += G;
  state[7] += H;
#if SHA512_DEBUG
  fprintf(stderr, "99: %8lx %8lx %8lx %8lx\n    %8lx %8lx %8lx %8lx\n",
	  state[0], state[1], state[2], state[3],
	  state[4], state[5], state[6], state[7]);
#endif
}

/* Borrowed from nettle (sha512.c).  */

static const uint64_t
K[80] =
{
  0x428A2F98D728AE22ULL,0x7137449123EF65CDULL,
  0xB5C0FBCFEC4D3B2FULL,0xE9B5DBA58189DBBCULL,
  0x3956C25BF348B538ULL,0x59F111F1B605D019ULL,
  0x923F82A4AF194F9BULL,0xAB1C5ED5DA6D8118ULL,
  0xD807AA98A3030242ULL,0x12835B0145706FBEULL,
  0x243185BE4EE4B28CULL,0x550C7DC3D5FFB4E2ULL,
  0x72BE5D74F27B896FULL,0x80DEB1FE3B1696B1ULL,
  0x9BDC06A725C71235ULL,0xC19BF174CF692694ULL,
  0xE49B69C19EF14AD2ULL,0xEFBE4786384F25E3ULL,
  0x0FC19DC68B8CD5B5ULL,0x240CA1CC77AC9C65ULL,
  0x2DE92C6F592B0275ULL,0x4A7484AA6EA6E483ULL,
  0x5CB0A9DCBD41FBD4ULL,0x76F988DA831153B5ULL,
  0x983E5152EE66DFABULL,0xA831C66D2DB43210ULL,
  0xB00327C898FB213FULL,0xBF597FC7BEEF0EE4ULL,
  0xC6E00BF33DA88FC2ULL,0xD5A79147930AA725ULL,
  0x06CA6351E003826FULL,0x142929670A0E6E70ULL,
  0x27B70A8546D22FFCULL,0x2E1B21385C26C926ULL,
  0x4D2C6DFC5AC42AEDULL,0x53380D139D95B3DFULL,
  0x650A73548BAF63DEULL,0x766A0ABB3C77B2A8ULL,
  0x81C2C92E47EDAEE6ULL,0x92722C851482353BULL,
  0xA2BFE8A14CF10364ULL,0xA81A664BBC423001ULL,
  0xC24B8B70D0F89791ULL,0xC76C51A30654BE30ULL,
  0xD192E819D6EF5218ULL,0xD69906245565A910ULL,
  0xF40E35855771202AULL,0x106AA07032BBD1B8ULL,
  0x19A4C116B8D2D0C8ULL,0x1E376C085141AB53ULL,
  0x2748774CDF8EEB99ULL,0x34B0BCB5E19B48A8ULL,
  0x391C0CB3C5C95A63ULL,0x4ED8AA4AE3418ACBULL,
  0x5B9CCA4F7763E373ULL,0x682E6FF3D6B2B8A3ULL,
  0x748F82EE5DEFB2FCULL,0x78A5636F43172F60ULL,
  0x84C87814A1F0AB72ULL,0x8CC702081A6439ECULL,
  0x90BEFFFA23631E28ULL,0xA4506CEBDE82BDE9ULL,
  0xBEF9A3F7B2C67915ULL,0xC67178F2E372532BULL,
  0xCA273ECEEA26619CULL,0xD186B8C721C0C207ULL,
  0xEADA7DD6CDE0EB1EULL,0xF57D4F7FEE6ED178ULL,
  0x06F067AA72176FBAULL,0x0A637DC5A2C898A6ULL,
  0x113F9804BEF90DAEULL,0x1B710B35131C471BULL,
  0x28DB77F523047D84ULL,0x32CAAB7B40C72493ULL,
  0x3C9EBE0A15C9BEBCULL,0x431D67C49C100D4CULL,
  0x4CC5D4BECB3E42B6ULL,0x597F299CFC657E2AULL,
  0x5FCB6FAB3AD6FAECULL,0x6C44198C4A475817ULL,
};

#define COMPRESS(ctx, data) (sha512_compress((ctx)->state, (data)))

void
sha512_init(struct sha512_ctx *ctx)
{
  /* Initial values, generated by the gp script
       {
         for (i = 1,8,
	   root = prime(i)^(1/2);
	   fraction = root - floor(root);
	   print(floor(2^64 * fraction));
	 );
       }
. */
  static const uint64_t H0[_SHA512_DIGEST_LENGTH] =
  {
    0x6A09E667F3BCC908ULL,0xBB67AE8584CAA73BULL,
    0x3C6EF372FE94F82BULL,0xA54FF53A5F1D36F1ULL,
    0x510E527FADE682D1ULL,0x9B05688C2B3E6C1FULL,
    0x1F83D9ABFB41BD6BULL,0x5BE0CD19137E2179ULL,
  };

  memcpy(ctx->state, H0, sizeof(H0));

  /* Initialize bit count */
  ctx->count_low = ctx->count_high = 0;
  
  /* Initialize buffer */
  ctx->index = 0;
}

void
sha512_update(struct sha512_ctx *ctx,
	      size_t length, const uint8_t *data)
{
  MD_UPDATE (ctx, length, data, COMPRESS, MD_INCR(ctx));
}

static void
sha512_write_digest(struct sha512_ctx *ctx,
		    size_t length,
		    uint8_t *digest)
{
  uint64_t high, low;

  unsigned i;
  unsigned words;
  unsigned leftover;

  assert(length <= SHA512_DIGEST_SIZE);

  MD_PAD(ctx, 16, COMPRESS);

  /* There are 1024 = 2^10 bits in one block */  
  high = (ctx->count_high << 10) | (ctx->count_low >> 54);
  low = (ctx->count_low << 10) | (ctx->index << 3);

  /* This is slightly inefficient, as the numbers are converted to
     big-endian format, and will be converted back by the compression
     function. It's probably not worth the effort to fix this. */
  WRITE_UINT64(ctx->block + (SHA512_BLOCK_SIZE - 16), high);
  WRITE_UINT64(ctx->block + (SHA512_BLOCK_SIZE - 8), low);
  sha512_compress(ctx->state, ctx->block);

  words = length / 8;
  leftover = length % 8;

  for (i = 0; i < words; i++, digest += 8)
    WRITE_UINT64(digest, ctx->state[i]);

  if (leftover)
    {
      /* Truncate to the right size */
      uint64_t word = ctx->state[i] >> (8*(8 - leftover));

      do {
	digest[--leftover] = word & 0xff;
	word >>= 8;
      } while (leftover);
    }
}

void
sha512_digest(struct sha512_ctx *ctx,
	      size_t length,
	      uint8_t *digest)
{
  assert(length <= SHA512_DIGEST_SIZE);

  sha512_write_digest(ctx, length, digest);
  sha512_init(ctx);
}

/* sha384 variant. */
void
sha384_init(struct sha512_ctx *ctx)
{
  /* Initial values, generated by the gp script
       {
         for (i = 9,16,
	   root = prime(i)^(1/2);
	   fraction = root - floor(root);
	   print(floor(2^64 * fraction));
	 );
       }
. */
  static const uint64_t H0[_SHA512_DIGEST_LENGTH] =
  {
    0xCBBB9D5DC1059ED8ULL, 0x629A292A367CD507ULL,
    0x9159015A3070DD17ULL, 0x152FECD8F70E5939ULL,
    0x67332667FFC00B31ULL, 0x8EB44A8768581511ULL,
    0xDB0C2E0D64F98FA7ULL, 0x47B5481DBEFA4FA4ULL,
  };

  memcpy(ctx->state, H0, sizeof(H0));

  /* Initialize bit count */
  ctx->count_low = ctx->count_high = 0;
  
  /* Initialize buffer */
  ctx->index = 0;
}

void
sha384_digest(struct sha512_ctx *ctx,
	      size_t length,
	      uint8_t *digest)
{
  assert(length <= SHA384_DIGEST_SIZE);

  sha512_write_digest(ctx, length, digest);
  sha384_init(ctx);
}

void
sha512_compress(uint64_t *state, const uint8_t *input)
{
  _nettle_sha512_compress(state, input, K);
}
