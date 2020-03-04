/*
 * General utilities to assist simulation of device keys.
 *
 * Copyright (c) 2019 Xilinx Inc.
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
#include "qapi/error.h"
#include "crypto/secret.h"
#include "hw/misc/xlnx-aes.h"

static int xlnx_aes_k256_xtob(const char *xs, uint8_t (*key)[256 / 8],
                              Error **errp)
{
    unsigned i;

    for (i = 0; i < 64; i += 2) {
        unsigned k8;
        int j;

        for (k8 = 0, j = i; j < (i + 2); j++) {
            gint x = g_ascii_xdigit_value(xs[j]);

            if (x == -1) {
                if (xs[j]) {
                    error_setg(errp, "Error - \"%.*s[%c: not a hex digit]%s\"",
                               j, xs, xs[j], &xs[j + 1]);
                } else {
                    error_setg(errp, "Error - \"%s\": %d hex digits < 64",
                               xs, j);
                }
                return -1;
            }

            k8 = (k8 << 4) | (x & 15);
        }

        (*key)[i / 2] = k8;
    }

    return 0;
}

static char *xlnx_aes_k256_get_secret(Object *obj, const char *id_prop,
                                      Error **errp)
{
    Error *local_err = NULL;
    char *secret_id, *data = NULL;
    int rc = 0;

    /* "No id" is treated as no secret */
    secret_id = object_property_get_str(obj, id_prop, NULL);
    if (!secret_id) {
        goto done;
    }

    data = qcrypto_secret_lookup_as_utf8(secret_id, &local_err);
    if (data) {
        data = g_strchomp(data);
        goto done;
    }

    /*
     * Object-not-found is handled gracefully by setting default.
     * Unfortunately, the only way to sniff out not-found is by
     * string-matching, a rather unrobust way.
     */
    if (!local_err) {
        error_setg(errp, "Secret id '%s' lookup failed: Unknown error",
                   secret_id);
        rc = -1;
    } else if (strncmp(error_get_pretty(local_err), "No secret with id", 17)) {
        error_propagate(errp, local_err);
        rc = -1;
    } else {
        error_free(local_err);
    }

 done:
    g_free(secret_id);
    if (!rc && !data) {
        data = g_strdup("");
    }

    return data;
}

int xlnx_aes_k256_get_provided(Object *obj, const char *id_prop,
                               const char *given_default,
                               uint8_t (*key)[256 / 8], Error **errp)
{
    static const char builtin_default[] =
        /* A pattern with all 32 bytes being unique */
        "01234567" "89abcdef" "02468ace" "13579bdf"
        "12345678" "9abcdef0" "2468ace0" "3579bdf1";

    const char *xd;
    char *data;
    int rc;

    assert(key != NULL);
    assert(obj != NULL);
    assert(id_prop != NULL);
    assert(id_prop[0] != '\0');

    /* Abort on unhandled errors */
    if (!errp) {
        errp = &error_abort;
    }

    data = xlnx_aes_k256_get_secret(obj, id_prop, errp);
    if (!data) {
        return -1;
    }

    xd = data;
    if (!xd[0]) {
        xd = given_default ? given_default : builtin_default;
    }

    rc = xlnx_aes_k256_xtob(xd, key, errp);
    g_free(data);

    return rc;
}

/*
 * Find AES256 key CRC for bbram and efuse.
 * k256[0]: BBRAM_0 or row_of(EFUSE_AES_START)
 * k256[7]: BBRAM_7 or row_of(EFUSE_AES_END)
 */
