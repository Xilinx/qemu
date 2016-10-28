/*
 * QEMU model of ZynqMP CSU Stream DMA
 *
 * Copyright (c) 2013 Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 * Copyright (c) 2013 Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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
#include "hw/sysbus.h"
#include "qemu/log.h"

#include "hw/stream.h"
#include "hw/ptimer.h"
#include "qemu/bitops.h"
#include "sysemu/dma.h"
#include "hw/register.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"

#include "hw/fdt_generic_util.h"

#define TYPE_ZYNQMP_CSU_DMA "zynqmp.csu-dma"

#define ZYNQMP_CSU_DMA(obj) \
     OBJECT_CHECK(ZynqMPCSUDMA, (obj), TYPE_ZYNQMP_CSU_DMA)

#ifndef ZYNQMP_CSU_DMA_ERR_DEBUG
#define ZYNQMP_CSU_DMA_ERR_DEBUG 0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do {\
    if (ZYNQMP_CSU_DMA_ERR_DEBUG > lvl) {\
        fprintf(stderr, TYPE_ZYNQMP_CSU_DMA ": %s:" fmt, __func__, ## args);\
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(0, fmt, ##args)

enum {
    R_ADDR         = 0x00 / 4,
    R_SIZE         = 0x04 / 4,
    R_STATUS       = 0x08 / 4,
    R_CTRL         = 0x0c / 4,
    R_CRC          = 0x10 / 4,
    R_INT_STATUS   = 0x14 / 4,
    R_INT_ENABLE   = 0x18 / 4,
    R_INT_DISABLE  = 0x1c / 4,
    R_INT_MASK     = 0x20 / 4,
    R_CTRL2        = 0x24 / 4,

    R_MAX          = R_CTRL2 + 1
};

enum {
    STATUS_DMA_BUSY          = (1 << 0),
    STATUS_RSVD              = 0
};

/* The DMA_DONE_CNT is write to clear.  */
FIELD(STATUS, DMA_DONE_CNT, 3, 13)

enum {
    CTRL_PAUSE_MEM                  = (1 << 0),
    CTRL_PAUSE_STRM                 = (1 << 1),

    CTRL_FIFO_THRESH_SHIFT          = 2,
    CTRL_AXI_BURST_FIXED            = (1 << 22),
    CTRL_ENDIANNESS                 = (1 << 23),
    CTRL_ERR_RESP                   = (1 << 24),
    CTRL_SSS_FIFOTHRESH_SHIFT       = 25,
    CTRL_RSVD                       = (~((1 << 25) - 1))
};

FIELD(CTRL, TIMEOUT, 12, 10)

enum {
    INT_FIFO_OVERFLOW               = 1 << 7,
    INT_INVALID_APB_ACCESS          = 1 << 6,
    INT_FIFO_THRESH_HIT             = 1 << 5,
    INT_TIMEOUT_MEM                 = 1 << 4,
    INT_TIMEOUT_STRM                = 1 << 3,
    INT_AXI_RDERR                   = 1 << 2,
    INT_DONE                        = 1 << 1,
    INT_MEM_DONE                    = 1 << 0,
    INT_RSVD                        = (~((1 << 8) - 1)),
    INT_ALL_SRC                     = ~INT_RSVD & ~INT_FIFO_OVERFLOW,
    INT_ALL_DST                     = ~INT_RSVD & ~INT_MEM_DONE,
};

enum {
    CTRL2_MAX_OUTS_CMDS_SHIFT       = 0,
    CTRL2_TIMEOUT_EN                = 1 << 22,
    CTRL2_TIMEOUT_PRE_SHIFT         = 4,
    CTRL2_RSVD                      = (~((1 << 28) - 1))
};

typedef struct ZynqMPCSUDMA {
    SysBusDevice busdev;
    MemoryRegion iomem;
    MemTxAttrs *attr;
    MemoryRegion *dma_mr;
    AddressSpace *dma_as;
    qemu_irq irq;
    StreamSlave *tx_dev;
    QEMUBH *bh;
    ptimer_state *src_timer;

    bool is_dst;

    StreamCanPushNotifyFn notify;
    void *notify_opaque;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} ZynqMPCSUDMA;

