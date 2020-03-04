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

typedef struct XlnxAES {
    DeviceState parent_obj;
    gcm_context gcm_ctx;
    const char *prefix;
    qemu_irq s_done;
    qemu_irq s_busy;

    /* Fields from here to the end will be autoreset to zero
       at reset.  */
    int32_t state;
    bool encrypt;
    bool tag_ok;
    bool key_zeroed;

    /* inp ready not directly derived from state because
       we will add delayed inp_ready handling at some point.  */
    bool inp_ready;
    uint32_t iv[4];
    uint32_t tag[4];
    uint32_t key[8];
    uint16_t keylen;
} XlnxAES;

enum XlnxAESState {
    IDLE,
    IV0,
    IV1,
    IV2,
    IV3,
    AAD,
    PAYLOAD,
    TAG0,
    TAG1,
    TAG2,
    TAG3
};

void xlnx_aes_write_key(XlnxAES *s, unsigned int pos, uint32_t val);
void xlnx_aes_load_key(XlnxAES *s, int len);
void xlnx_aes_key_zero(XlnxAES *s);
void xlnx_aes_start_message(XlnxAES *s, bool encrypt);
int xlnx_aes_push_data(XlnxAES *s,
                                uint8_t *data8, int len,
                                bool last_word , int lw_len,
                                uint8_t *outbuf, int *outlen);
int xlnx_aes_k256_get_provided(Object *obj, const char *id_prop,
                               const char *default_xd,
                               uint8_t (*key)[32], Error **errp);
uint32_t xlnx_aes_k256_crc(const uint32_t *k256, unsigned zpad_cnt);

#endif
