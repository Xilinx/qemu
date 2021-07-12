/*
 * QEMU model of the Xilinx AES
 *
 * Copyright (c) 2018 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
 *            Sai Pavan Boddu <saipava@xilinx.com>
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
#ifndef XLNX_AES_H
#define XLNX_AES_H

#include "qemu/gcm.h"
#include "hw/qdev-core.h"

#define TYPE_XLNX_AES "xlnx-aes"

enum XlnxAESState {
    IDLE,
    IV,
    AAD,
    PAYLOAD,
    TAG0,
    TAG1,
    TAG2,
    TAG3
};

typedef struct XlnxAES {
    DeviceState parent_obj;
    gcm_context gcm_ctx;
    const char *prefix;
    qemu_irq s_done;
    qemu_irq s_busy;

    /* Fields from here to the end will be autoreset to zero at reset. */
    enum XlnxAESState state;
    bool encrypt;
    bool tag_ok;
    bool key_zeroed;

    /* inp ready not directly derived from state because
       we will add delayed inp_ready handling at some point.  */
    bool inp_ready;

    /* Once ended, aad-feed must end until next start of message */
    bool aad_ready;

    /* 16-byte packing is needed for stream-push of IV and AAD. */
    unsigned pack_next;
    union {
        uint8_t u8[16];
        uint32_t u32[4];
    } pack_buf;
    uint32_t iv[4];
    uint32_t tag[4];
    uint32_t key[8];
    uint16_t keylen;
} XlnxAES;

void xlnx_aes_write_key(XlnxAES *s, unsigned int pos, uint32_t val);
void xlnx_aes_load_key(XlnxAES *s, int len);
void xlnx_aes_key_zero(XlnxAES *s);
void xlnx_aes_start_message(XlnxAES *s, bool encrypt);
int xlnx_aes_push_data(XlnxAES *s,
                       const uint8_t *data8, unsigned len,
                       bool is_aad, bool last_word, int lw_len,
                       uint8_t *outbuf, int *outlen);
uint32_t xlnx_aes_k256_crc(const uint32_t *k256, unsigned zpad_cnt);

/*
 * Wrap calls with statement expression macros to do build-time
 * type-checking 'key' as a 32-byte fixed-length array.
 */
#define XLNX_AES_K256_TYPE_CHECK(x) \
    QEMU_BUILD_BUG_MSG((sizeof(x) != 32 || ARRAY_SIZE(x) != 32), \
                       #x " is not a 32-byte array")

#define xlnx_aes_k256_get_provided(_O, _ID, _D, _K, _E) ({              \
            XLNX_AES_K256_TYPE_CHECK(_K);                               \
            xlnx_aes_k256_get_provided_i((_O), (_ID), (_D), (_K), (_E)); \
        })

int xlnx_aes_k256_get_provided_i(Object *obj, const char *id_prop,
                                 const char *default_xd,
                                 uint8_t key[32], Error **errp);

#define xlnx_aes_k256_swap32(_DK, _SK) ({           \
            XLNX_AES_K256_TYPE_CHECK(_DK);          \
            XLNX_AES_K256_TYPE_CHECK(_SK);          \
            xlnx_aes_k256_swap32_i((_DK), (_SK));   \
        })

void xlnx_aes_k256_swap32_i(uint8_t dst[32], const uint8_t src[32]);

#define xlnx_aes_k256_is_zero(_K) ({                \
            XLNX_AES_K256_TYPE_CHECK(_K);           \
            xlnx_aes_k256_is_zero_i((_K));          \
        })

bool xlnx_aes_k256_is_zero_i(const uint8_t key[32]);

uint32_t xlnx_calc_crc(const uint32_t *data, unsigned data_length);

#endif
