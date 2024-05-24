/*
 * Non-crypto strength pseudo random number generator for AMD/Xilinx devices
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/misc/xlnx-prng-if.h"

#include "qemu/bswap.h"
#include "qemu/guest-random.h"

#define TYPE_XLNX_PRNG_NON_CRYPTO "xlnx-prng-non-crypto"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxPRngNonCrypto, XLNX_PRNG_NON_CRYPTO)

typedef struct XlnxPRngNonCrypto {
    Object parent;

    GRand *prng;
    struct {
        size_t  avail;
        uint8_t pbuf[4];
        uint8_t pcnt;
    } data;
} XlnxPRngNonCrypto;

static void xlnx_prng_ncs_set_seed(XlnxPRngNonCrypto *s, const GArray *seed)
{
    const void *input = seed->data;
    size_t bcnt = seed->len;
    size_t wcnt = DIV_ROUND_UP(bcnt, 4);
    g_autofree guint32 *g32 = g_malloc(4 * (wcnt + 1));
    guint32 *p32 = g32, *last;

    if (s->prng) {
        /*
         * This is reseed, and it is supposed to mix in current states,
         * i.e., after the call, the PRNG state shall be different from
         * that after intial seeding, even if the same seed is given to
         * both.
         */
        *p32++ = g_rand_int(s->prng);
    } else {
        s->prng = g_rand_new();
    }

    /*
     * Input seed is given in big-endian.  If size is not multiple of
     * 32 bits, it will be 0 padded.
     */
    for (last = p32 + (bcnt / 4); p32 < last; p32++) {
        *p32 = ldl_be_p(input);
        input += 4;
    }

    bcnt %= 4;
    if (bcnt) {
        *p32 = 0;
        memcpy(p32, input, bcnt);
        *p32 = be32_to_cpu(*p32);
        p32++;
    }

    /* Now set up the PRNG */
    wcnt = p32 - g32;
    g_rand_set_seed_array(s->prng, g32, wcnt);
}

static GArray *xlnx_prng_ncs_gen_seed(XlnxPRngIf *h,
                                      const void *input, size_t len)
{
    GArray *seed;

    /* Nothing fancy, just clone the input as a GArray */
    seed = g_array_sized_new(false, false, 1, len);
    g_array_set_size(seed, len);

    memcpy(seed->data, input, len);
    return seed;
}

static void xlnx_prng_ncs_uninstantiate(XlnxPRngIf *h)
{
    XlnxPRngNonCrypto *s = XLNX_PRNG_NON_CRYPTO(h);

    if (s->prng) {
        g_rand_free(s->prng);
        s->prng = NULL;
    }

    memset(&s->data, 0, sizeof(s->data));
}

static void xlnx_prng_ncs_instantiate(XlnxPRngIf *h, const GArray *seed)
{
    XlnxPRngNonCrypto *s = XLNX_PRNG_NON_CRYPTO(h);

    if (s->prng) {
        xlnx_prng_ncs_uninstantiate(h);
    }

    xlnx_prng_ncs_set_seed(s, seed);
    g_assert(s->prng);
}

static void xlnx_prng_ncs_reseed(XlnxPRngIf *h, const GArray *seed)
{
    XlnxPRngNonCrypto *s = XLNX_PRNG_NON_CRYPTO(h);

    xlnx_prng_ncs_set_seed(s, seed);
}

static void xlnx_prng_ncs_generate(XlnxPRngIf *h, size_t bcnt,
                                   const void *adi, size_t alen)
{
    XlnxPRngNonCrypto *s = XLNX_PRNG_NON_CRYPTO(h);

    memset(&s->data, 0, sizeof(s->data));
    s->data.avail = bcnt;

    /*
     * Different 'adi' (additional input) is supposed to cause
     * different sequence of generated values.  Implement that
     * by simply discarding some values from g_rand_int().
     */
    if (adi && alen) {
        uint8_t n;

        for (n = 0; alen--; adi++) {
            n += *(const uint8_t *)adi;
        }
        n &= 31;

        while (n--) {
            (void)g_rand_int(s->prng);
        }
    }
}

