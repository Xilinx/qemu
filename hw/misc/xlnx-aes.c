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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/misc/xlnx-aes.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/cutils.h"

#include "hw/fdt_generic_util.h"

#ifndef XLNX_AES_ERR_DEBUG
#define XLNX_AES_ERR_DEBUG 0
#endif

#define XLNX_AES(obj) \
     OBJECT_CHECK(XlnxAES, (obj), TYPE_XLNX_AES)

#define DPRINT(fmt, args...) do { \
        if (XLNX_AES_ERR_DEBUG) { \
            qemu_log("%s:" fmt, __func__, ##args);\
        } \
    } while (0)

/* Debug print without function-name prefix */
#define DPRINT_NP(fmt, args...) do { \
        if (XLNX_AES_ERR_DEBUG) { \
            qemu_log(fmt, ##args); \
        } \
    } while (0)

#define XLNX_AES_PACKED_LEN  sizeof(((XlnxAES *)0)->pack_buf.u8)

/* This implements a model of the Xlnx AES unit.  */
static const char *aes_state2str(enum XlnxAESState state)
{
    static const char *state2str[] = {
        [IDLE] = "IDLE",
        [IV] = "IV",
        [AAD] = "AAD",
        [PAYLOAD] = "PAYLOAD",
        [TAG0] = "TAG0",
        [TAG1] = "TAG1",
        [TAG2] = "TAG2",
        [TAG3] = "TAG3",
    };
    return state2str[state];
}

static int xlnx_check_state(XlnxAES *s,
                              enum XlnxAESState expected,
                              const char *descr)
{
    int err = 0;
    if (s->state != expected) {
        qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: %s, while state is %s (expected %s)\n",
                  s->prefix, descr,
                  aes_state2str(s->state), aes_state2str(expected));
        err = 1;
    }
    return err;
}

static void xlnx_aes_set_state(XlnxAES *s, enum XlnxAESState state)
{
    assert(state <= TAG3);
    s->state = state;

    if (s->state != IDLE) {
        s->inp_ready = 1;
    } else {
        s->inp_ready = 0;
    }

    if (state == AAD) {
        s->aad_ready = true;
    }
}

void xlnx_aes_write_key(XlnxAES *s, unsigned int pos, uint32_t val)
{
    if (xlnx_check_state(s, IDLE, "Loading key")) {
        return;
    }

    assert(pos < 8);
    /* Xlnx wants the key in big-endian.  */
    s->key[pos] = bswap32(val);
}

void xlnx_aes_load_key(XlnxAES *s, int len)
{
    int i;

    if (xlnx_check_state(s, IDLE, "Loading key")) {
        return;
    }

    DPRINT("AES KEY loaded (big endian):\n");
    for (i = 0; i < len / 32; i++) {
        DPRINT_NP("0x%08x ", s->key[i]);
    }
    DPRINT_NP("\n");

    s->keylen = len;
    s->key_zeroed = 0;
}

void xlnx_aes_key_zero(XlnxAES *s)
{
    if (xlnx_check_state(s, IDLE, "Clearing key")) {
        return;
    }
    memset(s->key, 0, sizeof s->key);
    s->key_zeroed = 1;
}

static void xlnx_aes_load_iv(XlnxAES *s)
{
    if (s->state == IV) {
        unsigned int keylen = s->keylen;
        int r, i;

        if (keylen == 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "CSU-AES: Data but no key!\n");
            /* Use zero key.  */
            memset(s->key, 0, sizeof s->key);
            keylen = 256;
        }
        r = gcm_init(&s->gcm_ctx, (void *) s->key, keylen);
        if (r != 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "CSU-AES: GCM init failed\n");
            return;
        }
        gcm_push_iv(&s->gcm_ctx, (void *) s->iv, 12, 16);
        DPRINT("IV (big endian):\n");
        for (i = 0; i < 4; i++) {
            DPRINT_NP("0x%08x ", s->iv[i]);
        }
        DPRINT_NP("\n");
    }
}

static bool xlnx_aes_pack_empty(XlnxAES *s)
{
    return s->pack_next == 0;
}

static bool xlnx_aes_pack_full(XlnxAES *s)
{
    return s->pack_next >= XLNX_AES_PACKED_LEN;
}

static bool xlnx_aes_pack_pad0(XlnxAES *s)
{
    /* Pad the packing buffer only if not empty and not full */
    int pad = XLNX_AES_PACKED_LEN - s->pack_next;

    if (pad > 0 && pad < XLNX_AES_PACKED_LEN) {
        memset(&s->pack_buf.u8[s->pack_next], 0, pad);
        s->pack_next = XLNX_AES_PACKED_LEN;

        return true;
    } else {
        return xlnx_aes_pack_full(s);
    }
}

