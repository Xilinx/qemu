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
