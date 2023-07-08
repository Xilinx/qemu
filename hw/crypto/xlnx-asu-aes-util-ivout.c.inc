/*
 * QEMU model of the Xilinx ASU AES computation engine.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 * Shareable support for crypto libs that do not expose public API
 * to retrieve internal IV for NIST SP800-38A cipher modes of AES.
 *
 * To use, include this file into xlnx-asu-aes-<crypto_lib>.c
 *
 * References:
 *  https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38a.pdf
 *  https://www.highgo.ca/2019/08/08/the-difference-in-five-modes-in-the-aes-encryption-algorithm/
 */

/* 64-bit xor of unaligned buffers */
static void asu_aes_ivout_ua64_xor(void *r, const void *a, const void *b)
{
    /* Unaligned *r = *a ^ *b */
    uint64_t a64 = ldq_he_p(a);
    uint64_t b64 = ldq_he_p(b);

    stq_he_p(r, a64 ^ b64);
}

/* 128-bit xor of unaligned buffers */
static void asu_aes_ivout_ua128_xor(void *r, const void *a, const void *b)
{
    asu_aes_ivout_ua64_xor(r,     a,     b);
    asu_aes_ivout_ua64_xor(r + 8, a + 8, b + 8);
}

/* Derive next block's IV from plain-text and cipher-text of SP800-38A */
static void asu_aes_ivout(XlnxAsuAes *s, size_t len,
                          const void *din, const void *dout,
                          bool (*ctr_decrypt)(XlnxAsuAes *, void *))
{
    void *ivout = s->cipher.be_iv_out;
    const void *ct, *pt;
    uint64_t ctr[2], lo64;

    /*
     * IV-out is the additional context of selected NIST SP800-38a modes
     * needed to enc/dec next block, and can be saved/restored for context
     * switching.
     *
     * Unlike real hardware, there is no need for this model to extract
     * the IV-out after each block, because even for real hardware, there
     * is no stable way for software to handle context-switching until
     * last chunk of data has left the cipher and landed to the destination
     * by DMA.
     */
    switch (s->cipher.mode) {
    case ASU_AES_MODE_CBC:
    case ASU_AES_MODE_CFB:
    case ASU_AES_MODE_OFB:
    case ASU_AES_MODE_CTR:
        if (s->cipher.fin_phase && !(s->cipher.in_error)) {
            break;
        }
        __attribute__((fallthrough));
    default:
        ASU_AES_IZERO(ivout);
        return;
    }

    /*
     * This extraction works only if the data size is multiple of block-length.
     */
    ASU_AES_BUG((len % ASU_AES_BLKLEN) != 0);

    din  += len - ASU_AES_BLKLEN;
    dout += len - ASU_AES_BLKLEN;
    if (s->cipher.enc) {
        pt = din;
        ct = dout;
    } else {
        ct = din;
        pt = dout;
    }

    switch (s->cipher.mode) {
    case ASU_AES_MODE_CTR:
        break;
    case ASU_AES_MODE_CBC:
    case ASU_AES_MODE_CFB:
        /* Next-IV = this block's cipher text */
        memcpy(ivout, ct, ASU_AES_IVLEN);
        return;
    case ASU_AES_MODE_OFB:
        /* Next-IV = xor of this block's cipher text and plain text */
        asu_aes_ivout_ua128_xor(ivout, ct, pt);
        return;
    default:
        /* Should not reach here */
        ASU_AES_IZERO(ivout);
        return;
    }

    /*
     * Next block's counter value = 1 + counter_of_this_block
     *
     * This block's counter value
     *  = ecb_decrypt(cipher_text_of_this_block XOR plain_text_of_this_block)
     */
    ASU_AES_BUG(ctr_decrypt == NULL);

    asu_aes_ivout_ua128_xor(ctr, ct, pt);
    if (ctr_decrypt(s, ctr)) {
        ASU_AES_IZERO(ivout);
        return;
    }

    /* Do *(unaligned __uint128_t *)ivout = be128_to_cpu(ctr) + 1 */
    lo64 = be64_to_cpu(ctr[1]) + 1;
    if (lo64) {
        stq_be_p(ivout + 8, lo64);    /* Store as BE */
        stq_he_p(ivout, ctr[0]);      /* No change from decrypt */
    } else {
        stq_he_p(ivout + 8, 0);       /* Carry flows into upper 64 bits */
        stq_be_p(ivout, be64_to_cpu(ctr[0]) + 1);
    }
}
