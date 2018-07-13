/*
 * QEMU model of ZynqMP CSU AES-GCM block
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"

#include "hw/stream.h"
#include "qemu/bitops.h"
#include "sysemu/dma.h"
#include "hw/register-dep.h"
#include "hw/zynqmp_aes_key.h"
#include "hw/fdt_generic_util.h"

#include "hw/misc/xlnx-aes.h"

#ifndef ZYNQMP_CSU_AES_ERR_DEBUG
#define ZYNQMP_CSU_AES_ERR_DEBUG 0
#endif

#define TYPE_ZYNQMP_CSU_AES "zynqmp.csu-aes"

#define ZYNQMP_CSU_AES(obj) \
     OBJECT_CHECK(ZynqMPCSUAES, (obj), TYPE_ZYNQMP_CSU_AES)

DEP_REG32(AES_STATUS, 0x00)
    DEP_FIELD(AES_STATUS, OKR_ZEROED, 1, 11)
    DEP_FIELD(AES_STATUS, BOOT_ZEROED, 1, 10)
    DEP_FIELD(AES_STATUS, KUP_ZEROED, 1, 9)
    DEP_FIELD(AES_STATUS, AES_KEY_ZEROED, 1, 8)
    DEP_FIELD(AES_STATUS, KEY_INIT_DONE, 1, 4)
    DEP_FIELD(AES_STATUS, GCM_TAG_PASS, 1, 3)
    DEP_FIELD(AES_STATUS, DONE, 1, 2)
    DEP_FIELD(AES_STATUS, READY, 1, 1)
    DEP_FIELD(AES_STATUS, BUSY, 1, 0)
DEP_REG32(AES_KEY_SRC, 0x04)
    DEP_FIELD(AES_KEY_SRC, KEY_SRC, 4, 0)

#define AES_KEYSRC_KUP        0
#define AES_KEYSRC_DEV        1

DEP_REG32(AES_KEY_LOAD, 0x08)
    DEP_FIELD(AES_KEY_LOAD, KEY_LOAD, 1, 0)
DEP_REG32(AES_START_MSG, 0x0c)
    DEP_FIELD(AES_START_MSG, START_MSG, 1, 0)
DEP_REG32(AES_RESET, 0x10)
    DEP_FIELD(AES_RESET, RESET, 1, 0)
DEP_REG32(AES_KEY_CLEAR, 0x14)
    DEP_FIELD(AES_KEY_CLEAR, AES_KUP_ZERO, 1, 1)
    DEP_FIELD(AES_KEY_CLEAR, AES_KEY_ZERO, 1, 0)
DEP_REG32(AES_CFG, 0x18)
    DEP_FIELD(AES_CFG, ENCRYPT_DECRYPT_N, 1, 0)
DEP_REG32(AES_KUP_WR, 0x1c)
    DEP_FIELD(AES_KUP_WR, IV_WRITE, 1, 1)
    DEP_FIELD(AES_KUP_WR, KUP_WRITE, 1, 0)
DEP_REG32(AES_KUP_0, 0x20)
DEP_REG32(AES_KUP_1, 0x24)
DEP_REG32(AES_KUP_2, 0x28)
DEP_REG32(AES_KUP_3, 0x2c)
DEP_REG32(AES_KUP_4, 0x30)
DEP_REG32(AES_KUP_5, 0x34)
DEP_REG32(AES_KUP_6, 0x38)
DEP_REG32(AES_KUP_7, 0x3c)
DEP_REG32(AES_IV_0, 0x40)
DEP_REG32(AES_IV_1, 0x44)
DEP_REG32(AES_IV_2, 0x48)
DEP_REG32(AES_IV_3, 0x4c)

#define R_MAX                      (R_AES_IV_3 + 1)

static const DepRegisterAccessInfo aes_regs_info[] = {
    { .name = "AES_STATUS",  .decode.addr = A_AES_STATUS,
        .reset = 0xf00,
        .rsvd = 0xe0,
        .ro = 0xfff,
    },{ .name = "AES_KEY_SRC",  .decode.addr = A_AES_KEY_SRC,
    },{ .name = "AES_KEY_LOAD",  .decode.addr = A_AES_KEY_LOAD,
    },{ .name = "AES_START_MSG",  .decode.addr = A_AES_START_MSG,
    },{ .name = "AES_KUP_WR",  .decode.addr = A_AES_KUP_WR,
    },{ .name = "AES_RESET",  .decode.addr = A_AES_RESET,
    },{ .name = "AES_KEY_CLEAR",  .decode.addr = A_AES_KEY_CLEAR,
    },{ .name = "AES_CFG",  .decode.addr = A_AES_CFG,
    },{ .name = "AES_KUP_0",  .decode.addr = A_AES_KUP_0,
    },{ .name = "AES_KUP_1",  .decode.addr = A_AES_KUP_1,
    },{ .name = "AES_KUP_2",  .decode.addr = A_AES_KUP_2,
    },{ .name = "AES_KUP_3",  .decode.addr = A_AES_KUP_3,
    },{ .name = "AES_KUP_4",  .decode.addr = A_AES_KUP_4,
    },{ .name = "AES_KUP_5",  .decode.addr = A_AES_KUP_5,
    },{ .name = "AES_KUP_6",  .decode.addr = A_AES_KUP_6,
    },{ .name = "AES_KUP_7",  .decode.addr = A_AES_KUP_7,
    },{ .name = "AES_IV_0",  .decode.addr = A_AES_IV_0,
        .ro = 0xffffffffL,
    },{ .name = "AES_IV_1",  .decode.addr = A_AES_IV_1,
        .ro = 0xffffffffL,
    },{ .name = "AES_IV_2",  .decode.addr = A_AES_IV_2,
        .ro = 0xffffffffL,
    },{ .name = "AES_IV_3",  .decode.addr = A_AES_IV_3,
        .ro = 0xffffffffL,
    }
};


/* This implements a model of the wrapper logic around the Helion unit.  */
typedef struct ZynqMPCSUAES {
    SysBusDevice busdev;
    MemoryRegion iomem;
    StreamSlave *tx_dev;

    XlnxAES *aes;
    qemu_irq aes_rst;
    bool aes_done;
    bool aes_busy;

    /* holds the device key beeing passed to us.  */
    uint32_t device_key[8];
    bool key_loaded;
    uint32_t data_count;
    uint32_t regs[R_MAX];
    DepRegisterInfo regs_info[R_MAX];

    union {
        struct {
            bool kup_write;
            bool boot_write;
            bool okr_write;
            bool iv_write;
            bool key_decrypt;
        };
        bool bl[5];
    } inputs;

    ZynqMPAESKeySink *boot_key;
    ZynqMPAESKeySink *okr_key;

    struct {
        uint32_t key[256 / 32];
        uint32_t iv[128 / 32];
    } feedback;

    StreamCanPushNotifyFn notify;
    void *notify_opaque;
    /* Debug only */
    const char *prefix;
    /* AES needs blocks of 16 bytes.  */
    uint8_t buf[16];
    uint8_t bufpos;
} ZynqMPCSUAES;

