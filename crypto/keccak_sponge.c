/*
 * Keccak hashing's "absorb" and "squeeze" primitives
 *
 * Copyright (C) 2012 Niels Möller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"

#include "crypto/keccak_sponge.h"
#define sha3_state keccak_sponge

/* Borrowed from nettle. */

#define SHA3_ROUNDS 24

#define ROTL64(n, x) (((x)<<(n)) | ((x)>>(64-(n))))

static void sha3_permute(struct sha3_state *state)
{
    static const uint64_t rc[SHA3_ROUNDS] = {
        0x0000000000000001ULL, 0X0000000000008082ULL,
        0X800000000000808AULL, 0X8000000080008000ULL,
        0X000000000000808BULL, 0X0000000080000001ULL,
        0X8000000080008081ULL, 0X8000000000008009ULL,
        0X000000000000008AULL, 0X0000000000000088ULL,
        0X0000000080008009ULL, 0X000000008000000AULL,
        0X000000008000808BULL, 0X800000000000008BULL,
        0X8000000000008089ULL, 0X8000000000008003ULL,
        0X8000000000008002ULL, 0X8000000000000080ULL,
        0X000000000000800AULL, 0X800000008000000AULL,
        0X8000000080008081ULL, 0X8000000000008080ULL,
        0X0000000080000001ULL, 0X8000000080008008ULL,
    };

  /* Original permutation:

       0,10,20, 5,15,
      16, 1,11,21, 6,
       7,17, 2,12,22,
      23, 8,18, 3,13,
      14,24, 9,19, 4

     Rotation counts:

       0,  1, 62, 28, 27,
      36, 44,  6, 55, 20,
       3, 10, 43, 25, 39,
      41, 45, 15, 21,  8,
      18,  2, 61, 56, 14,
  */

  /* In-place implementation. Permutation done as a long sequence of
     25 moves "following" the permutation.

      T <--  1
      1 <--  6
      6 <--  9
      9 <-- 22
     22 <-- 14
     14 <-- 20
     20 <--  2
      2 <-- 12
     12 <-- 13
     13 <-- 19
     19 <-- 23
     23 <-- 15
     15 <--  4
      4 <-- 24
     24 <-- 21
     21 <--  8
      8 <-- 16
     16 <--  5
      5 <--  3
      3 <-- 18
     18 <-- 17
     17 <-- 11
     11 <--  7
      7 <-- 10
     10 <--  T

  */
    uint64_t C[5], D[5], T, X;
    unsigned i, y;

#define A (state->a)

    C[0] = A[0] ^ A[5+0] ^ A[10+0] ^ A[15+0] ^ A[20+0];
    C[1] = A[1] ^ A[5+1] ^ A[10+1] ^ A[15+1] ^ A[20+1];
    C[2] = A[2] ^ A[5+2] ^ A[10+2] ^ A[15+2] ^ A[20+2];
    C[3] = A[3] ^ A[5+3] ^ A[10+3] ^ A[15+3] ^ A[20+3];
    C[4] = A[4] ^ A[5+4] ^ A[10+4] ^ A[15+4] ^ A[20+4];

    for (i = 0; i < SHA3_ROUNDS; i++) {
        D[0] = C[4] ^ ROTL64(1, C[1]);
        D[1] = C[0] ^ ROTL64(1, C[2]);
        D[2] = C[1] ^ ROTL64(1, C[3]);
        D[3] = C[2] ^ ROTL64(1, C[4]);
        D[4] = C[3] ^ ROTL64(1, C[0]);

        A[0] ^= D[0];
        X = A[ 1] ^ D[1];     T = ROTL64(1, X);
        X = A[ 6] ^ D[1]; A[ 1] = ROTL64(44, X);
        X = A[ 9] ^ D[4]; A[ 6] = ROTL64(20, X);
        X = A[22] ^ D[2]; A[ 9] = ROTL64(61, X);
        X = A[14] ^ D[4]; A[22] = ROTL64(39, X);
        X = A[20] ^ D[0]; A[14] = ROTL64(18, X);
        X = A[ 2] ^ D[2]; A[20] = ROTL64(62, X);
        X = A[12] ^ D[2]; A[ 2] = ROTL64(43, X);
        X = A[13] ^ D[3]; A[12] = ROTL64(25, X);
        X = A[19] ^ D[4]; A[13] = ROTL64( 8, X);
        X = A[23] ^ D[3]; A[19] = ROTL64(56, X);
        X = A[15] ^ D[0]; A[23] = ROTL64(41, X);
        X = A[ 4] ^ D[4]; A[15] = ROTL64(27, X);
        X = A[24] ^ D[4]; A[ 4] = ROTL64(14, X);
        X = A[21] ^ D[1]; A[24] = ROTL64( 2, X);
        X = A[ 8] ^ D[3]; A[21] = ROTL64(55, X); /* row 4 done */
        X = A[16] ^ D[1]; A[ 8] = ROTL64(45, X);
        X = A[ 5] ^ D[0]; A[16] = ROTL64(36, X);
        X = A[ 3] ^ D[3]; A[ 5] = ROTL64(28, X);
        X = A[18] ^ D[3]; A[ 3] = ROTL64(21, X); /* row 0 done */
        X = A[17] ^ D[2]; A[18] = ROTL64(15, X);
        X = A[11] ^ D[1]; A[17] = ROTL64(10, X); /* row 3 done */
        X = A[ 7] ^ D[2]; A[11] = ROTL64( 6, X); /* row 1 done */
        X = A[10] ^ D[0]; A[ 7] = ROTL64( 3, X);
        A[10] = T;                                /* row 2 done */

        D[0] = ~A[1] & A[2];
        D[1] = ~A[2] & A[3];
        D[2] = ~A[3] & A[4];
        D[3] = ~A[4] & A[0];
        D[4] = ~A[0] & A[1];

        A[0] ^= D[0] ^ rc[i]; C[0] = A[0];
        A[1] ^= D[1]; C[1] = A[1];
        A[2] ^= D[2]; C[2] = A[2];
        A[3] ^= D[3]; C[3] = A[3];
        A[4] ^= D[4]; C[4] = A[4];

        for (y = 5; y < 25; y += 5) {
            D[0] = ~A[y+1] & A[y+2];
            D[1] = ~A[y+2] & A[y+3];
            D[2] = ~A[y+3] & A[y+4];
            D[3] = ~A[y+4] & A[y+0];
            D[4] = ~A[y+0] & A[y+1];

            A[y+0] ^= D[0]; C[0] ^= A[y+0];
            A[y+1] ^= D[1]; C[1] ^= A[y+1];
            A[y+2] ^= D[2]; C[2] ^= A[y+2];
            A[y+3] ^= D[3]; C[3] ^= A[y+3];
            A[y+4] ^= D[4]; C[4] ^= A[y+4];
        }
    }
#undef A
}

