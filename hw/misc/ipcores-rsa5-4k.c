/*
 * QEMU model of IPCores RSA5 4K accelerator.
 *
 * Copyright (c) 2013-2017 Xilinx Inc
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
#include "hw/misc/ipcores-rsa5-4k.h"

#include <gcrypt.h>

#define RSA_DEBUG 0
#define D(x)              \
    do {                  \
        if (RSA_DEBUG) {  \
            x;            \
        }                 \
    } while (0)

#define MAX_LEN 4224

static const char *err2str[] = {
    [RSA_NO_ERROR]      = "No error",
    [RSA_ZERO_EXPONENT] = "Zero Exponent",
    [RSA_ZERO_MODULO]   = "Zero Modulo",
    [RSA_BAD_RRMOD]     = "Bad RRMOD",
    [RSA_BAD_MINV]     = "Bad MINV",
};

const char *rsa_strerror(int err)
{
    assert(err < ARRAY_SIZE(err2str));
    return err2str[err];
}

/* Debug code to dump the contents of an MPI.  */
static void show_mpi(const char *prefix, gcry_mpi_t m)
{
    unsigned char *str;
    size_t len;

    gcry_mpi_aprint(GCRYMPI_FMT_HEX, &str, &len, m);
    printf("%s: %s\n", prefix, str);
    free(str);
}

/* Check if a given portion of a reg has defined contents.  */
static void check_reg_defined(IPCoresRSA *s, unsigned int regnr,
                              unsigned int bytelen)
{
    unsigned int word_len = bytelen / BYTES_PER_WORD;
    unsigned int word_offset = regnr * WORDS_PER_REG;
    unsigned int i;

    for (i = 0; i < word_len; i++) {
        if (s->word_def[word_offset + i] == false) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "reg %d used with undefined contents at word %d (%d)\n",
                           regnr, i, word_offset + i);
        }
    }
}

/*
 * Calculate -1/a mod 2^32
 */
static uint32_t mod2_32_inverse(uint32_t a)
{
    unsigned int c;
    uint32_t b = 1; /* a mod 2. */

    for (c = 0; c < 5; ++c) {
        b = (b * (2 - a * b));
    }
    return -b;
}

static bool rsa_verify_minv(IPCoresRSA *s)
{
    uint32_t minv;

    minv = s->mem.regs[REG_M].u32[0];
    minv = mod2_32_inverse(minv);

    if (minv != s->minv) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Wrong MINV, expected %8.8x but got %8.8x\n",
                      minv, s->minv);
        return false;
    }
    return true;
}

/* Load from reg into MPI.  */
static void load_mpi(gcry_mpi_t d, struct reg *s, unsigned int len)
{
    int i;

    /* We assume lengths are 32bit aligned.  */
    assert((len & 3) == 0);
    len /= 4;

    gcry_mpi_set_ui(d, 0);
    for (i = len - 1; i >= 0; i--) {
        gcry_mpi_mul_2exp(d, d, 32);
        gcry_mpi_add_ui(d, d, s->u32[i]);
    }
}

/* Store from MPI into reg.  */
static void store_mpi(struct reg *d, gcry_mpi_t s, unsigned int len)
{
    unsigned char *buf, *cbuf;
    size_t writelen;
    size_t buflen = 0;
    int i;
    int pos;

    gcry_mpi_aprint(GCRYMPI_FMT_STD, &buf, &buflen, s);
    cbuf = buf;

    /* Remove insignificant top zero bytes.  */
    for (i = 0; i < buflen; i++) {
        if (buf[i]) {
            break;
        }
        cbuf++;
        buflen--;
    }

    if (RSA_DEBUG) {
        printf("store mpi: ");
        for (i = 0; i < buflen; i++) {
            printf("%2.2x ", cbuf[i]);
        }
        printf("\n");
    }

    /* These are in big endian.  */
    writelen = buflen > len ? len : buflen;
    pos = 0;
    for (i = writelen - 1; i >= 0; i--) {
        d->u8[pos] = cbuf[i];
        pos++;
    }

    /* Zero the left-over.  */
    if (writelen < len) {
        for (i = writelen; i < len; i++) {
            d->u8[pos++] = 0;
        }
    }

    free(buf);
}

int rsa_do_nop(IPCoresRSA *s, unsigned int bitlen, unsigned int digits)
{
    return RSA_NO_ERROR;
}