uint32_t xlnx_aes_k256_crc(const uint32_t *k256, unsigned zpad_cnt)
{
    /* A table for 7-bit slicing */
    static const uint32_t crc_tab[128] = {
        0x00000000, 0xe13b70f7, 0xc79a971f, 0x26a1e7e8,
        0x8ad958cf, 0x6be22838, 0x4d43cfd0, 0xac78bf27,
        0x105ec76f, 0xf165b798, 0xd7c45070, 0x36ff2087,
        0x9a879fa0, 0x7bbcef57, 0x5d1d08bf, 0xbc267848,
        0x20bd8ede, 0xc186fe29, 0xe72719c1, 0x061c6936,
        0xaa64d611, 0x4b5fa6e6, 0x6dfe410e, 0x8cc531f9,
        0x30e349b1, 0xd1d83946, 0xf779deae, 0x1642ae59,
        0xba3a117e, 0x5b016189, 0x7da08661, 0x9c9bf696,
        0x417b1dbc, 0xa0406d4b, 0x86e18aa3, 0x67dafa54,
        0xcba24573, 0x2a993584, 0x0c38d26c, 0xed03a29b,
        0x5125dad3, 0xb01eaa24, 0x96bf4dcc, 0x77843d3b,
        0xdbfc821c, 0x3ac7f2eb, 0x1c661503, 0xfd5d65f4,
        0x61c69362, 0x80fde395, 0xa65c047d, 0x4767748a,
        0xeb1fcbad, 0x0a24bb5a, 0x2c855cb2, 0xcdbe2c45,
        0x7198540d, 0x90a324fa, 0xb602c312, 0x5739b3e5,
        0xfb410cc2, 0x1a7a7c35, 0x3cdb9bdd, 0xdde0eb2a,
        0x82f63b78, 0x63cd4b8f, 0x456cac67, 0xa457dc90,
        0x082f63b7, 0xe9141340, 0xcfb5f4a8, 0x2e8e845f,
        0x92a8fc17, 0x73938ce0, 0x55326b08, 0xb4091bff,
        0x1871a4d8, 0xf94ad42f, 0xdfeb33c7, 0x3ed04330,
        0xa24bb5a6, 0x4370c551, 0x65d122b9, 0x84ea524e,
        0x2892ed69, 0xc9a99d9e, 0xef087a76, 0x0e330a81,
        0xb21572c9, 0x532e023e, 0x758fe5d6, 0x94b49521,
        0x38cc2a06, 0xd9f75af1, 0xff56bd19, 0x1e6dcdee,
        0xc38d26c4, 0x22b65633, 0x0417b1db, 0xe52cc12c,
        0x49547e0b, 0xa86f0efc, 0x8ecee914, 0x6ff599e3,
        0xd3d3e1ab, 0x32e8915c, 0x144976b4, 0xf5720643,
        0x590ab964, 0xb831c993, 0x9e902e7b, 0x7fab5e8c,
        0xe330a81a, 0x020bd8ed, 0x24aa3f05, 0xc5914ff2,
        0x69e9f0d5, 0x88d28022, 0xae7367ca, 0x4f48173d,
        0xf36e6f75, 0x12551f82, 0x34f4f86a, 0xd5cf889d,
        0x79b737ba, 0x988c474d, 0xbe2da0a5, 0x5f16d052
    };

    uint32_t crc = 0;
    unsigned j, k;

    /*
     * BBRAM check has a zero-u32 prepended; see:
     *  https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_bbramps_zynqmp.c#L311
     *
     * eFuse calculation is shown here:
     *  https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_utils.c#L1496
     *
     * Each u32 word is appended a 5-bit value, for a total of 37 bits; see:
     *  https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_utils.c#L1356
     */
    k = 8 + zpad_cnt;
    while (k--) {
        const unsigned rshf = 7;
        const uint32_t im = (1 << rshf) - 1;
        const uint32_t rm = (1 << (32 - rshf)) - 1;
        const uint32_t i2 = (1 << 2) - 1;
        const uint32_t r2 = (1 << 30) - 1;

        uint32_t i, r;
        uint64_t w;

        w = (uint64_t)(k + 1) << 32;
        w |= k > 7 ? 0 : k256[k];

        /* Feed 35 bits, in 5 rounds, each a slice of 7 bits */
        for (j = 0; j < 5; j++) {
            r = rm & (crc >> rshf);
            i = im & (crc ^ w);
            crc = crc_tab[i] ^ r;

            w >>= rshf;
        }

        /* Feed the remaining 2 bits */
        r = r2 & (crc >> 2);
        i = i2 & (crc ^ w);
        crc = crc_tab[i << (rshf - 2)] ^ r;
    }

    return crc;
}