/* Xilinx wrapper logic.
 *
 * Disable AAD.
 * Cap encryption lengths to 256bit.
 */
static int xlx_aes_push_data(ZynqMPCSUAES *s,
                             uint8_t *data8x, int len,
                             bool last_word , int lw_len,
                             uint8_t *outbuf, int *outlen)
{
    uint8_t *wbuf = data8x;
    int wlen = len;
    int rlen = len;

//    printf("%s len=%d eop=%d\n", __func__, len, last_word);
    /* 16 bytes write buffer.  */
    if (s->aes->state != PAYLOAD && (s->bufpos || wlen < 16)) {
        unsigned int tocopy = MIN(16 - s->bufpos, wlen);

        memcpy(s->buf + s->bufpos, wbuf, tocopy);
        s->bufpos += tocopy;
        rlen = tocopy;
        assert(s->bufpos <= 16);

        /* Full block?  */
        if (s->bufpos == 16 || last_word) {
            last_word = (tocopy == wlen) && last_word;
            wbuf = s->buf;
            wlen = s->bufpos;
            s->bufpos = 0;
        } else {
            return tocopy;
        }
    }

    /* End the AAD phase after the 16 bytes of IV.  */
    if (s->data_count < 16) {
        int plen = MIN(16 - s->data_count, wlen);
        s->data_count += plen;
        last_word = s->data_count == 16;

        xlnx_aes_push_data(s->aes, wbuf, plen,
                             last_word, 4, NULL, NULL);
        return plen;
    }

    s->data_count += wlen;
    /* FIXME: Encryption of more than 256 might be HW limited??  */
    if (s->aes->encrypt && s->data_count > 32) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: encryption of more than 256 bits!\n", s->prefix);
    }
    xlnx_aes_push_data(s->aes, wbuf, wlen, last_word, lw_len,
                         outbuf, outlen);
    return rlen;
}