static unsigned int xlnx_aes_pack_push(XlnxAES *s, const void *data,
                                       unsigned len, bool last_word)
{
    unsigned next, plen;

    assert(s->state != PAYLOAD); /* PAYLOAD not subject to packing */

    if (!len) {
        return 0;
    }

    next = s->pack_next;
    assert(next < XLNX_AES_PACKED_LEN);

    plen = MIN(len, XLNX_AES_PACKED_LEN - next);

    memcpy(&s->pack_buf.u8[next], data, plen);
    s->pack_next = next + plen;

    /*
     * Trigger padding if having packed end-of-message byte.
     * 1/ To pad shortened IV
     * 2/ To pad shortened TAG (on decrypt)
     * 3/ To pad AAD (on encrypt) to multiple of block-size (16 bytes)
     */
    if (plen == len && last_word) {
        xlnx_aes_pack_pad0(s);
    }

    return plen;
}

static unsigned xlnx_aes_load_aad(XlnxAES *s, const void *data, unsigned len)
{
    assert(s->aad_ready);

    /* Auto-reset packing if sourced from packer */
    if ((const void *)&s->pack_buf == data) {
        len = s->pack_next;
        s->pack_next = 0;
    }

    /* An empty or partial block stops aad */
    if (!len) {
        s->aad_ready = false;
        return 0;
    }

    if (len & 15) {
        s->aad_ready = false;
    }

    gcm_push_aad(&s->gcm_ctx, data, len);
    return len;
}

static unsigned xlnx_aes_push_aad(XlnxAES *s, const uint8_t *data,
                                  const unsigned len, bool is_aad, void *outbuf)
{
    unsigned pos = 0, blen;

    assert(!xlnx_check_state(s, AAD, "Loading AAD"));

    if (!is_aad) {
        /*
         * data is actual payload. Thus, AAD phase has ended,
         * and residual AAD from earlier push(es) must be flused.
         */
        xlnx_aes_load_aad(s, &s->pack_buf, 0);

        /* None consumed; pass all given data to PAYLOAD state */
        xlnx_aes_set_state(s, PAYLOAD);
        return 0;
    }

    /* The entire AAD goes straight through.  */
    memcpy(outbuf, data, len);

    if (!xlnx_aes_pack_empty(s)) {
        /* Combine with AAD from earlier pushes into a block */
        pos = xlnx_aes_pack_push(s, data, len, false);

        /* A partially packed buffer is not ready to be loaded yet */
        if (!xlnx_aes_pack_full(s)) {
            assert(pos == len);
            return len;
        }

        xlnx_aes_load_aad(s, &s->pack_buf, 0);
        assert(xlnx_aes_pack_empty(s));
    }

    /* Sink more AAD by the blocks */
    blen = QEMU_ALIGN_DOWN(len, XLNX_AES_PACKED_LEN);
    if (blen) {
        pos += xlnx_aes_load_aad(s, &data[pos], blen);
    }

    /* Collect AAD tail into the empty packing buffer */
    pos += xlnx_aes_pack_push(s, &data[pos], (len - pos), false);

    /* All data should have been consumed */
    assert(pos == len);
    return len;
}

static unsigned xlnx_aes_push_iv(XlnxAES *s, const void *data,
                                 unsigned len, bool last_word)
{
    int pos;

    assert(!xlnx_check_state(s, IV, "Loading IV"));

    /* Collect 16 bytes as IV */
    pos = xlnx_aes_pack_push(s, data, len, last_word);

    if (xlnx_aes_pack_full(s)) {
        memcpy(s->iv, &s->pack_buf, sizeof(s->iv));
        s->pack_next = 0;

        xlnx_aes_load_iv(s);
        xlnx_aes_set_state(s, AAD);
    }

    return pos;
}

void xlnx_aes_start_message(XlnxAES *s, bool encrypt)
{
    if (xlnx_check_state(s, IDLE, "Start message")) {
        /* Clean up then proceed anyway */
        xlnx_aes_set_state(s, IDLE);
        qemu_set_irq(s->s_busy, false);
    }
    /* Loading IV.  */
    xlnx_aes_set_state(s, IV);
    s->pack_next = 0;
    s->encrypt = encrypt;
    s->tag_ok = 0;

    qemu_set_irq(s->s_done, false);
    qemu_set_irq(s->s_busy, false);
}