/* This is a zynqmp specific CSU hack.  */
static int dmach_validate_addr(ZynqMPCSUDMA *s)
{
    /* priv ROM access?  */
    if (s->regs[R_ADDR] >= 0xffc00000 && s->regs[R_ADDR] < 0xffc20000) {
        return 1;
    }
    /* priv RAM access?  */
    if (s->regs[R_ADDR] >= 0xffc40000 && s->regs[R_ADDR] < 0xffc48000) {
        return 1;
    }
    return 0;
}

static bool dmach_is_paused(ZynqMPCSUDMA *s)
{
    bool paused;

    paused = !!(s->regs[R_CTRL] & CTRL_PAUSE_STRM);
    paused |= !!(s->regs[R_CTRL] & CTRL_PAUSE_MEM);
    return paused;
}

static bool dmach_get_eop(ZynqMPCSUDMA *s)
{
    return s->regs[R_SIZE] & 1;
}

static uint32_t dmach_get_size(ZynqMPCSUDMA *s)
{
    return s->regs[R_SIZE] & ~3;
}

static void dmach_set_size(ZynqMPCSUDMA *s, uint32_t size)
{
    assert((size & 3) == 0);

    s->regs[R_SIZE] &= 1;
    s->regs[R_SIZE] |= size;
}

static bool dmach_burst_is_fixed(ZynqMPCSUDMA *s)
{
    return !!(s->regs[R_CTRL] & CTRL_AXI_BURST_FIXED);
}

static bool dmach_timeout_enabled(ZynqMPCSUDMA *s)
{
    return s->regs[R_CTRL2] & CTRL2_TIMEOUT_EN;
}

static inline void dmach_update_dma_cnt(ZynqMPCSUDMA *s, int a)
{
    int cnt;

    /* Increase dma_cnt.  */
    cnt = AF_EX32(s->regs, STATUS, DMA_DONE_CNT) + a;
    AF_DP32(s->regs, STATUS, DMA_DONE_CNT, cnt);
}

static void dmach_done(ZynqMPCSUDMA *s)
{
    dmach_update_dma_cnt(s, +1);
    s->regs[R_STATUS] &= ~STATUS_DMA_BUSY;

    DB_PRINT("\n");
    s->regs[R_INT_STATUS] |= INT_DONE;
    if (!s->is_dst) {
        s->regs[R_INT_STATUS] |= INT_MEM_DONE;
    }
}

static void dmach_advance(ZynqMPCSUDMA *s, unsigned int len)
{
    uint32_t size = dmach_get_size(s);

    /* Has to be 32bit aligned.  */
    assert((len & 3) == 0);
    assert(len <= size);

    if (!dmach_burst_is_fixed(s)) {
        s->regs[R_ADDR] += len;
    }

    size -= len;
    dmach_set_size(s, size);

    if (size == 0) {
        dmach_done(s);
    }
}

static void dmach_data_process(ZynqMPCSUDMA *s, uint8_t *buf, unsigned int len)
{
    unsigned int bswap;
    unsigned int i;

    /* Xor only for src channel.  */
    bswap = s->regs[R_CTRL] & CTRL_ENDIANNESS;
    if (s->is_dst && !bswap) {
        /* Fast!  */
        return;
    }

    /* buf might not be 32bit aligned... slooow.  */
    assert((len & 3) == 0);
    /* FIXME: move me to bitops.c for global reusability */
    for (i = 0; i < len; i += 4) {
        uint8_t *b = &buf[i];
        union {
            uint8_t u8[4];
            uint32_t u32;
        } v = {
            .u8 = { b[0], b[1], b[2], b[3] }
        };

        if (!s->is_dst) {
            s->regs[R_CRC] += v.u32;
        }
        if (bswap) {
            /* No point using bswap, we need to writeback
               into a potentially unaligned pointer..   */
            b[0] = v.u8[3];
            b[1] = v.u8[2];
            b[2] = v.u8[1];
            b[3] = v.u8[0];
        }
    }
}