/* Convert an unsigned mpi into a signed one.  */
static void mpi_to_signed(gcry_mpi_t a, unsigned int bytelen)
{
    int s_bit = bytelen * 8 - 1;

    if (gcry_mpi_test_bit(a, s_bit)) {
        gcry_mpi_t tmp;

        /*
         * Allocate an additional bit to workaround a bug in older
         * libgcrypt (at least 1.5.2). They allocate new space on demand but
         * seem to fail to zero it in certain cases. This of course leads
         * to incorrect computations.
         *
         * This makes sure the space is pre-allocated and zeroed out.
         */
        tmp = gcry_mpi_new(s_bit + 2);
        gcry_mpi_set_ui(tmp, 0);
        gcry_mpi_set_bit(tmp, s_bit + 1);

        gcry_mpi_sub(a, a, tmp);
        gcry_mpi_release(tmp);
    }
}

/* Convert a signed mpi into an unsigned one.  */
static void mpi_to_unsigned(gcry_mpi_t a, unsigned int bytelen)
{
    int s_bit = bytelen * 8;
    gcry_mpi_t r;

    if (gcry_mpi_cmp_ui(a, 0) < 0) {
        r = gcry_mpi_new(bytelen * 8 + 1);
        gcry_mpi_set_ui(r, 0);
        gcry_mpi_set_bit(r, s_bit);
        gcry_mpi_add(a, r, a);
        gcry_mpi_release(r);
    }
}

static void bin_xor(unsigned int bitlen,
                    gcry_mpi_t r, gcry_mpi_t a, gcry_mpi_t b)
{
    gcry_mpi_t res;
    int i;

    res = gcry_mpi_new(bitlen);
    gcry_mpi_set_ui(res, 0);
    for (i = 0; i < bitlen; i++) {
        bool bit_a;
        bool bit_b;

        bit_a = gcry_mpi_test_bit(a, i);
        bit_b = gcry_mpi_test_bit(b, i);
        if (bit_a ^ bit_b) {
            gcry_mpi_set_bit(res, i);
        }
    }

    gcry_mpi_set(r, res);
    gcry_mpi_release(res);
}

static void gf_reduce(unsigned int bitlen,
                      gcry_mpi_t r, gcry_mpi_t a, gcry_mpi_t m2)
{
    gcry_mpi_t res;
    int s_bit = bitlen - 1;

    res = gcry_mpi_new(bitlen);

    if (gcry_mpi_test_bit(a, s_bit)) {
        bin_xor(bitlen, res, a, m2);
    }

    gcry_mpi_set(r, res);
    gcry_mpi_release(res);
}

static void gf_lshift(unsigned int bitlen,
                      gcry_mpi_t r, gcry_mpi_t a, gcry_mpi_t m2,
                      unsigned int nr)
{
    gcry_mpi_t res;

    res = gcry_mpi_new(bitlen);

    gcry_mpi_set(res, a);
    while (nr--) {
        gcry_mpi_lshift(res, res, 1);
        gf_reduce(bitlen, res, res, m2);
    }

    gcry_mpi_set(r, res);
    gcry_mpi_release(res);
}

static void gf_mul(unsigned int bitlen,
                   gcry_mpi_t r, gcry_mpi_t a, gcry_mpi_t b, gcry_mpi_t m2)
{
    gcry_mpi_t res, tmp;
    unsigned int nr_bits = gcry_mpi_get_nbits(a);
    unsigned int i;

    res = gcry_mpi_new(bitlen);
    tmp = gcry_mpi_new(bitlen);
    gcry_mpi_set_ui(res, 0);
    gcry_mpi_set(tmp, b);

    for (i = 0; i < nr_bits; i++) {
        if (gcry_mpi_test_bit(a, i)) {
            bin_xor(bitlen, res, res, tmp);
        }
        gf_lshift(bitlen, tmp, tmp, m2, 1);
    }

    gcry_mpi_set(r, res);
    gcry_mpi_release(res);
    gcry_mpi_release(tmp);
}

