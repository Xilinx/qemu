/*
 * QEMU model of AMD/Xilinx Type-1 True Random Number Generator,
 * release 2.
 *
 * This is not a full device, but just an object to be embedded
 * into other devices based on this TRNG.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/misc/xlnx-trng1-r2.h"

#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#ifndef XLNX_TRNG1R2_ERR_DEBUG
#define XLNX_TRNG1R2_ERR_DEBUG 0
#endif

REG32(INT, 0x00)
    FIELD(INT, CERTF_RST, 5, 1)
    FIELD(INT, DTF_RST, 4, 1)
    FIELD(INT, DONE_RST, 3, 1)
    FIELD(INT, CERTF_EN, 2, 1)
    FIELD(INT, DTF_EN, 1, 1)
    FIELD(INT, DONE_EN, 0, 1)
REG32(STATUS, 0x04)
    FIELD(STATUS, QCNT, 9, 3)
    FIELD(STATUS, EAT, 4, 5)
    FIELD(STATUS, CERTF, 3, 1)
    FIELD(STATUS, QERTF, 2, 1)
    FIELD(STATUS, DFT, 1, 1)
    FIELD(STATUS, DONE, 0, 1)
REG32(CTRL, 0x08)
    FIELD(CTRL, PERSODISABLE, 10, 1)
    FIELD(CTRL, SINGLEGENMODE, 9, 1)
    FIELD(CTRL, EUMODE, 8, 1)
    FIELD(CTRL, PRNGMODE, 7, 1)
    FIELD(CTRL, TSTMODE, 6, 1)
    FIELD(CTRL, PRNGSTART, 5, 1)
    FIELD(CTRL, PRNGXS, 3, 1)
    FIELD(CTRL, TRSSEN, 2, 1)
    FIELD(CTRL, PRNGSRST, 0, 1)
REG32(CONF0, 0x0c)
    FIELD(CONF0, REPCOUNTTESTCUTOFF, 8, 9)
    FIELD(CONF0, DIT, 0, 5)
REG32(CONF1, 0x10)
    FIELD(CONF1, ADAPTPROPTESTCUTOFF, 8, 10)
    FIELD(CONF1, DLEN, 0, 8)
REG32(TSTENT, 0x14)
    FIELD(TSTENT, SINGLEBITRAW, 0, 1)

#define XLNX_TRNG1R2_R_MAX (R_TSTENT + 1)
QEMU_BUILD_BUG_ON(XLNX_TRNG1R2_R_MAX * 4 != sizeof_field(XlnxTRng1r2, regs));

/* Special register ragnes; see xlnx_trng1r2_regs_read/_write */
REG32(SEED_DATA_APER, 0x40)
REG32(CORE_OUTPUT, 0xc0)
#define XLNX_TRNG1R2_MR_MAX (A_CORE_OUTPUT + 4)