/* len is in bytes.  */
static void dmach_write(ZynqMPCSUDMA *s, uint8_t *buf, unsigned int len)
{
    int err = dmach_validate_addr(s);

    if (err) {
        return;
    }

    dmach_data_process(s, buf, len);
    if (dmach_burst_is_fixed(s)) {
        unsigned int i;

        for (i = 0; i < len; i += 4) {
            address_space_rw(s->dma_as, s->regs[R_ADDR], *s->attr, buf, 4,
                                  true);
            buf += 4;
        }
    } else {
        address_space_rw(s->dma_as, s->regs[R_ADDR], *s->attr, buf, len,
                              true);
    }
}

/* len is in bytes.  */
static inline void dmach_read(ZynqMPCSUDMA *s, uint8_t *buf, unsigned int len)
{
    int raz = dmach_validate_addr(s);

    if (raz) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csu-dma: Reading from unaccessible memory addr=%x\n",
                      s->regs[R_ADDR]);
        /* This maybe raises an exception instead... */
        memset(buf, 0, len);
        return;
    }

    if (dmach_burst_is_fixed(s)) {
        unsigned int i;

        for (i = 0; i < len; i += 4) {
            address_space_rw(s->dma_as, s->regs[R_ADDR], *s->attr, buf + i, 4,
                             false);
        }
    } else {
        address_space_rw(s->dma_as, s->regs[R_ADDR], *s->attr, buf, len,
                              false);
    }
    dmach_data_process(s, buf, len);
}

static void ronaldu_csu_dma_update_irq(ZynqMPCSUDMA *s)
{
    qemu_set_irq(s->irq, !!(s->regs[R_INT_STATUS] & ~s->regs[R_INT_MASK]));
}

static void zynqmp_csu_dma_reset(DeviceState *dev)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(dev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        register_reset(&s->regs_info[i]);
    }
}

static size_t zynqmp_csu_dma_stream_push(StreamSlave *obj, uint8_t *buf,
                                          size_t len, uint32_t attr)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(obj);
    uint32_t size = dmach_get_size(s);
    uint32_t btt = MIN(size, len);

    assert(s->is_dst);
    if (len && (dmach_is_paused(s) || btt == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csu-dma: DST channel dropping %zd b of data.\n", len);
        s->regs[R_INT_STATUS] |= INT_FIFO_OVERFLOW;
        return len;
    }

    if (!btt) {
        return 0;
    }

    /* DMA transfer.  */
    dmach_write(s, buf, btt);
    dmach_advance(s, btt);
    ronaldu_csu_dma_update_irq(s);
    return btt;
}

static bool zynqmp_csu_dma_stream_can_push(StreamSlave *obj,
                                            StreamCanPushNotifyFn notify,
                                            void *notify_opaque)
{
    /* DST channel side has no flow-control.  */
    return true;
}

static void zynqmp_csu_dma_src_notify(void *opaque)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(opaque);
    unsigned char buf[4 * 1024];

    /* Stop the backpreassure timer.  */
    ptimer_stop(s->src_timer);

    while (dmach_get_size(s) && !dmach_is_paused(s) &&
           stream_can_push(s->tx_dev, zynqmp_csu_dma_src_notify, s)) {
        uint32_t size = dmach_get_size(s);
        unsigned int plen = MIN(size, sizeof buf);
        uint32_t attr = 0;
        size_t ret;

        /* Did we fit it all?  */
        if (size == plen && dmach_get_eop(s)) {
            attr |= STREAM_ATTR_EOP;
        }

        /* DMA transfer.  */
        dmach_read(s, buf, plen);
        ret = stream_push(s->tx_dev, buf, plen, attr);
        dmach_advance(s, ret);
    }

    /* REMOVE-ME?: Check for flow-control timeout. This is all theoretical as
       we currently never see backpreassure.  */
    if (dmach_timeout_enabled(s) && dmach_get_size(s)
        && !stream_can_push(s->tx_dev, zynqmp_csu_dma_src_notify, s)) {
        unsigned int timeout = AF_EX32(s->regs, CTRL, TIMEOUT);
        unsigned int div = extract32(s->regs[R_CTRL2], 4, 12) + 1;
        unsigned int freq = 400 * 1000 * 1000;

        freq /= div;
        ptimer_set_freq(s->src_timer, freq);
        ptimer_set_count(s->src_timer, timeout);
        ptimer_run(s->src_timer, 1);
    }

    ronaldu_csu_dma_update_irq(s);
}