int rsa_do_bin_mont(IPCoresRSA *s,
                    unsigned int a_addr, unsigned int b_addr,
                    unsigned int r_addr, unsigned int m2_addr,
                    unsigned int digits)
{
    gcry_mpi_t a, b, c, m2, r, q, tmp;
    unsigned int bytelen;
    unsigned int i;
    int ret = RSA_NO_ERROR;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    c = gcry_mpi_new(bytelen * 8);
    m2 = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);
    q = gcry_mpi_new(bytelen * 8);
    tmp = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);
    load_mpi(m2, (struct reg *) &s->mem.words[m2_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));
    D(show_mpi("m2", m2));

    gf_mul(MAX_LEN, c, a, b, m2);
    D(show_mpi("c", c));
    gf_lshift(MAX_LEN, c, c, m2, 32);
    for (i = 0; i < digits + 1; i++) {
        gcry_mpi_set_ui(tmp, 1);
        gcry_mpi_lshift(tmp, tmp, 32);

        gcry_mpi_rshift(q, c, i * 32);
        gcry_mpi_mod(q, q, tmp);

        gf_lshift(MAX_LEN, tmp, m2, m2, i * 32);
        gf_mul(MAX_LEN, tmp, q, tmp, m2);
        bin_xor(MAX_LEN, c, c, tmp);
    }

    gcry_mpi_rshift(c, c, (digits + 1) * 32);
    gcry_mpi_set(r, c);

    D(show_mpi("Result", r));

    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(c);
    gcry_mpi_release(m2);
    gcry_mpi_release(r);
    gcry_mpi_release(q);
    gcry_mpi_release(tmp);
    return ret;
}

int rsa_do_gf_mod(IPCoresRSA *s,
                  unsigned int a_addr, unsigned int b_addr,
                  unsigned int r_addr, unsigned int m2_addr,
                  unsigned int digits)
{
    gcry_mpi_t a, b, m2, r, bit, tmp;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;
    int ab;
    int bb = 0;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    m2 = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);
    bit = gcry_mpi_new(bytelen * 8);
    tmp = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);
    load_mpi(m2, (struct reg *) &s->mem.words[m2_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));

    gcry_mpi_set(r, a);
    if (gcry_mpi_cmp(a, b) <= 0) {
        goto done;
    }

    gcry_mpi_set(tmp, b);
    gcry_mpi_set_ui(bit, 1);

    for (ab = 0; gcry_mpi_cmp(bit, r) < 0; ab++) {
        if (gcry_mpi_cmp(bit, tmp) < 0) {
            bb = ab;
        }
        gcry_mpi_lshift(bit, bit, 1);
    }
    ab--;
    gcry_mpi_rshift(bit, bit, 1);
    assert(ab >= bb);
    gcry_mpi_lshift(tmp, tmp, ab - bb);

    for (; ab >= bb; ab--) {
        if (gcry_mpi_test_bit(a, ab)) {
            bin_xor(MAX_LEN, r, r, tmp);
        }
        gcry_mpi_rshift(bit, bit, 1);
        gcry_mpi_rshift(tmp, tmp, 1);
    }

done:
    D(show_mpi("Result", r));

    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(m2);
    gcry_mpi_release(r);
    gcry_mpi_release(bit);
    gcry_mpi_release(tmp);
    return ret;
}

int rsa_do_xor(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits)
{
    gcry_mpi_t a, b, r;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));

    bin_xor(bytelen * 8, r, a, b);
    D(show_mpi("Result", r));
    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(r);
    return ret;
}

int rsa_do_add(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits)
{
    gcry_mpi_t a, b, r;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));

    mpi_to_signed(a, bytelen);
    mpi_to_signed(b, bytelen);

    gcry_mpi_add(r, a, b);
    D(show_mpi("Result", r));
    mpi_to_unsigned(r, bytelen);

    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(r);
    return ret;
}

int rsa_do_sub(IPCoresRSA *s,
               unsigned int a_addr, unsigned int b_addr,
               unsigned int r_addr, unsigned int m2_addr,
               unsigned int digits)
{
    gcry_mpi_t a, b, r;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));

    mpi_to_signed(a, bytelen);
    mpi_to_signed(b, bytelen);

    gcry_mpi_sub(r, a, b);
    D(show_mpi("Result", r));
    mpi_to_unsigned(r, bytelen);

    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(r);
    return ret;
}

int rsa_do_mod_addr(IPCoresRSA *s,
                    unsigned int a_addr, unsigned int b_addr,
                    unsigned int r_addr, unsigned int m2_addr,
                    unsigned int digits)
{
    gcry_mpi_t a, b, r;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));

    mpi_to_signed(a, bytelen);
    mpi_to_signed(b, bytelen);
    gcry_mpi_mod(r, a, b);
    D(show_mpi("Result", r));
    mpi_to_unsigned(r, bytelen);

    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(r);
    return ret;
}

