/*
 * QEMU model of Xilinx CSU IPCores RSA5 4K accelerator.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#include <gcrypt.h>

/* 0x6E + 22 words. Each word is 192 bits.  */
#define BITS_PER_WORD 192
#define BYTES_PER_WORD (BITS_PER_WORD / 8)
#define WORDS_PER_REG 22
#define BYTES_PER_REG (BYTES_PER_WORD * WORDS_PER_REG)
#define NR_WORDS 0x84
#define RAMSIZE (NR_WORDS * (BITS_PER_WORD / 8))

#define REG_E 0
#define REG_M 1
#define REG_X 2
#define REG_Y 4
#define REG_MUL_RESULT 5

struct word {
    union {
        uint8_t  u8[BYTES_PER_WORD];
        /* For completeness.  */
        uint16_t u16[BITS_PER_WORD / 16];
        uint32_t u32[BITS_PER_WORD / 32];
        uint64_t u64[BITS_PER_WORD / 64];
    };
};

struct reg {
    union {
        uint8_t u8[BYTES_PER_REG];
        uint16_t u16[BYTES_PER_REG / 2];
        uint32_t u32[BYTES_PER_REG / 4];
        uint64_t u64[BYTES_PER_REG / 8];
        struct word words[WORDS_PER_REG];
    };
};

typedef struct IPCoresRSA {
    struct {
        union {
            uint8_t u8[RAMSIZE];
            uint32_t u32[RAMSIZE / 4];
            struct word words[0x84];
            struct reg regs[6];
        };
    } mem;

    uint32_t minv;
    uint32_t exp_result_shift;

    /* Used to track if words have defined values or not.  */
    unsigned char word_def[NR_WORDS];
} IPCoresRSA;

#define RSA_NO_ERROR        0
#define RSA_ZERO_EXPONENT   1
#define RSA_ZERO_MODULO     2
#define RSA_BAD_RRMOD       3
#define RSA_BAD_MINV        4

const char *rsa_strerror(int err);
static inline void rsa_set_minv(IPCoresRSA *s, uint32_t minv)
{
    s->minv = minv;
}

static inline void rsa_reset(IPCoresRSA *s)
{
    memset(s, 0, sizeof *s);
}

static inline void rsa_set_exp_result_shift(IPCoresRSA *s, uint32_t v)
{
    s->exp_result_shift = v;
}

int rsa_do_nop(IPCoresRSA *s, unsigned int bitlen, unsigned int digits);
int rsa_do_xor(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits);
int rsa_do_bin_mont(IPCoresRSA *s,
                    unsigned int a_addr, unsigned int b_addr,
                    unsigned int r_addr, unsigned int m2_addr,
                    unsigned int digits);
int rsa_do_gf_mod(IPCoresRSA *s,
                   unsigned int a_addr, unsigned int b_addr,
                   unsigned int r_addr, unsigned int m2_addr,
                   unsigned int digits);
int rsa_do_add(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits);
int rsa_do_sub(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits);
int rsa_do_mod_addr(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits);
int rsa_do_montmul(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits);
int rsa_do_exp(IPCoresRSA *s, unsigned int bitlen, unsigned int digits);
int rsa_do_exppre(IPCoresRSA *s, unsigned int bitlen,
                  unsigned int digits);
int rsa_do_mod(IPCoresRSA *s, unsigned int bitlen, unsigned int digits);
int rsa_do_rrmod(IPCoresRSA *s, unsigned int bitlen, unsigned int digits);
int rsa_do_mul(IPCoresRSA *s, unsigned int bitlen, unsigned int digits);