#define XLNX_PRNG_CALL(FN, S, ...)                    \
    ((S)->prng.cls->FN)((S)->prng.obj, ## __VA_ARGS__)

static void xlnx_prng_reset(XlnxTRng1r2 *s)
{
    XLNX_PRNG_CALL(uninstantiate, s);
    s->prng.seed_age = 0;
}

static GArray *xlnx_prng_gen_seed(XlnxTRng1r2 *s, const GArray *seed_material)
{
    return XLNX_PRNG_CALL(gen_seed, s, seed_material->data, seed_material->len);
}

static void xlnx_prng_set_seed(XlnxTRng1r2 *s, const GArray *seed)
{
    if (s->prng.seed_age == 0) {
        XLNX_PRNG_CALL(instantiate, s, seed);
    } else {
        XLNX_PRNG_CALL(reseed, s, seed);
    }

    s->prng.seed_age = 1; /* Seeded but no generation yet */
}

static void xlnx_prng_generate(XlnxTRng1r2 *s)
{
    XLNX_PRNG_CALL(generate, s, sizeof(s->rand.vals), NULL, 0);
    s->prng.seed_age++;
}

static bool xlnx_prng_seeded(XlnxTRng1r2 *s)
{
    uint32_t life;

    if (s->prng.seed_age == 0) {
        return false;  /* Never seeded after reset */
    }

    life = s->seed_life ? *(s->seed_life) : UINT32_MAX;
    if (life == UINT32_MAX) {
        return true;   /* Unlimited seed life, e.g., just a DRNG */
    }

    /* age == 1: no generation since seeded */
    return (s->prng.seed_age - 1) < life;
}

static void xlnx_prng_get_data(XlnxTRng1r2 *s)
{
    XLNX_PRNG_CALL(get_data, s, s->rand.vals, sizeof(s->rand.vals));
}

static GArray *xlnx_array_new0(guint esize, guint ecnt)
{
    GArray *ary;

    ary = g_array_sized_new(false, true, esize, ecnt);
    return g_array_set_size(ary, ecnt);
}

static Object *xlnx_trng1r2_parent(XlnxTRng1r2 *s)
{
    return OBJECT(s)->parent;
}

static bool xlnx_trng1r2_has_trss(XlnxTRng1r2 *s)
{
    Object *parent;

    if (!ARRAY_FIELD_EX32(s->regs, CTRL, TRSSEN)) {
        return false;
    }

    parent = xlnx_trng1r2_parent(s);
    if (parent && s->trss_avail) {
        return s->trss_avail(parent);
    }

    return true;
}

static bool xlnx_trng1r2_in_sreset(XlnxTRng1r2 *s)
{
    return ARRAY_FIELD_EX32(s->regs, CTRL, PRNGSRST);
}

static bool xlnx_trng1r2_tst_mode(XlnxTRng1r2 *s)
{
    return ARRAY_FIELD_EX32(s->regs, CTRL, TSTMODE) &&
           ARRAY_FIELD_EX32(s->regs, CTRL, TRSSEN);
}

static bool xlnx_trng1r2_eu_mode(XlnxTRng1r2 *s)
{
    /* Supported only in TSTMODE to read back injected entropy */
    return ARRAY_FIELD_EX32(s->regs, CTRL, EUMODE)
           && xlnx_trng1r2_tst_mode(s);
}

static bool xlnx_trng1r2_gen_mode(XlnxTRng1r2 *s)
{
    return ARRAY_FIELD_EX32(s->regs, CTRL, PRNGMODE);
}

static bool xlnx_trng1r2_single_mode(XlnxTRng1r2 *s)
{
    return ARRAY_FIELD_EX32(s->regs, CTRL, SINGLEGENMODE);
}

static bool xlnx_trng1r2_is_idle(XlnxTRng1r2 *s)
{
    return !ARRAY_FIELD_EX32(s->regs, CTRL, PRNGSTART);
}

static bool xlnx_trng1r2_is_seeding(XlnxTRng1r2 *s)
{
    return !xlnx_trng1r2_is_idle(s) && !xlnx_trng1r2_gen_mode(s);
}

static bool xlnx_trng1r2_is_nonstop(XlnxTRng1r2 *s)
{
    return xlnx_trng1r2_gen_mode(s) && !xlnx_trng1r2_single_mode(s);
}

static bool xlnx_trng1r2_is_autoproc(XlnxTRng1r2 *s)
{
    return s->autoproc_ctrl;
}

static size_t xlnx_trng1r2_ent_bcnt(XlnxTRng1r2 *s)
{
    return 16 * (1 + ARRAY_FIELD_EX32(s->regs, CONF1, DLEN));
}

static void xlnx_trng1r2_int_update(XlnxTRng1r2 *s)
{
    Object *parent = xlnx_trng1r2_parent(s);
    uint32_t sts, ien;
    bool on;

    if (!parent || !s->intr_update) {
        return;  /* No-op, since nowhere to send event */
    }

    sts = s->int_status;
    on = false;
    if (sts) {
        ien = s->regs[R_INT];

        on |= FIELD_EX32(sts, STATUS, CERTF) & FIELD_EX32(ien, INT, CERTF_EN);
        on |= FIELD_EX32(sts, STATUS, DFT)   & FIELD_EX32(ien, INT, DTF_EN);
        on |= FIELD_EX32(sts, STATUS, DONE)  & FIELD_EX32(ien, INT, DONE_EN);
    }

    s->intr_update(parent, on);
}

static void xlnx_trng1r2_clr_done(XlnxTRng1r2 *s)
{
    /*
     * Clearing of STATUS.DONE can be:
     * 1. reset/soft-reset, or
     * 2. certain state of R_CTRL, i.e.:
     *    When in non-stop generation mode, STATUS.DONE being set
     *    is unobservable by software, who is expected to poll
     *    STATUS.WCNT instead.
     *
     * More importantly:
     * 3. 1->0 transition of DONE-irq does not clear STATUS.DONE, and
     * 4. 1->0 transition of STATUS.DONE does not clear DONE-irq.
     */
    if (xlnx_trng1r2_is_nonstop(s) || xlnx_trng1r2_is_idle(s)) {
        ARRAY_FIELD_DP32(s->regs, STATUS, DONE, 0);
    }
}

static void xlnx_trng1r2_set_done(XlnxTRng1r2 *s)
{
    /* STATUS.DONE is set conditionally */
    ARRAY_FIELD_DP32(s->regs, STATUS, DONE, 1);
    xlnx_trng1r2_clr_done(s);

    /* DONE-irq is raised unconditionally */
    s->int_status |= R_STATUS_DONE_MASK;
    xlnx_trng1r2_int_update(s);
}

static void xlnx_trng1r2_set_wcnt(XlnxTRng1r2 *s, size_t wcnt)
{
    ARRAY_FIELD_DP32(s->regs, STATUS, QCNT, MIN(wcnt, (128 / 32)));

    s->rand.wcnt = wcnt;

    /*
     * In generation mode, regardless in idle or in generating,
     * DONE-irq is raised at every 128-bit multiple of QCNT.
     *
     * However, STATUS.DONE is set only conditionally.
     */
    if (!(wcnt % (128 / 32)) && ARRAY_FIELD_EX32(s->regs, CTRL, PRNGMODE)) {
        xlnx_trng1r2_set_done(s);
    }
}

static uint32_t xlnx_trng1r2_tstent_u32(XlnxTRng1r2 *s)
{
    size_t i, o;

    if (!xlnx_trng1r2_eu_mode(s)) {
        return 0;
    }

    if (!s->entropy.test_input) {
        return 0;
    }

    i = s->entropy.test_input->len;
    o = s->entropy.test_output + 4;
    if (o > i) {
        return 0;
    }

    s->entropy.test_output = o;
    xlnx_trng1r2_set_wcnt(s, (i - o) / 4);

    return ldl_be_p(s->entropy.test_input->data + o - 4);
}

static void xlnx_trng1r2_tstent_collect(XlnxTRng1r2 *s)
{
    if (!s->entropy.test_input
        || (s->entropy.test_input_vld & 255) != 255) {
        return;
    }

    /* Collect assembled octet */
    g_array_append_val(s->entropy.test_input, s->entropy.test_input_buf);
    s->entropy.test_input_buf = 0;
    s->entropy.test_input_vld = 0;

    /* Indicate available for readback */
    if (xlnx_trng1r2_eu_mode(s)) {
        xlnx_trng1r2_set_wcnt(s, (s->entropy.test_input->len / 4));
    }
}

static void xlnx_trng1r2_tstent_clr(XlnxTRng1r2 *s)
{
    if (xlnx_trng1r2_eu_mode(s)) {
        xlnx_trng1r2_set_wcnt(s, 0);
    }

    /* Octet assembly is affected only by reset/soft-reset */
    s->entropy.test_output = 0;
    if (s->entropy.test_input) {
        g_autoptr(GArray) te = s->entropy.test_input;

        s->entropy.test_input = NULL;
    }
}

static void xlnx_trng1r2_tstent_new(XlnxTRng1r2 *s)
{
    g_assert(s->entropy.test_input == NULL);

    xlnx_trng1r2_tstent_clr(s);
    s->entropy.test_input = g_array_new(false, false, 1);

    xlnx_trng1r2_tstent_collect(s);
}

static void xlnx_trng1r2_tstent_add(XlnxTRng1r2 *s, bool bit)
{
    /* Assemble into an octet */
    s->entropy.test_input_buf <<= 1;
    s->entropy.test_input_buf |= (uint8_t)bit;

    /* Use bit-mask instead of counter to gracefully avoid overflow */
    s->entropy.test_input_vld <<= 1;
    s->entropy.test_input_vld |= 1;

    xlnx_trng1r2_tstent_collect(s);
}

static GArray *xlnx_trng1r2_tstent_take(XlnxTRng1r2 *s)
{
    GArray *te = s->entropy.test_input;

    xlnx_trng1r2_tstent_clr(s);
    return te;
}

static void xlnx_trng1r2_personalize(XlnxTRng1r2 *s, GArray *sa)
{
    size_t sa_esiz = g_array_get_element_size(sa);
    size_t sa_ecnt = sa->len;
    size_t sa_bcnt = sa_esiz * sa_ecnt;
    size_t ps_ecnt = QEMU_ALIGN_UP(sizeof(s->sd384), sa_esiz) / sa_esiz;

    /* Extend seed-input to include the 384-bit personalization string */
    g_array_set_size(sa, (sa_ecnt + ps_ecnt));
    memcpy(sa->data + sa_bcnt, s->sd384, sizeof(s->sd384));
}

static GArray *xlnx_trng1r2_ext_seed(XlnxTRng1r2 *s)
{
    GArray *sa = xlnx_array_new0(1, sizeof(s->sd384));

    memcpy(sa->data, s->sd384, sizeof(s->sd384));
    return sa;
}

static GArray *xlnx_trng1r2_entropy(XlnxTRng1r2 *s)
{
    GArray *ent;

    if (xlnx_trng1r2_tst_mode(s)) {
        ent = xlnx_trng1r2_tstent_take(s);
        g_assert(ent);
    } else if (xlnx_trng1r2_has_trss(s)) {
        ent = xlnx_prng_get_entropy(xlnx_trng1r2_ent_bcnt(s),
                                    &s->entropy.trss_fake_cnt,
                                    &s->entropy.trss_seed);
    } else {
        /* Force entropy to all 0 when TRSS is not running */
        ent = xlnx_array_new0(1, xlnx_trng1r2_ent_bcnt(s));
    }

    return ent;
}

static GArray *xlnx_trng1r2_ent_seed(XlnxTRng1r2 *s)
{
    g_autoptr(GArray) seed_input = xlnx_trng1r2_entropy(s);

    /*
     * Seeding after a RAND reset is INSTANTIATE, and seed-material
     * includes personalization string.
     */
    if (s->prng.seed_age == 0
        && !ARRAY_FIELD_EX32(s->regs, CTRL, PERSODISABLE)) {
        xlnx_trng1r2_personalize(s, seed_input);
    }

    return xlnx_prng_gen_seed(s, seed_input);
}

static GArray *xlnx_trng1r2_tst_seed(XlnxTRng1r2 *s)
{
    if (!xlnx_trng1r2_is_seeding(s)) {
        return NULL;
    }

    if (!s->entropy.test_input) {
        return NULL;
    }

    if (s->entropy.test_input->len < xlnx_trng1r2_ent_bcnt(s)) {
        return NULL;
    }

    /* Create a seed from injected entropy of sufficient length */
    return xlnx_trng1r2_ent_seed(s);
}

static bool xlnx_trng1r2_seed(XlnxTRng1r2 *s)
{
    g_autoptr(GArray) seed = NULL;
    bool skip_entropy = ARRAY_FIELD_EX32(s->regs, CTRL, PRNGXS);

    /* Clear out old generated data */
    xlnx_trng1r2_set_wcnt(s, 0);

    /*
     * When entropy is not used for seeding, EXT_SEED_* should be
     * used as 'seed_material' input to CTR_DRBG_Update() defined
     * by NIST SP800-90ar1.
     *
     * Otherwise, 'seed_material' = ctr_df(entropy + per_strng)
     */
    if (skip_entropy) {
        seed = xlnx_trng1r2_ext_seed(s);
    } else if (!xlnx_trng1r2_tst_mode(s)) {
        seed = xlnx_trng1r2_ent_seed(s);
    } else {
        seed = xlnx_trng1r2_tst_seed(s);
        if (!seed) {
            /* Seeding is deferred until sufficient entropy injected */
            return false;
        }
    }

    xlnx_prng_set_seed(s, seed);
    return true;
}

static void xlnx_trng1r2_make(XlnxTRng1r2 *s)
{
    g_assert(s->rand.wcnt == 0);

    if (xlnx_prng_seeded(s)) {
        xlnx_prng_generate(s);
        xlnx_prng_get_data(s);
    } else if (xlnx_trng1r2_is_autoproc(s) && s->prng.seed_age) {
        xlnx_trng1r2_seed(s);   /* Auto-reseed on expiration */
        xlnx_prng_generate(s);
        xlnx_prng_get_data(s);
    } else {
        memset(s->rand.vals, 0, sizeof(s->rand.vals));
    }

    xlnx_trng1r2_set_wcnt(s, ARRAY_SIZE(s->rand.vals));
}

static uint32_t xlnx_trng1r2_get32(XlnxTRng1r2 *s)
{
    uint32_t n;
    unsigned wcnt;

    if (s->rand.wcnt == 0) {
        return 0;
    }

    wcnt = s->rand.wcnt;
    g_assert(wcnt <= ARRAY_SIZE(s->rand.vals));
    n = be32_to_cpu(s->rand.vals[ARRAY_SIZE(s->rand.vals) - wcnt]);

    wcnt--;
    xlnx_trng1r2_set_wcnt(s, wcnt);

    return n;
}

static uint32_t xlnx_trng1r2_core_output(XlnxTRng1r2 *s)
{
    uint32_t n;

    if (xlnx_trng1r2_eu_mode(s)) {
        return xlnx_trng1r2_tstent_u32(s);
    }

    if (xlnx_trng1r2_is_nonstop(s)) {
        g_assert(s->rand.wcnt); /* Should never be empty */
        n = xlnx_trng1r2_get32(s);

        if (!s->rand.wcnt) {
            /* Need to keep QCNT > 0 in non 1-shot mode */
            xlnx_trng1r2_make(s);
        }
    } else {
        n = xlnx_trng1r2_get32(s);
    }

    return n;
}

static void xlnx_trng1r2_ent_reset(XlnxTRng1r2 *s)
{
    uint64_t seed = s->entropy.trss_seed;
    uint64_t cnt = s->entropy.trss_fake_cnt;

    xlnx_trng1r2_tstent_clr(s);
    memset(&s->entropy, 0, sizeof(s->entropy));

    s->entropy.trss_seed = seed;
    s->entropy.trss_fake_cnt = cnt;
}

static uint64_t xlnx_trng1r2_int_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(reg->opaque);
    uint32_t *regs = s->regs;
    uint32_t i_sta = s->int_status;
    uint32_t v_new = val64;

    /*
     * *_RST only clears the interrupts, not the STATUS register, which
     * can be cleared by reset/soft-reset.  STATUS.DONE can also be
     * cleared by selected states of CTRL; see xlnx_trng1r2_clr_done().
     */
    if (FIELD_EX32(v_new, INT, CERTF_RST)) {
        i_sta = FIELD_DP32(i_sta, STATUS, CERTF, 0);
    }
    if (FIELD_EX32(v_new, INT, DTF_RST)) {
        i_sta = FIELD_DP32(i_sta, STATUS, DFT, 0);
    }
    if (FIELD_EX32(v_new, INT, DONE_RST)) {
        i_sta = FIELD_DP32(i_sta, STATUS, DONE, 0);
    }

    v_new &= R_INT_CERTF_EN_MASK | R_INT_DTF_EN_MASK | R_INT_DONE_EN_MASK;
    regs[R_INT] = v_new;

    s->int_status = i_sta;
    xlnx_trng1r2_int_update(s);

    return v_new;
}