static ssize_t xlnx_prng_ncs_get_data(XlnxPRngIf *h, void *out, size_t bcnt)
{
    XlnxPRngNonCrypto *s = XLNX_PRNG_NON_CRYPTO(h);
    size_t pcnt;
    void *pbuf, *dout;

    if (!s) {
        return -1;
    }

    if (bcnt > s->data.avail) {
        bcnt = s->data.avail;
    }
    s->data.avail -= bcnt;

    if (!bcnt) {
        return 0;
    }

    /* Copy left-over from previous get_data */
    QEMU_BUILD_BUG_ON(sizeof(s->data.pbuf) != 4);
    g_assert(s->data.pcnt < 4);

    pcnt = s->data.pcnt;
    pbuf = s->data.pbuf + 4 - pcnt;
    if (bcnt <= pcnt) {
        memcpy(out, pbuf, bcnt);
        s->data.pcnt -= bcnt;
        return bcnt;
    }

    dout = out;
    if (pcnt > 0) {
        s->data.pcnt = 0;
        memcpy(dout, pbuf, pcnt);
        dout += pcnt;
        bcnt -= pcnt;
    }

    /* Emit group(s) of 4 octets */
    for ( ; bcnt >= 4; bcnt -= 4, dout += 4) {
        stl_be_p(dout, g_rand_int(s->prng));
    }

    /* Emit 1, 2, or 3 octets */
    if (bcnt > 0) {
        stl_be_p(s->data.pbuf, g_rand_int(s->prng));
        memcpy(dout, s->data.pbuf, bcnt);
        dout += bcnt;
        s->data.pcnt = 4 - bcnt;
    }

    return dout - out;
}

static void xlnx_prng_ncs_finalize(Object *h)
{
    xlnx_prng_ncs_uninstantiate(XLNX_PRNG_IF(h));
}

static void xlnx_prng_ncs_class_init(ObjectClass *klass, void *data)
{
    XlnxPRngIfClass *ifc = XLNX_PRNG_IF_CLASS(klass);

    ifc->uninstantiate = xlnx_prng_ncs_uninstantiate;
    ifc->instantiate = xlnx_prng_ncs_instantiate;
    ifc->reseed = xlnx_prng_ncs_reseed;
    ifc->generate = xlnx_prng_ncs_generate;
    ifc->get_data = xlnx_prng_ncs_get_data;
    ifc->gen_seed = xlnx_prng_ncs_gen_seed;
}

static const TypeInfo XlnxPRngNonCrypto_info = {
    .name = TYPE_XLNX_PRNG_NON_CRYPTO,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(XlnxPRngNonCrypto),
    .instance_finalize = xlnx_prng_ncs_finalize,
    .class_init = xlnx_prng_ncs_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_XLNX_PRNG_IF },
        { },
    }
};

static const TypeInfo XlnxPRngIf_info = {
    .name = TYPE_XLNX_PRNG_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(XlnxPRngIfClass),
};

static void register_types(void)
{
    type_register_static(&XlnxPRngIf_info);
    type_register_static(&XlnxPRngNonCrypto_info);
}

type_init(register_types);

/*
 * Generic util
 */
GArray *xlnx_prng_get_entropy(size_t len, uint64_t *fake_ctx,
                              const uint64_t *fake)
{
    GArray *ent;

    ent = g_array_sized_new(false, false, 1, len);
    g_array_set_size(ent, len);

    if (!fake_ctx || !fake || *fake == 0) {
        /* Non-reproducible entropy used */
        qemu_guest_getrandom_nofail(ent->data, ent->len);
    } else {
        /* Reproducible (aka, fake) entropy used */
        uint64_t en[2];
        size_t ez = sizeof(en);

        (*fake_ctx)++;
        en[0] = cpu_to_be64(*fake_ctx);
        en[1] = cpu_to_be64(*fake);
        if (ent->len <= ez) {
            memcpy(ent->data, en, ent->len);
        } else {
            memcpy(ent->data, en, ez);
            memset(ent->data + ez, 0, ent->len - ez);
        }
    }

    return ent;
}
