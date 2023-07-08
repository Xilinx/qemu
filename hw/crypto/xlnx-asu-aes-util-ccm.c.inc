/*
 * QEMU model of the Xilinx ASU AES computation engine.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 * Shareable support for crypto libs that need adaptation to how
 * ASU-AES processes CCM.
 *
 * To use, include this file into xlnx-asu-aes-<crypto_lib>.c
 *
 * References:
 *  https://nvlpubs.nist.gov/nistpubs/Legacy/SP/nistspecialpublication800-38c.pdf
 */

static bool asu_aes_ccm_b0_adata(const void *b0)
{
    uint8_t flags = ((const uint8_t *)b0)[0];

    return (flags >> 6) & 1;
}

static unsigned asu_aes_ccm_b0_tlen(const void *b0)
{
    uint8_t flags = ((const uint8_t *)b0)[0];
    unsigned coded = (flags >> 3) & 7;

    /*
     * coded = (tlen - 2) / 2, thus tlen = 2 * coded + 2 = (coded + 1) * 2.
     *
     * That is, tlen is even between 4 and 16, inclusively. See sp800-38c, A.1
     */
    return (coded + 1) * 2;
}

static unsigned asu_aes_ccm_b0_qlen(const void *b0)
{
    uint8_t flags = ((const uint8_t *)b0)[0];
    unsigned coded = flags & 7;

    /*
     * coded = qlen - 1, thus qlen = coded + 1.
     *
     * That is, 2 <= qlen <= 8. See sp800-38c, A.1
     */
    return coded + 1;
}

static int asu_aes_ccm_b0_nonce(void *np, const void *b0)
{
    unsigned qlen = asu_aes_ccm_b0_qlen(b0);
    unsigned nlen = 15 - qlen;  /* sp800-38c, A.1 */

    if (nlen < 7 || nlen > 15) {
        return -qlen; /* Out of range */
    } else {
        memcpy(np, b0 + 1, nlen);
        return nlen;
    }
}

static uint64_t asu_aes_ccm_b0_plen(const void *b0)
{
    uint8_t be64[8] = { 0 };
    unsigned qlen = asu_aes_ccm_b0_qlen(b0);
    const void *qp = b0 + (128 / 8) - qlen;

    memcpy(be64 + 8 - qlen, qp, qlen);
    return ldq_be_p(be64);
}

static uint64_t asu_aes_ccm_b1_alen(const void *b1, size_t len, size_t *next)
{
    const uint8_t *ap = b1;
    unsigned need = 0;

    if (ap[0] != 0xff) {
        need = 2;
    } else if (ap[1] == 0xfe) {
        need = 6;
    } else if (ap[1] == 0xff) {
        need = 10;
    } else {
        return 0;  /* Invalid encoding */
    }

    if (len < need) {
        return 0;  /* Insufficient */
    }

    *next += need;
    switch (need) {
    case 2:
        return lduw_be_p(ap);
    case 6:
        return ldl_be_p(ap + 2);
    case 10:
        return ldq_be_p(ap + 2);
    default:
        /* Should never be here */
        error_setg(&error_abort, "Should not be here");
        return 0;
    }
}

/*
 * Extract <TAGlen, Nonce, Alen, Plen> from start of CCM aad.
 *
 * If B0.qlen is invalid, setup() is called with nlen = -B0.qlen.
 *
 * Return: the number of bytes consumed from 'din'.
 *         -1 if error.
 */
static ssize_t asu_aes_ccm_parse(XlnxAsuAes *s, size_t len, const void *din,
                                 bool (*setup)(XlnxAsuAes *s,
                                               uint64_t plen, uint64_t alen,
                                               unsigned tlen, int nlen,
                                               const void *nonce))
{
    size_t next;
    uint8_t nonce[16];
    unsigned tlen, nlen;
    uint64_t alen, plen;
    const uint8_t *b0, *b1;

    /* Has passed the parsing phase */
    if (s->cipher.aad_bcnt >= ASU_AES_BLKLEN * 2) {
        return 0;
    }

    if (s->cipher.aad_bcnt == 0) {
        /* Expect at least the entire B0 */
        if (len < ASU_AES_BLKLEN) {
            return -1;
        }

        b0 = din;
        next = ASU_AES_BLKLEN;

        if (!asu_aes_ccm_b0_adata(b0)) {
            /* With Adata==0, parsing can complete, and B1 not needed */
            alen = 0;
            goto config;
        }

        if (len == ASU_AES_BLKLEN) {
            /* With Adata==1 and B0 only, wait for B1 with B0 saved in iv_out */
            memcpy(s->cipher.be_iv_out, b0, ASU_AES_BLKLEN);
            return len;
        }

        /* Move on to parsing B1 */
        b1 = din + next;
        len -= next;
    } else {
        b0 = s->cipher.be_iv_out;
        b1 = din;
        next = 0;
    }

    alen = asu_aes_ccm_b1_alen(b1, len, &next);
    if (!alen) {
        return -1;
    }

 config:
    plen = asu_aes_ccm_b0_plen(b0);
    tlen = asu_aes_ccm_b0_tlen(b0);
    nlen = asu_aes_ccm_b0_nonce(nonce, b0);

    if (setup(s, plen, alen, tlen, nlen, nonce)) {
        return -1;
    }

    /* Set upper limit, with header length included */
    s->cipher.aad_bmax = alen;
    s->cipher.txt_bmax = plen;

    /*
     * If text is to follow, clear FIN, which was used to indicate
     * non block-multiple of AAD length.
     */
    if (plen) {
        s->cipher.fin_phase = false;
    }

    return (ssize_t)next;
}