static void xlnx_trng1r2_tstent_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(reg->opaque);
    g_autoptr(GArray) seed = NULL;

    if (!xlnx_trng1r2_tst_mode(s)) {
        return;
    }

    /* Collect the injection */
    xlnx_trng1r2_tstent_add(s, (val64 & 1));

    /*
     * Handle completion of pending seeding based on injected entropy.
     *
     * If in autoproc mode, do generate as well
     */
    seed = xlnx_trng1r2_tst_seed(s);
    if (!seed) {
        return;
    }

    xlnx_prng_set_seed(s, seed);
    xlnx_trng1r2_set_done(s);

    if (xlnx_trng1r2_is_autoproc(s)) {
        xlnx_trng1r2_make(s);
    }
}

static void xlnx_trng1r2_sreset(XlnxTRng1r2 *s)
{
    memset(&s->rand, 0, sizeof(s->rand));
    xlnx_prng_reset(s);
    xlnx_trng1r2_ent_reset(s);
    xlnx_trng1r2_set_wcnt(s, 0);

    s->autoproc_ctrl = 0;
    s->regs[R_STATUS] = 0;

    s->int_status = 0;
    xlnx_trng1r2_int_update(s);
}

static void xlnx_trng1r2_ctrl_on_start(XlnxTRng1r2 *s)
{
    if (ARRAY_FIELD_EX32(s->regs, CTRL, PRNGMODE)) {
        xlnx_trng1r2_make(s); /* setting STATUS.DONE is more complex */
    } else {
        /* Test-mode entropy injection can defer seeding DONE */
        if (xlnx_trng1r2_seed(s)) {
            xlnx_trng1r2_set_done(s);
        }
    }
}