static void r_ctrl_post_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);

    if (!s->is_dst) {
        if (!dmach_is_paused(s)) {
            zynqmp_csu_dma_src_notify(s);
        }
    } else {
        if (!dmach_is_paused(s) && s->notify) {
            s->notify(s->notify_opaque);
        }
    }
}

static uint64_t size_pre_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);
    if (dmach_get_size(s) != 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csu-dma: Starting DMA while already running.\n");
    }
    return val;
}

static void size_post_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);

    s->regs[R_STATUS] |= STATUS_DMA_BUSY;
    /* When starting the DMA channel with a zero length, it signals
       done immediately.  */
    if (dmach_get_size(s) == 0) {
        dmach_done(s);
        ronaldu_csu_dma_update_irq(s);
        return;
    }

    if (!s->is_dst) {
        zynqmp_csu_dma_src_notify(s);
    } else {
        if (s->notify) {
            s->notify(s->notify_opaque);
        }
    }
}

static uint64_t int_status_pre_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);

    /* DMA counter decrements on interrupt clear */
    if (~val & s->regs[R_INT_STATUS] & INT_DONE) {
        dmach_update_dma_cnt(s, -1);
    }

    return val;
}

static void int_status_post_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);

    ronaldu_csu_dma_update_irq(s);
}

static uint64_t int_enable_pre_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);
    uint32_t v32 = val;

    s->regs[R_INT_MASK] &= ~v32;
    ronaldu_csu_dma_update_irq(s);
    return 0;
}

static uint64_t int_disable_pre_write(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(reg->opaque);
    uint32_t v32 = val;

    s->regs[R_INT_MASK] |= v32;
    ronaldu_csu_dma_update_irq(s);
    return 0;
}

static void src_timeout_hit(void *opaque)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(opaque);

    /* Ignore if the timeout is masked.  */
    if (!dmach_timeout_enabled(s)) {
        return;
    }

    s->regs[R_INT_STATUS] |= INT_TIMEOUT_STRM;
    ronaldu_csu_dma_update_irq(s);
}