static uint32_t shift_in_u32(uint32_t *a, unsigned int size, uint32_t data)
{
    unsigned int i;
    uint32_t r = a[0];

    for (i = 1; i < size; i++) {
        a[i - 1] = a[i];
    }
    a[i - 1] = data;

    return r;
}

static void xlx_aes_feedback(ZynqMPCSUAES *s, unsigned char *buf, int len)
{
    bool key_feedback;
    bool kup_key_feedback;
    bool iv_feedback;
    int i;

    iv_feedback = s->inputs.iv_write;
    iv_feedback |= s->regs[R_AES_KUP_WR] & R_AES_KUP_WR_IV_WRITE_MASK;

    kup_key_feedback = s->inputs.kup_write;
    kup_key_feedback |= s->regs[R_AES_KUP_WR] & R_AES_KUP_WR_KUP_WRITE_MASK;

    key_feedback = kup_key_feedback;
    key_feedback |= s->inputs.okr_write | s->inputs.boot_write;

    assert((len & 3) == 0);

    for (i = 0; i < len; i += 4) {
        uint32_t data;
        memcpy(&data, buf + i, 4);

        if (iv_feedback) {
            data = shift_in_u32(s->feedback.iv, ARRAY_SIZE(s->feedback.iv),
                               data);
        }
        if (key_feedback) {
            shift_in_u32(s->feedback.key, ARRAY_SIZE(s->feedback.key), data);
        }
    }

    /* feedback the AES output into Key and IV storage.  */
    if (iv_feedback) {
        for (i = 0; i < ARRAY_SIZE(s->feedback.iv); i++) {
            s->regs[R_AES_IV_0 + i] = s->feedback.iv[i];
        }
    }
    if (s->inputs.kup_write | kup_key_feedback) {
        for (i = 0; i < ARRAY_SIZE(s->feedback.key); i++) {
            s->regs[R_AES_KUP_0 + i] = s->feedback.key[i];
        }
    }

    if (s->inputs.boot_write) {
        assert(s->boot_key);
        zynqmp_aes_key_update(s->boot_key,
                               (void *) s->feedback.key,
                               sizeof(s->feedback.key));
    }
    if (s->inputs.okr_write) {
        assert(s->okr_key);
        zynqmp_aes_key_update(s->okr_key,
                               (void *) s->feedback.key,
                               sizeof(s->feedback.key));
    }
}

static void bswap32_buf8(uint8_t *buf, int len)
{
    int i;

    assert((len & 3) == 0);
    for (i = 0; i < len; i += 4) {
        uint8_t v[4];

        v[0] = buf[i];
        v[1] = buf[i + 1];
        v[2] = buf[i + 2];
        v[3] = buf[i + 3];
        buf[i] = v[3];
        buf[i + 1] = v[2];
        buf[i + 2] = v[1];
        buf[i + 3] = v[0];
    }
}

static size_t xlx_aes_stream_push(StreamSlave *obj, uint8_t *buf, size_t len,
                                  uint32_t attr)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(obj);
    unsigned char outbuf[8 * 1024 + 16];
    int outlen = 0;
    bool feedback;
    size_t ret;

    /* When encrypting, we need to be prepared to receive the 16 byte tag.  */
    if (len > (sizeof(outbuf) - 16)) {
        len = sizeof(outbuf) - 16;
        attr &= ~STREAM_ATTR_EOP;
    }

    /* TODO: Add explicit eop to the stream interface.  */
    bswap32_buf8(buf, len);
    ret = xlx_aes_push_data(s, buf, len, stream_attr_has_eop(attr), 4,
                            outbuf, &outlen);
    bswap32_buf8(outbuf, outlen);

    /* No flow-control on the output.  */
    feedback = s->inputs.iv_write | s->inputs.kup_write;
    feedback |= s->inputs.boot_write | s->inputs.okr_write;
    feedback |= s->regs[R_AES_KUP_WR]
                & (R_AES_KUP_WR_IV_WRITE_MASK | R_AES_KUP_WR_KUP_WRITE_MASK);
    if (feedback) {
        xlx_aes_feedback(s, outbuf, outlen);
        memset(outbuf, 0, outlen);
    }
    stream_push(s->tx_dev, outbuf, outlen, attr);