int rsa_do_montmul(IPCoresRSA *s,
                   unsigned int a_addr, unsigned int b_addr,
                   unsigned int r_addr, unsigned int m2_addr,
                   unsigned int digits)
{
    gcry_mpi_t a, b, m2, q, c, r;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;
    int i;

    bytelen = (digits + 1) * 4;

    a = gcry_mpi_new(bytelen * 8);
    b = gcry_mpi_new(bytelen * 8);
    m2 = gcry_mpi_new(bytelen * 8);
    q = gcry_mpi_new(bytelen * 8);
    c = gcry_mpi_new(bytelen * 8);
    r = gcry_mpi_new(bytelen * 8);

    load_mpi(a, (struct reg *) &s->mem.words[a_addr], bytelen);
    load_mpi(b, (struct reg *) &s->mem.words[b_addr], bytelen);
    load_mpi(m2, (struct reg *) &s->mem.words[m2_addr], bytelen);

    D(show_mpi("a", a));
    D(show_mpi("b", b));
    D(show_mpi("m2", m2));

    mpi_to_signed(a, bytelen);
    mpi_to_signed(b, bytelen);

    gcry_mpi_mul(c, a, b);
    gcry_mpi_lshift(c, c, 32);
    for (i = 0; i < digits + 2; i++) {
        gcry_mpi_set_ui(r, 1);
        gcry_mpi_lshift(r, r, 32);

        gcry_mpi_rshift(q, c, i * 32);
        gcry_mpi_mod(q, q, r);

        gcry_mpi_lshift(r, m2, i * 32);
        gcry_mpi_mul(r, q, r);
        gcry_mpi_add(c, c, r);
    }

    gcry_mpi_rshift(r, c, (digits + 2) * 32);
    mpi_to_unsigned(r, bytelen);
    D(show_mpi("Result", r));
    store_mpi((struct reg *) &s->mem.words[r_addr], r, bytelen);

    gcry_mpi_release(a);
    gcry_mpi_release(b);
    gcry_mpi_release(m2);
    gcry_mpi_release(q);
    gcry_mpi_release(c);
    gcry_mpi_release(r);
    return ret;
}

#define MIN_RSA_EXP_LEN (36  * 32)
int rsa_do_exp(IPCoresRSA *s, unsigned int bitlen, unsigned int digits)
{
    gcry_mpi_t e, x, m, r;
    unsigned int bytelen;
    int ret = RSA_NO_ERROR;

    bytelen = digits * 4;

    x = gcry_mpi_new(MAX_LEN);
    m = gcry_mpi_new(MAX_LEN);
    e = gcry_mpi_new(MAX_LEN);
    r = gcry_mpi_new(MAX_LEN);

    check_reg_defined(s, REG_X, bytelen);
    load_mpi(x, &s->mem.regs[REG_X], bytelen);
    load_mpi(e, &s->mem.regs[REG_E], bytelen);
    load_mpi(m, &s->mem.regs[REG_M], bytelen);

    D(show_mpi("REG_X", x));
    D(show_mpi("REG_E", e));
    D(show_mpi("REG_M", m));

    if (!gcry_mpi_cmp_ui(e, 0)) {
        return RSA_ZERO_EXPONENT;
    }

    if (!gcry_mpi_cmp_ui(m, 0)) {
        return RSA_ZERO_MODULO;
    }

    gcry_mpi_powm(r, x, e, m);
    D(show_mpi("Result", r));
    gcry_mpi_lshift(r, r, s->exp_result_shift);

    if (rsa_verify_minv(s) == false) {
        gcry_mpi_set_ui(r, 0);
        ret = RSA_BAD_MINV;
    }

    bytelen = bytelen * 8 < MIN_RSA_EXP_LEN ?
                  MIN_RSA_EXP_LEN / 8 : bytelen + 2 * 4;
    D(show_mpi("Result", r));
    mpi_to_unsigned(r, bytelen);
    store_mpi(&s->mem.regs[REG_Y], r, bytelen);

    /* Clear X, real HW will modify it.  */
    gcry_mpi_set_ui(r, 1);
    store_mpi(&s->mem.regs[REG_X], r, bytelen);

    gcry_mpi_release(x);
    gcry_mpi_release(m);
    gcry_mpi_release(e);
    gcry_mpi_release(r);
    return ret;
}

static void rsa_compute_rrmod(gcry_mpi_t dst, gcry_mpi_t m,
                              unsigned int nbits)
{
    gcry_mpi_set_ui(dst, 1);
    gcry_mpi_lshift(dst, dst, nbits);
    gcry_mpi_mulm(dst, dst, dst, m);
}