static void memxor(uint8_t *a, const uint8_t *b, unsigned int len)
{
    int i;

    for (i = 0; i < len; i++) {
        *a = *a ^ *b;
         a++;
         b++;
    }
}

static void
sha3_absorb(struct sha3_state *state, unsigned length, const uint8_t *data)
{
    assert((length & 7) == 0);
#ifdef HOST_WORDS_BIGENDIAN
    {
        uint64_t *p;
        for (p = state->a; length > 0; p++, length -= 8, data += 8) {
            uint64_t v64;
            memcpy(&v64, data, sizeof v64);
            *p ^= cpu_to_le64(v64);
        }
    }
#else /* !WORDS_BIGENDIAN */
    memxor((uint8_t *) state->a, data, length);
#endif

    sha3_permute(state);
}

static void write_le64(unsigned length, uint8_t *dst,
                       uint64_t *src)
{
    unsigned i;
    unsigned words;
    unsigned leftover;

    words = length / 8;
    leftover = length % 8;

    for (i = 0; i < words; i++, dst += 8) {
        uint64_t v64 = cpu_to_le64(src[i]);
        memcpy(dst, &v64, sizeof v64);
    }

    if (leftover) {
        uint64_t word;
        word = src[i];

        do {
            *dst++ = word & 0xff;
            word >>= 8;
        } while (--leftover);
    }
}

void keccak_absorb(keccak_sponge_t *state, unsigned length, const void *block)
{
    sha3_absorb(state, length, block);
}

void keccak_squeeze(keccak_sponge_t *state, unsigned length, void *digest)
{
    write_le64(length, digest, state->a);
}