static void xlnx_trng1r2_ctrl_updated(XlnxTRng1r2 *s,
                                      uint32_t v_reg, uint32_t v_new)
{
    uint32_t to_1s = find_bits_to_1(v_reg, v_new);
    uint32_t to_0s = find_bits_to_0(v_reg, v_new);
    uint32_t tggle = find_bits_changed(v_reg, v_new);
    bool started, starting;

    if (!tggle) {
        return;  /* No change: do nothing */
    }

    /* Soft-reset blocks everything else */
    if (FIELD_EX32(to_1s, CTRL, PRNGSRST)) {
        xlnx_trng1r2_sreset(s);
        return;
    }

    if (xlnx_trng1r2_in_sreset(s)) {
        return;
    }

    /* Activation or deactivation of entropy injection */
    if (FIELD_EX32(to_1s, CTRL, TSTMODE)) {
        xlnx_trng1r2_tstent_new(s);
    }
    if (FIELD_EX32(to_0s, CTRL, TSTMODE)) {
        xlnx_trng1r2_tstent_clr(s);
    }
    if (FIELD_EX32(to_0s, CTRL, EUMODE)) {
        xlnx_trng1r2_set_wcnt(s, 0);
    }

    /* Any toggle is a potential source causing STATUS.DONE to be cleared */
    xlnx_trng1r2_clr_done(s);

    /*
     * PRNGSTART, if suppressed by sreset, needs replayed as 0->1 transition
     * on 1->0 transition of PRNGSRST.
     */
    started  = FIELD_EX32(v_reg, CTRL, PRNGSTART);
    starting = FIELD_EX32(to_1s, CTRL, PRNGSTART);

    if (started && FIELD_EX32(to_0s, CTRL, PRNGSRST)) {
        starting = true;
    }
    if (starting) {
        xlnx_trng1r2_ctrl_on_start(s);
        return;
    }
}