int rsa_do_exppre(IPCoresRSA *s, unsigned int bitlen,
                         unsigned int digits)
{
    gcry_mpi_t m, r, y;
    unsigned int bytelen;
    unsigned int nbits;

    bytelen = digits * 4;

    m = gcry_mpi_new(MAX_LEN);
    y = gcry_mpi_new(MAX_LEN);

    load_mpi(m, &s->mem.regs[REG_M], bytelen);
    load_mpi(y, &s->mem.regs[REG_Y], bytelen);

    if (!gcry_mpi_cmp_ui(m, 0)) {
        return RSA_ZERO_MODULO;
    }

    nbits = (bitlen / 32 + 2) * 32;
    r = gcry_mpi_new(nbits + 1);
    rsa_compute_rrmod(r, m, nbits);

    if (gcry_mpi_cmp(r, y) != 0) {
        return RSA_BAD_RRMOD;
    }

    gcry_mpi_release(m);
    gcry_mpi_release(y);
    gcry_mpi_release(r);

    return rsa_do_exp(s, bitlen, digits);
}

int rsa_do_mod(IPCoresRSA *s, unsigned int bitlen, unsigned int digits)
{
    gcry_mpi_t y, m, r;
    unsigned int bytelen, mpos;

    bytelen = digits * 4;

    y = gcry_mpi_new(MAX_LEN);
    m = gcry_mpi_new(MAX_LEN);
    r = gcry_mpi_new(MAX_LEN);

    load_mpi(y, &s->mem.regs[REG_Y], bytelen);
    load_mpi(m, &s->mem.regs[REG_M], bytelen);

    if (!gcry_mpi_cmp_ui(m, 0)) {
        return RSA_ZERO_MODULO;
    }

    mpos = bitlen / 32;
    mpos *= 16;

    gcry_mpi_rshift(m, m, mpos);

    gcry_mpi_mod(r, y, m);
    gcry_mpi_lshift(r, r, mpos + s->exp_result_shift);
    store_mpi(&s->mem.regs[REG_Y], r, MAX_LEN / 8);

    gcry_mpi_release(y);
    gcry_mpi_release(m);
    gcry_mpi_release(r);
    return RSA_NO_ERROR;
}

int rsa_do_rrmod(IPCoresRSA *s, unsigned int bitlen, unsigned int digits)
{
    gcry_mpi_t m, r;
    unsigned int bytelen;
    unsigned int nbits;

    bytelen = digits * 4;

    m = gcry_mpi_new(MAX_LEN);

    load_mpi(m, &s->mem.regs[REG_M], bytelen);

    if (!gcry_mpi_cmp_ui(m, 0)) {
        return RSA_ZERO_MODULO;
    }

    nbits = (bitlen / 32 + 2) * 32;
    r = gcry_mpi_new(nbits + 1);
    rsa_compute_rrmod(r, m, nbits);

    D(show_mpi("M", m));
    D(show_mpi("Result Y", r));
    store_mpi(&s->mem.regs[REG_Y], r, MAX_LEN / 8);

    gcry_mpi_release(m);
    gcry_mpi_release(r);
    return RSA_NO_ERROR;
}

int rsa_do_mul(IPCoresRSA *s, unsigned int bitlen, unsigned int digits)
{
    gcry_mpi_t x, y, r;

    x = gcry_mpi_new(MAX_LEN);
    y = gcry_mpi_new(MAX_LEN);
    r = gcry_mpi_new(MAX_LEN);

    load_mpi(x, &s->mem.regs[REG_X], 256);
    load_mpi(y, &s->mem.regs[REG_Y], 256);

    gcry_mpi_mul(r, x, y);

    /* Move the result into place.  */
    /* Remove the lower LSB part from x.  */
    gcry_mpi_rshift(x, r, 2080);
    gcry_mpi_lshift(x, x, 2080);

    /* Remove the lower MSB part from r.  */
    gcry_mpi_sub(r, r, x);
    /* Swap them.  */
    gcry_mpi_lshift(r, r, 2144);
    gcry_mpi_rshift(x, x, 2080);
    gcry_mpi_add(r, r, x);

    /* Write back the result.  */
    store_mpi(&s->mem.regs[REG_MUL_RESULT], r, 528);

    gcry_mpi_release(x);
    gcry_mpi_release(y);
    gcry_mpi_release(r);
    return RSA_NO_ERROR;
}