/*
      printf("%s len=%zd ret=%zd outlen=%d eop=%d\n",
             __func__, len, ret, outlen, attr);
*/
    return ret;
}

static bool xlx_aes_stream_can_push(StreamSlave *obj,
                                    StreamCanPushNotifyFn notify,
                                    void *notify_opaque)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(obj);
/*    printf("%s: %d\n", __func__, s->aes->inp_ready); */
    return s->aes->inp_ready;
}

static void xlx_aes_write_key(ZynqMPCSUAES *s, unsigned int pos, uint32_t val)
{
    if (s->inputs.key_decrypt) {
        xlx_aes_stream_push(STREAM_SLAVE(s), (void *) &val, sizeof val, 0);
    } else {
        xlnx_aes_write_key(s->aes, pos, val);
    }
}

static void xlx_aes_load_key(ZynqMPCSUAES *s, int len)
{
    unsigned int src, i;
    src = (s->regs[R_AES_KEY_SRC] & R_AES_KEY_SRC_KEY_SRC_MASK)
           >> R_AES_KEY_SRC_KEY_SRC_SHIFT;

    switch (src) {
    case AES_KEYSRC_KUP:
        for (i = 0; i < 8; i++) {
            xlx_aes_write_key(s, i, s->regs[R_AES_KUP_0 + i]);
        }
        break;
    case AES_KEYSRC_DEV:
        for (i = 0; i < 8; i++) {
            xlx_aes_write_key(s, i, s->device_key[i]);
        }
    break;
    default:
        hw_error("%s: Unsupported AES Key source %d\n", s->prefix, src);
        break;
    }

    if (!s->inputs.key_decrypt) {
        xlnx_aes_load_key(s->aes, len);
    }
    s->key_loaded = true;
}

static uint64_t xlx_aes_read(void *opaque, hwaddr addr, unsigned size)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(opaque);
    uint32_t v;

    addr >>= 2;
    assert(addr < R_MAX);
    v = dep_register_read(&s->regs_info[addr]);
    switch (addr) {
    case R_AES_KUP_0...R_AES_KUP_7:
        v = 0;
        break;
    case R_AES_STATUS:
        v = 0;
        v |= R_AES_STATUS_BOOT_ZEROED_MASK;
        v |= R_AES_STATUS_OKR_ZEROED_MASK;
        v |= R_AES_STATUS_KUP_ZEROED_MASK;
        v |= s->key_loaded ? R_AES_STATUS_KEY_INIT_DONE_MASK : 0;
        v |= s->aes->key_zeroed ? R_AES_STATUS_AES_KEY_ZEROED_MASK : 0;
        v |= s->aes->tag_ok ? R_AES_STATUS_GCM_TAG_PASS_MASK : 0;
        v |= s->aes->inp_ready ? R_AES_STATUS_READY_MASK : 0;
        v |= s->aes_busy ? R_AES_STATUS_BUSY_MASK : 0;
        v |= s->aes_done ? R_AES_STATUS_DONE_MASK : 0;
        break;
    default:
        break;
    }
    return v;
}

static void device_key_update(ZynqMPAESKeySink *obj, uint8_t *key, size_t len)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(obj);
    /* We only support MAX 256 bit keys at the moment.  */
    assert(len == 256 / 8);

    memcpy(s->device_key, key, len);
}

static void xlx_aes_reset(DeviceState *dev)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        dep_register_reset(&s->regs_info[i]);
    }
    qemu_irq_pulse(s->aes_rst);
    s->key_loaded = false;
    s->data_count = 0;
}

static void xlx_aes_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(opaque);

    addr >>= 2;
    dep_register_write(&s->regs_info[addr], value, ~0);

    switch (addr) {
    case R_AES_KEY_LOAD:
        if (value) {
            xlx_aes_load_key(s, 256);
        }
        break;
    case R_AES_START_MSG:
        if (value) {
            s->data_count = 0;
            xlnx_aes_start_message(s->aes,
                         s->regs[R_AES_CFG] & R_AES_CFG_ENCRYPT_DECRYPT_N_MASK);
        }
        break;
    case R_AES_RESET:
        if (value) {
            xlx_aes_reset(opaque);
        }
        break;
    case R_AES_KEY_CLEAR:
        if (value & R_AES_KEY_CLEAR_AES_KEY_ZERO_MASK) {
            xlnx_aes_key_zero(s->aes);
            s->regs[R_AES_KEY_CLEAR] &= ~R_AES_KEY_CLEAR_AES_KEY_ZERO_MASK;
            s->key_loaded = false;
        }
        if (value & R_AES_KEY_CLEAR_AES_KUP_ZERO_MASK) {
            s->regs[R_AES_KEY_CLEAR] &= ~R_AES_KEY_CLEAR_AES_KUP_ZERO_MASK;
            memset(&s->regs[R_AES_KUP_0], 0, 8 * 4);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps aes_ops = {
    .read = xlx_aes_read,
    .write = xlx_aes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void gpio_key_write_ctrl(void *opaque, int n, int level)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(opaque);
    assert(n < ARRAY_SIZE(s->inputs.bl));

    s->inputs.bl[n] = level;
}

static void aes_busy_update(void *opaque, int n, int level)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(opaque);

    s->aes_busy = level;
}

static void aes_done_update(void *opaque, int n, int level)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(opaque);

    s->aes_done = level;
}

