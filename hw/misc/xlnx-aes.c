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

/* This implements a model of the Xlnx AES unit.  */
static const char *aes_state2str(enum XlnxAESState state)
{
    static const char *state2str[] = {
        [IDLE] = "IDLE",
        [IV0] = "IV0",
        [IV1] = "IV1",
        [IV2] = "IV2",
        [IV3] = "IV3",
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

static void xlnx_aes_push_iv(XlnxAES *s, uint32_t v)
{
    if (s->state < IV0 || s->state > IV3) {
        xlnx_check_state(s, IV0, "Loading IV");
        return;
    }
    s->iv[s->state - IV0] = v;
    xlnx_aes_set_state(s, s->state + 1);

    if (s->state == AAD) {
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

void xlnx_aes_start_message(XlnxAES *s, bool encrypt)
{
    if (xlnx_check_state(s, IDLE, "Start message")) {
        /* Clean up then proceed anyway */
        xlnx_aes_set_state(s, IDLE);
        qemu_set_irq(s->s_busy, false);
    }
    /* Loading IV.  */
    xlnx_aes_set_state(s, IV0);
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
                                uint8_t *data8, int len,
                                bool last_word , int lw_len,
                                uint8_t *outbuf, int *outlen)
{
    int pos = 0, opos = 0, plen;
    uint32_t v32;

    qemu_set_irq(s->s_busy, true);

    while (pos < len) {
        switch (s->state) {
        case IDLE:
            qemu_log_mask(LOG_GUEST_ERROR, "AES: Data while idle\n");
            return len;
        case IV0...IV3:
            /* Slow.  */
            memcpy(&v32, data8 + pos, 4);
            xlnx_aes_push_iv(s, v32);
            pos += 4;
            break;
        case AAD:
            plen = len - pos;
            gcm_push_aad(&s->gcm_ctx, &data8[pos], plen);
            /* AAD goes straight through.  */
            memcpy(outbuf + opos, &data8[pos], plen);

            pos += plen;
            opos += plen;
            break;
        case PAYLOAD:
            plen = len - pos;
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
                    qemu_hexdump((void *) s->tag,
                                 stderr, "expected-tag", 16);
                    qemu_hexdump((void *) tag, stderr, "tag", 16);
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

    if (last_word) {
        /* Only supported value at the moment.  */
        assert(lw_len == 0 || lw_len == 4);
        xlnx_aes_set_state(s, s->state + 1);
        qemu_set_irq(s->s_busy, false);
    }

    /* Emit tag with last word of payload.  */
    if (s->state == TAG0 && s->encrypt) {
        gcm_emit_tag(&s->gcm_ctx, outbuf + opos, 16);
        opos += 16;
        xlnx_aes_done(s);
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