static uint64_t xlnx_trng1r2_ctrl_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(reg->opaque);
    uint32_t *p_reg = reg->data;
    uint32_t v_reg = *p_reg;
    uint32_t v_new = val64;

    /* Update reg to simplify implementing ctrl actions */
    *p_reg = v_new;
    xlnx_trng1r2_ctrl_updated(s, v_reg, v_new);

    return *p_reg;
}

static void xlnx_trng1r2_hreset(XlnxTRng1r2 *s)
{
    unsigned i;

    xlnx_trng1r2_sreset(s);
    s->entropy.trss_fake_cnt = 0;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static const RegisterAccessInfo xlnx_trng1r2_regs_info[] = {
    {   .name = "INT",  .addr = A_INT,
        .pre_write = xlnx_trng1r2_int_prew,
    },{ .name = "STATUS",  .addr = A_STATUS,
        .ro = 0xfff,
    },{ .name = "CTRL",  .addr = A_CTRL,
        .pre_write = xlnx_trng1r2_ctrl_prew,
    },{ .name = "CONF0",  .addr = A_CONF0,
        .reset = 0x210c,
    },{ .name = "CONF1",  .addr = A_CONF1,
        .reset = 0x26409,
    },{ .name = "TSTENT",  .addr = A_TSTENT,
        .post_write = xlnx_trng1r2_tstent_postw,
    },
};

static void xlnx_trng1r2_r384_write(XlnxTRng1r2 *s, hwaddr addr, uint32_t v32)
{
    /*
     * A write to the A_SEED_APER aperture loads the least-significant
     * 32 bits after shifting the 384-bit registers by 32.
     *
     * However, all writes are silently ignored if there is an action
     * operation.
     *
     * If there are 12 writes since a device reset, bits[383:352]
     * (the 1st 4 octes in an NIST test vector) is 1st write's value.
     *
     * If there are less than 12 writes, .e.g 11 writes, bits[383:352]
     * are zero.
     *
     * If there are more than 12 writes, .e.g 13 writes, bits[383:352]
     * are 2nd write's value, with 1st write's value discarded.
     */
    if (!ARRAY_FIELD_EX32(s->regs, CTRL, PRNGSTART)) {
        uint8_t *lsw = &s->sd384[(384 - 32) / 8];

        memmove(&s->sd384[0], &s->sd384[4], (lsw - s->sd384));
        stl_be_p(lsw, v32);
    }
}

static bool xlnx_trng1r2_regs_blocked(XlnxTRng1r2 *s, bool wr, hwaddr addr)
{
    Object *parent = xlnx_trng1r2_parent(s);

    /* Parent's gate-keeping */
    if (parent && s->accessible) {
        if (!(s->accessible(parent, wr))) {
            return true;
        }
    }

    /*
     * Autoproc's gate-keeping:
     * 1. All readable except CORE_OUTPUT.
     * 2. Only selected ones writable.
     */
    if (!xlnx_trng1r2_is_autoproc(s)) {
        return false;
    }

    if (!wr) {
        return addr == A_CORE_OUTPUT;
    }

    switch (addr) {
    case A_INT:
    case A_CONF0:
    case A_CONF1:
    case A_TSTENT:
        return false;
    default:
        return true;
    }
}

static void xlnx_trng1r2_regs_write(void *opaque, hwaddr addr,
                                    uint64_t value, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(reg_array->r[0]->opaque);

    if (xlnx_trng1r2_regs_blocked(s, true, addr)) {
        return;
    }

    /* Ignore writes to read-only register(s) */
    switch (addr) {
    case A_STATUS:
    case A_CORE_OUTPUT:
        return;
    }

    /* Writing seed-data aperture shift data into 384b shift register */
    switch (addr) {
    case A_SEED_DATA_APER ... (A_CORE_OUTPUT - 1):
        xlnx_trng1r2_r384_write(s, value, reg_array->debug);
        return;
    }

    register_write_memory(opaque, addr, value, size);
}

static uint64_t xlnx_trng1r2_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    RegisterInfoArray *reg_array = opaque;
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(reg_array->r[0]->opaque);

    if (xlnx_trng1r2_regs_blocked(s, false, addr)) {
        return 0;
    }

    /* Read on write-only returns 0 */
    switch (addr) {
    case A_CONF0 ... (A_TSTENT + 3):
        return 0;
    }

    switch (addr) {
    case A_CORE_OUTPUT:
        return xlnx_trng1r2_core_output(s);
    case A_SEED_DATA_APER ... (A_CORE_OUTPUT - 1):
        addr = A_STATUS; /* Read seed-material's aperture returns STATUS */
        break;
    }

    return register_read_memory(opaque, addr, size);
}