static void aes_realize(DeviceState *dev, Error **errp)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < R_MAX; ++i) {
        DepRegisterInfo *r = &s->regs_info[i];

        *r = (DepRegisterInfo) {
            .data = (uint8_t *)&s->regs[i],
            .data_size = sizeof(uint32_t),
            .access = &aes_regs_info[i],
            .debug = ZYNQMP_CSU_AES_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        dep_register_init(r);
        qdev_pass_all_gpios(DEVICE(r), dev);
    }
    s->prefix = g_strdup_printf("%s:", object_get_canonical_path(OBJECT(s)));
    s->aes->prefix = s->prefix;
    qdev_init_gpio_in_named(dev, aes_busy_update, "busy", 1);
    qdev_init_gpio_in_named(dev, aes_done_update, "done", 1);
    qdev_init_gpio_out_named(dev, &s->aes_rst, "reset", 1);
    qdev_init_gpio_in_named(dev, gpio_key_write_ctrl, "key-wr", 5);
}

static void aes_init(Object *obj)
{
    ZynqMPCSUAES *s = ZYNQMP_CSU_AES(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    object_property_add_link(obj, "stream-connected-aes", TYPE_STREAM_SLAVE,
                             (Object **) &s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             NULL);

    object_property_add_link(obj, "zynqmp-aes-key-sink-boot",
                                 TYPE_ZYNQMP_AES_KEY_SINK,
                                 (Object **)&s->boot_key,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 NULL);

    object_property_add_link(obj, "zynqmp-aes-key-sink-operational",
                                 TYPE_ZYNQMP_AES_KEY_SINK,
                                 (Object **)&s->okr_key,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 NULL);
    object_property_add_link(obj, "aes-core", TYPE_XLNX_AES,
                             (Object **) &s->aes,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             NULL);

    memory_region_init_io(&s->iomem, obj, &aes_ops, s,
                          "zynqmp.csu-aes", R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aes = {
    .name = "zynqmp_csu_aes",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(buf, ZynqMPCSUAES, 16),
        VMSTATE_UINT8(bufpos, ZynqMPCSUAES),
        VMSTATE_UINT32_ARRAY(regs, ZynqMPCSUAES, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const FDTGenericGPIOSet aes_gpios[] = {
    {
      .names = &fdt_generic_gpio_name_set_gpio,
      .gpios = (FDTGenericGPIOConnection[]) {
        { .name = "key-wr", .fdt_index = 0, .range = 5 },
        { .name = "reset", .fdt_index = 5, .range = 1},
        { },
      },
    },
    { },
};

static void aes_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);
    ZynqMPAESKeySinkClass *ksc = ZYNQMP_AES_KEY_SINK_CLASS(klass);
    FDTGenericGPIOClass *fggc = FDT_GENERIC_GPIO_CLASS(klass);


    dc->reset = xlx_aes_reset;
    dc->realize = aes_realize;
    dc->vmsd = &vmstate_aes;

    ssc->push = xlx_aes_stream_push;
    ssc->can_push = xlx_aes_stream_can_push;

    ksc->update = device_key_update;
    fggc->controller_gpios = aes_gpios;
}

static const TypeInfo aes_info = {
    .name          = TYPE_ZYNQMP_CSU_AES,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPCSUAES),
    .class_init    = aes_class_init,
    .instance_init = aes_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { TYPE_ZYNQMP_AES_KEY_SINK },
        { TYPE_FDT_GENERIC_GPIO },
        { }
    }
};

static void aes_register_types(void)
{
    type_register_static(&aes_info);
}

type_init(aes_register_types)