static void xlnx_aes_done(XlnxAES *s)
{
    xlnx_aes_set_state(s, IDLE);
    qemu_set_irq(s->s_done, true);
    qemu_set_irq(s->s_busy, false);
}

/* Length is in bytes.  */
int xlnx_aes_push_data(XlnxAES *s,
                       const uint8_t *data8, unsigned len,
                       bool is_aad, bool last_word, int lw_len,
                       uint8_t *outbuf, int *outlen)
{
    unsigned pos = 0, opos = 0, plen;
    uint32_t v32;

    assert(!last_word || lw_len == 0 || lw_len == 4);
    qemu_set_irq(s->s_busy, true);

    while (pos < len) {
        plen = len - pos;
        switch (s->state) {
        case IDLE:
            qemu_log_mask(LOG_GUEST_ERROR, "AES: Data while idle\n");
            return len;
        case IV:
            pos += xlnx_aes_push_iv(s, &data8[pos], plen, last_word);
            break;
        case AAD:
            plen = xlnx_aes_push_aad(s, &data8[pos], plen, is_aad,
                                     outbuf + opos);
            pos += plen;
            opos += plen;
            break;
        case PAYLOAD:
            gcm_push_data(&s->gcm_ctx, s->encrypt ? AES_ENCRYPT : AES_DECRYPT,
                          outbuf + opos, &data8[pos], plen);
            pos += plen;
            opos += plen;
            break;
        case TAG0...TAG3:
            /* Only decrypt case has data here.  */
            assert(s->encrypt == 0);
            assert(len >= 4);

            memcpy(&v32, data8 + pos, 4);
            s->tag[s->state - TAG0] = v32;
            pos += 4;
            if (s->state == TAG3) {
                uint8_t tag[16];

                gcm_emit_tag(&s->gcm_ctx, tag, 16);
                s->tag_ok = memcmp(s->tag, tag, 16) == 0;
                if (XLNX_AES_ERR_DEBUG) {
                    qemu_hexdump(stderr, "expected-tag",
                                 (void *) s->tag, 16);
                    qemu_hexdump(stderr, "tag", (void *) tag, 16);
                }
                xlnx_aes_done(s);
                goto done;
            }
            xlnx_aes_set_state(s, s->state + 1);
            break;
        default:
            assert(0);
            break;
        }
    }

    /* 'last_word' is honored only for PAYLOAD phase */
    if (last_word && s->state == PAYLOAD) {
        if (s->encrypt) {
            /* Emit tag on end-of-message */
            gcm_emit_tag(&s->gcm_ctx, outbuf + opos, 16);
            opos += 16;
            xlnx_aes_done(s);
        } else {
            /* Receive 16-byte TAG to compare with calculated */
            xlnx_aes_set_state(s, TAG0);
            qemu_set_irq(s->s_busy, false);
        }
    }

done:
    if (outlen) {
        *outlen = opos;
    }
    return pos;
}

static void xlnx_aes_reset(DeviceState *dev)
{
    XlnxAES *s =  XLNX_AES(dev);

    s->state = IDLE;
    s->encrypt = false;
    s->tag_ok = false;
    s->key_zeroed = false;
    s->inp_ready = false;
    memset(s->iv, 0, 16);
    memset(s->tag, 0, 16);
    memset(s->key, 0, 32);
    s->keylen = 256;

    qemu_set_irq(s->s_done, false);
    qemu_set_irq(s->s_busy, false);
}

static void reset_handler(void *opaque, int n, int level)
{
    XlnxAES *s = XLNX_AES(opaque);

    if (level) {
        xlnx_aes_reset(DEVICE(s));
    }
}

static void xlnx_aes_realize(DeviceState *dev, Error **errp)
{
    XlnxAES *s =  XLNX_AES(dev);

    qdev_init_gpio_out(dev, &s->s_busy, 1);
    qdev_init_gpio_out(dev, &s->s_done, 1);
    qdev_init_gpio_in_named(dev, reset_handler, "reset", 1);
}

static void xlnx_aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xlnx_aes_reset;
    dc->realize = xlnx_aes_realize;
}

static const TypeInfo xlnx_aes_info = {
    .name          = TYPE_XLNX_AES,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XlnxAES),
    .class_init    = xlnx_aes_class_init,
};

static void xlnx_aes_types(void)
{
    type_register_static(&xlnx_aes_info);
}

type_init(xlnx_aes_types)