static const MemoryRegionOps xlnx_trng1r2_ops = {
    .read = xlnx_trng1r2_regs_read,
    .write = xlnx_trng1r2_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xlnx_trng1r2_autoproc_enter(XlnxTRng1r2 *s, uint32_t seeding_ctrl)
{
    /* Keep only relevant bits */
    seeding_ctrl &= R_CTRL_PERSODISABLE_MASK |
                    R_CTRL_TSTMODE_MASK |
                    R_CTRL_PRNGXS_MASK |
                    R_CTRL_TRSSEN_MASK |
                    R_CTRL_PRNGSTART_MASK;

    if (s->autoproc_ctrl == seeding_ctrl) {
        return; /* No change, so keep running as is */
    }

    /* Reset and reseed as requested */
    xlnx_trng1r2_sreset(s);

    s->autoproc_ctrl = seeding_ctrl;
    s->regs[R_CTRL] = seeding_ctrl;
    xlnx_trng1r2_ctrl_on_start(s);

    /*
     * Start generation only if seeding completes, which may not
     * happen if there insufficient entropy while in TSTMODE
     */
    if (ARRAY_FIELD_EX32(s->regs, STATUS, DONE)) {
        ARRAY_FIELD_DP32(s->regs, STATUS, DONE, 0);
        ARRAY_FIELD_DP32(s->regs, CTRL, PRNGMODE, 1);
        xlnx_trng1r2_ctrl_on_start(s);
    }
}

static void xlnx_trng1r2_autoproc_leave(XlnxTRng1r2 *s)
{
    s->autoproc_ctrl = 0;
    s->regs[R_CTRL] = 0;
    xlnx_trng1r2_sreset(s);
}

static void xlnx_trng1r2_autoproc(XlnxTRng1r2 *s, uint32_t seeding_ctrl)
{
    if (seeding_ctrl) {
        xlnx_trng1r2_autoproc_enter(s, seeding_ctrl);
    } else {
        xlnx_trng1r2_autoproc_leave(s);
    }
}

static void xlnx_trng1r2_get_data(XlnxTRng1r2 *s, void *out, size_t bcnt)
{
    while (bcnt >= 4) {
        stl_he_p(out, xlnx_trng1r2_core_output(s));
        out += 4;
        bcnt -= 4;
    }

    if (bcnt) {
        uint32_t u32 = xlnx_trng1r2_core_output(s);
        memcpy(out, &u32, bcnt);
    }
}

static void xlnx_trng1r2_reset_enter(Object *obj, ResetType type)
{
    xlnx_trng1r2_hreset(XLNX_TRNG1_R2(obj));
}

static void xlnx_trng1r2_realize(DeviceState *dev, Error **errp)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(dev);
    const char *type = s->prng.type;
    Object *prng;

    if (!type) {
        type = "xlnx-prng-non-crypto";
    }

    prng = object_new(type);
    if (!prng) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        error_setg(errp, "%s: PRNG type \'%s\' not supported", path, type);
        return;
    }

    s->prng.obj = XLNX_PRNG_IF(prng);
    s->prng.cls = XLNX_PRNG_IF_GET_CLASS(prng);
    s->prng.seed_age = 0;
}