static const RegisterAccessInfo *zynqmp_csu_dma_regs_info[] = {
#define DMACH_REGINFO(NAME, snd)                                              \
(const RegisterAccessInfo []) {                                               \
    [R_ADDR] = { .name =  #NAME "_ADDR" },                                    \
    [R_SIZE] = { .name =  #NAME "_SIZE",                                      \
                               .pre_write = size_pre_write,                   \
                               .post_write = size_post_write },               \
    [R_STATUS] = { .name =  #NAME "_STATUS",                                  \
            .ro = STATUS_RSVD,                                                \
            .w1c = R_STATUS_DMA_DONE_CNT_MASK,                                \
            .ge1 = (RegisterAccessError[]) {                                  \
                 { .mask = ~(R_STATUS_DMA_DONE_CNT_MASK),                     \
                   .reason = "cannot write to status register" },             \
                   {},                                                        \
            },                                                                \
    },                                                                        \
    [R_CTRL] = { .name = #NAME "_CTRL",                                       \
        .ro = (snd) ? CTRL_RSVD : 0,                                          \
        .reset = ((snd) ? 0 : 0x40 << CTRL_SSS_FIFOTHRESH_SHIFT) |            \
                 R_CTRL_TIMEOUT_MASK | 0x80 << CTRL_FIFO_THRESH_SHIFT,        \
        .ge1 = (RegisterAccessError[]) {                                      \
            { .mask = (snd) ? CTRL_RSVD : 0,                                  \
              .reason = "write of 1 to reserved bit" },                       \
            {},                                                               \
        },                                                                    \
        .post_write = r_ctrl_post_write,                                      \
    },                                                                        \
    [R_CRC] = { .name =  #NAME "_CRC" },                                      \
    [R_INT_STATUS] = { .name =  #NAME "_INT_STATUS",                          \
                                     .w1c = ~0,                               \
                                     .pre_write = int_status_pre_write,       \
                                     .post_write = int_status_post_write },   \
    [R_INT_ENABLE] = { .name =  #NAME "_INT_ENABLE",                          \
                                     .pre_write = int_enable_pre_write },     \
    [R_INT_DISABLE] = { .name =  #NAME "_INT_DISABLE",                        \
                                     .pre_write = int_disable_pre_write },    \
    [R_INT_MASK] = { .name =  #NAME "_INT_MASK",                              \
                                     .reset = snd ? INT_ALL_SRC : INT_ALL_DST,\
                                     .ro = ~0 },                              \
    [R_CTRL2] = { .name =  #NAME "_CTRL2",                                    \
        .reset = 0x8 << CTRL2_MAX_OUTS_CMDS_SHIFT |                           \
                 0xFFF << CTRL2_TIMEOUT_PRE_SHIFT | 0x081b0000,               \
        .ro = CTRL2_RSVD,                                                     \
        .ge0 = (RegisterAccessError[]) {                                      \
            { .mask = 0x00090000, .reason = "reserved - do not modify" },     \
            {}                                                                \
        },                                                                    \
        .ge1 = (RegisterAccessError[]) {                                      \
            { .mask = 0x00F60000, .reason = "reserved - do not modify" },     \
            {}                                                                \
        }                                                                     \
    }                                                                         \
}
    DMACH_REGINFO(DMA_SRC, true),
    DMACH_REGINFO(DMA_DST, false)
};

static const MemoryRegionOps zynqmp_csu_dma_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void map_dma_channel(const char *prefix, ZynqMPCSUDMA *s)
{
    int i;

    for (i = 0; i < R_MAX; ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[i],
            .data_size = sizeof(uint32_t),
            .access = &zynqmp_csu_dma_regs_info[!!s->is_dst][i],
            .debug = ZYNQMP_CSU_DMA_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(s), &zynqmp_csu_dma_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, i * 4, &r->mem);
    }
}

static void zynqmp_csu_dma_realize(DeviceState *dev, Error **errp)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init(&s->iomem, OBJECT(dev), "zynqmp.csu-dma", 0x800);
    sysbus_init_mmio(sbd, &s->iomem);

    const char *prefix = object_get_canonical_path(OBJECT(dev));

    map_dma_channel(prefix, s);

    s->bh = qemu_bh_new(src_timeout_hit, s);
    s->src_timer = ptimer_init(s->bh);

    s->dma_as = s->dma_mr ? address_space_init_shareable(s->dma_mr, NULL)
                          : &address_space_memory;

    if (!s->attr) {
        s->attr = MEMORY_TRANSACTION_ATTR(
                      object_new(TYPE_MEMORY_TRANSACTION_ATTR));
    }
}

static void zynqmp_csu_dma_init(Object *obj)
{
    ZynqMPCSUDMA *s = ZYNQMP_CSU_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);

    object_property_add_link(obj, "stream-connected-dma", TYPE_STREAM_SLAVE,
                             (Object **) &s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             NULL);
    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
    object_property_add_link(obj, "memattr", TYPE_MEMORY_TRANSACTION_ATTR,
                             (Object **)&s->attr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);

}

static const VMStateDescription vmstate_zynqmp_csu_dma = {
    .name = "zynqmp_csu_dma",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(src_timer, ZynqMPCSUDMA),
        VMSTATE_UINT32_ARRAY(regs, ZynqMPCSUDMA, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property zynqmp_csu_dma_properties [] = {
    DEFINE_PROP_BOOL("is-dst", ZynqMPCSUDMA, is_dst, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void zynqmp_csu_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    dc->reset = zynqmp_csu_dma_reset;
    dc->realize = zynqmp_csu_dma_realize;
    dc->vmsd = &vmstate_zynqmp_csu_dma;
    dc->props = zynqmp_csu_dma_properties;

    ssc->push = zynqmp_csu_dma_stream_push;
    ssc->can_push = zynqmp_csu_dma_stream_can_push;
}

static const TypeInfo zynqmp_csu_dma_info = {
    .name          = TYPE_ZYNQMP_CSU_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPCSUDMA),
    .class_init    = zynqmp_csu_dma_class_init,
    .instance_init = zynqmp_csu_dma_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void zynqmp_csu_dma_register_types(void)
{
    type_register_static(&zynqmp_csu_dma_info);
}

type_init(zynqmp_csu_dma_register_types)