static void xlnx_trng1r2_unrealize(DeviceState *dev)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(dev);

    xlnx_trng1r2_sreset(s);
    object_unref(s->prng.obj);
}

static void xlnx_trng1r2_init(Object *obj)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(obj);
    RegisterInfoArray *reg_array;

    reg_array =
        register_init_block32(DEVICE(obj), xlnx_trng1r2_regs_info,
                              ARRAY_SIZE(xlnx_trng1r2_regs_info),
                              s->regs_info, s->regs,
                              &xlnx_trng1r2_ops,
                              XLNX_TRNG1R2_ERR_DEBUG,
                              XLNX_TRNG1R2_MR_MAX);

    s->iomem = &reg_array->mem;

    s->autoproc = xlnx_trng1r2_autoproc;
    s->get_data = xlnx_trng1r2_get_data;
    s->hard_rst = xlnx_trng1r2_hreset;
}

static void xlnx_trng1r2_prop_fault_event_set(Object *obj, Visitor *v,
                                              const char *name, void *opaque,
                                              Error **errp)
{
    XlnxTRng1r2 *s = XLNX_TRNG1_R2(obj);
    Property *prop = opaque;
    uint32_t *pval = object_field_prop_ptr(obj, prop);
    uint32_t events = *pval;
    bool injected = false;

    if (!visit_type_uint32(v, name, pval, errp)) {
        return;
    }

    if (FIELD_EX32(events, STATUS, CERTF)) {
        ARRAY_FIELD_DP32(s->regs, STATUS, CERTF, 1);
        injected = true;
    }
    if (FIELD_EX32(events, STATUS, QERTF)) {
        ARRAY_FIELD_DP32(s->regs, STATUS, QERTF, 1);
        injected = true;
    }
    if (FIELD_EX32(events, STATUS, DFT)) {
        ARRAY_FIELD_DP32(s->regs, STATUS, DFT, 1);
        injected = true;
    }

    if (injected) {
        /* Once occured, fault(s) can only be cleared by reset/soft-reset */
        s->int_status |= s->regs[R_STATUS] & R_STATUS_CERTF_MASK;
        s->int_status |= s->regs[R_STATUS] & R_STATUS_QERTF_MASK;
        s->int_status |= s->regs[R_STATUS] & R_STATUS_DFT_MASK;

        xlnx_trng1r2_int_update(s);
    }
}

static const PropertyInfo xlnx_trng1r2_prop_fault_events = {
    .name = "uint32:bits",
    .description = "Set STATUS register's fault-event bits",
    .set = xlnx_trng1r2_prop_fault_event_set,
    .realized_set_allowed = true,
};

static Property xlnx_trng1r2_props[] = {
    DEFINE_PROP("fips-fault-events", XlnxTRng1r2, forced_faults,
                xlnx_trng1r2_prop_fault_events, uint32_t),

    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_trng1r2 = {
    .name = TYPE_XLNX_TRNG1_R2,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxTRng1r2, XLNX_TRNG1R2_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void xlnx_trng1r2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_trng1r2;
    dc->realize = xlnx_trng1r2_realize;
    dc->unrealize = xlnx_trng1r2_unrealize;
    rc->phases.enter = xlnx_trng1r2_reset_enter;

    device_class_set_props(dc, xlnx_trng1r2_props);
}

static const TypeInfo xlnx_trng1r2_info = {
    .name          = TYPE_XLNX_TRNG1_R2,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XlnxTRng1r2),
    .class_init    = xlnx_trng1r2_class_init,
    .instance_init = xlnx_trng1r2_init,
};

static void xlnx_trng1r2_register_types(void)
{
    type_register_static(&xlnx_trng1r2_info);
}

type_init(xlnx_trng1r2_register_types)
