/*
 * Xilinx Platform CSU Stream DMA emulation
 *
 * This implementation is based on
 * https://github.com/Xilinx/qemu/blob/master/hw/dma/csu_stream_dma.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"
#include "hw/ptimer.h"
#include "hw/stream.h"
#include "hw/register.h"
#include "hw/dma/xlnx_csu_dma.h"

/*
 * Ref: UG1087 (v1.7) February 8, 2019
 * https://www.xilinx.com/html_docs/registers/ug1087/ug1087-zynq-ultrascale-registers.html
 * CSUDMA Module section
 */
REG32(ADDR, 0x0)
    FIELD(ADDR, ADDR, 2, 30) /* wo */
REG32(ADDR_UNALIGNED, 0x0) /* when the DMA allows unaligned accesses */
    FIELD(ADDR_UNALIGNED, ADDR, 0, 32) /* wo */
REG32(SIZE, 0x4)
    FIELD(SIZE, SIZE, 2, 27) /* wo */
    FIELD(SIZE, LAST_WORD, 0, 1) /* rw, only exists in SRC */
REG32(SIZE_UNALIGNED, 0x4) /* when the DMA allows unaligned accesses */
    FIELD(SIZE_UNALIGNED, SIZE, 0, 29) /* wo */
    FIELD(SIZE_UNALIGNED, LAST_WORD, 29, 1) /* rw, only exists in SRC */
REG32(STATUS, 0x8)
    FIELD(STATUS, DONE_CNT, 13, 3) /* wtc */
    FIELD(STATUS, FIFO_LEVEL, 5, 8) /* ro */
    FIELD(STATUS, OUTSTANDING, 1, 4) /* ro */
    FIELD(STATUS, BUSY, 0, 1) /* ro */
REG32(CTRL, 0xc)
    FIELD(CTRL, FIFOTHRESH, 25, 7) /* rw, only exists in DST, reset 0x40 */
    FIELD(CTRL, APB_ERR_RESP, 24, 1) /* rw */
    FIELD(CTRL, ENDIANNESS, 23, 1) /* rw */
    FIELD(CTRL, AXI_BRST_TYPE, 22, 1) /* rw */
    FIELD(CTRL, TIMEOUT_VAL, 10, 12) /* rw, reset: 0xFFE */
    FIELD(CTRL, FIFO_THRESH, 2, 8) /* rw, reset: 0x80 */
    FIELD(CTRL, PAUSE_STRM, 1, 1) /* rw */
    FIELD(CTRL, PAUSE_MEM, 0, 1) /* rw */
REG32(CRC, 0x10)
REG32(INT_STATUS, 0x14)
    FIELD(INT_STATUS, FIFO_OVERFLOW, 7, 1) /* wtc */
    FIELD(INT_STATUS, INVALID_APB, 6, 1) /* wtc */
    FIELD(INT_STATUS, THRESH_HIT, 5, 1) /* wtc */
    FIELD(INT_STATUS, TIMEOUT_MEM, 4, 1) /* wtc */
    FIELD(INT_STATUS, TIMEOUT_STRM, 3, 1) /* wtc */
    FIELD(INT_STATUS, AXI_BRESP_ERR, 2, 1) /* wtc, SRC: AXI_RDERR */
    FIELD(INT_STATUS, DONE, 1, 1) /* wtc */
    FIELD(INT_STATUS, MEM_DONE, 0, 1) /* wtc */
REG32(INT_ENABLE, 0x18)
    FIELD(INT_ENABLE, FIFO_OVERFLOW, 7, 1) /* wtc */
    FIELD(INT_ENABLE, INVALID_APB, 6, 1) /* wtc */
    FIELD(INT_ENABLE, THRESH_HIT, 5, 1) /* wtc */
    FIELD(INT_ENABLE, TIMEOUT_MEM, 4, 1) /* wtc */
    FIELD(INT_ENABLE, TIMEOUT_STRM, 3, 1) /* wtc */
    FIELD(INT_ENABLE, AXI_BRESP_ERR, 2, 1) /* wtc, SRC: AXI_RDERR */
    FIELD(INT_ENABLE, DONE, 1, 1) /* wtc */
    FIELD(INT_ENABLE, MEM_DONE, 0, 1) /* wtc */
REG32(INT_DISABLE, 0x1c)
    FIELD(INT_DISABLE, FIFO_OVERFLOW, 7, 1) /* wtc */
    FIELD(INT_DISABLE, INVALID_APB, 6, 1) /* wtc */
    FIELD(INT_DISABLE, THRESH_HIT, 5, 1) /* wtc */
    FIELD(INT_DISABLE, TIMEOUT_MEM, 4, 1) /* wtc */
    FIELD(INT_DISABLE, TIMEOUT_STRM, 3, 1) /* wtc */
    FIELD(INT_DISABLE, AXI_BRESP_ERR, 2, 1) /* wtc, SRC: AXI_RDERR */
    FIELD(INT_DISABLE, DONE, 1, 1) /* wtc */
    FIELD(INT_DISABLE, MEM_DONE, 0, 1) /* wtc */
REG32(INT_MASK, 0x20)
    FIELD(INT_MASK, FIFO_OVERFLOW, 7, 1) /* ro, reset: 0x1 */
    FIELD(INT_MASK, INVALID_APB, 6, 1) /* ro, reset: 0x1 */
    FIELD(INT_MASK, THRESH_HIT, 5, 1) /* ro, reset: 0x1 */
    FIELD(INT_MASK, TIMEOUT_MEM, 4, 1) /* ro, reset: 0x1 */
    FIELD(INT_MASK, TIMEOUT_STRM, 3, 1) /* ro, reset: 0x1 */
    FIELD(INT_MASK, AXI_BRESP_ERR, 2, 1) /* ro, reset: 0x1, SRC: AXI_RDERR */
    FIELD(INT_MASK, DONE, 1, 1) /* ro, reset: 0x1 */
    FIELD(INT_MASK, MEM_DONE, 0, 1) /* ro, reset: 0x1 */
REG32(CTRL2, 0x24)
    FIELD(CTRL2, ARCACHE, 24, 3) /* rw */
    FIELD(CTRL2, ROUTE_BIT, 23, 1) /* rw */
    FIELD(CTRL2, TIMEOUT_EN, 22, 1) /* rw */
    FIELD(CTRL2, TIMEOUT_PRE, 4, 12) /* rw, reset: 0xFFF */
    FIELD(CTRL2, MAX_OUTS_CMDS, 0, 4) /* rw, reset: 0x8 */
REG32(ADDR_MSB, 0x28)
    FIELD(ADDR_MSB, ADDR_MSB, 0, 17) /* wo */
REG32(CRC1, 0x2c) /* on 128 bits DMAs */
REG32(CRC2, 0x30)
REG32(CRC3, 0x34)

#define R_CTRL_TIMEOUT_VAL_RESET    (0xFFE)
#define R_CTRL_FIFO_THRESH_RESET    (0x80)
#define R_CTRL_FIFOTHRESH_RESET     (0x40)

#define R_CTRL2_TIMEOUT_PRE_RESET   (0xFFF)
#define R_CTRL2_MAX_OUTS_CMDS_RESET (0x8)

#define XLNX_CSU_DMA_ERR_DEBUG      (0)
#define XLNX_CSU_DMA_INT_R_MASK     (0xff)

/* UG1807: Set the prescaler value for the timeout in clk (~2.5ns) cycles */
#define XLNX_CSU_DMA_TIMER_FREQ     (400 * 1000 * 1000)

static bool xlnx_csu_dma_is_paused(XlnxCSUDMA *s)
{
    bool paused;

    paused = !!(s->regs[R_CTRL] & R_CTRL_PAUSE_STRM_MASK);
    paused |= !!(s->regs[R_CTRL] & R_CTRL_PAUSE_MEM_MASK);

    return paused;
}

static bool xlnx_csu_dma_get_eop(XlnxCSUDMA *s)
{
    return s->r_size_last_word;
}

static bool xlnx_csu_dma_burst_is_fixed(XlnxCSUDMA *s)
{
    return !!(s->regs[R_CTRL] & R_CTRL_AXI_BRST_TYPE_MASK);
}

static bool xlnx_csu_dma_timeout_enabled(XlnxCSUDMA *s)
{
    return !!(s->regs[R_CTRL2] & R_CTRL2_TIMEOUT_EN_MASK);
}

static void xlnx_csu_dma_update_done_cnt(XlnxCSUDMA *s, int a)
{
    int cnt;

    /* Increase DONE_CNT */
    cnt = ARRAY_FIELD_EX32(s->regs, STATUS, DONE_CNT) + a;
    ARRAY_FIELD_DP32(s->regs, STATUS, DONE_CNT, cnt);
}

static inline void update_crc_32(XlnxCSUDMA *s, const uint8_t *buf, uint32_t len)
{
    uint32_t leftover = 0;
    size_t shift = 0;

    while (len >= 4) {
        s->regs[R_CRC] += ldl_he_p(buf);
        buf += 4;
        len -= 4;
    }

    /*
     * Handle unaligned accesses. The DMA pads missing MSB bytes with 0s for CRC
     * computation. Once we're here we have at most 3 bytes to read.
     */
    while (len) {
        size_t ld_sz = pow2floor(MIN(2, len));

        leftover |= ldn_he_p(buf, ld_sz) << shift;

        shift += ld_sz;
        buf += ld_sz;
        len -= ld_sz;
    }

    s->regs[R_CRC] += leftover;
}

static inline void update_crc_128(XlnxCSUDMA *s, const uint8_t *buf,
                                  uint32_t len)
{
    Int128 crc;
    Int128 leftover = int128_zero();
    uint64_t lo, hi;
    size_t shift = 0;

    crc = int128_make128(s->regs[R_CRC1], s->regs[R_CRC3]);
    crc = int128_lshift(crc, 32);
    crc = int128_or(crc, int128_make128(s->regs[R_CRC], s->regs[R_CRC2]));

    while (len >= 16) {
        Int128 d;

        d = int128_make128(ldq_he_p(buf), ldq_he_p(buf + 8));

        int128_addto(&crc, d);
        buf += 16;
        len -= 16;
    }

    while (len) {
        Int128 d;
        size_t ld_sz = pow2floor(MIN(8, len));

        d = int128_make64(ldn_he_p(buf, ld_sz));
        d = int128_lshift(d, shift);
        leftover = int128_or(leftover, d);

        shift += ld_sz;
        buf += ld_sz;
        len -= ld_sz;
    }

    int128_addto(&crc, leftover);

    lo = int128_getlo(crc);
    hi = int128_gethi(crc);
    s->regs[R_CRC] = lo & UINT32_MAX;
    s->regs[R_CRC1] = lo >> 32;
    s->regs[R_CRC2] = hi & UINT32_MAX;
    s->regs[R_CRC3] = hi >> 32;
}

static void update_crc(XlnxCSUDMA *s, const uint8_t *buf, uint32_t len)
{
    g_assert(!s->is_dst);

    switch (s->width) {
    case 4:
        update_crc_32(s, buf, len);
        break;

    case 16:
        update_crc_128(s, buf, len);
        break;

    default:
        g_assert_not_reached();
    }
}

static inline void do_byte_swap(XlnxCSUDMA *s, uint8_t *buf, uint32_t len)
{
    uint32_t *b;

    if (!FIELD_EX32(s->regs[R_CTRL], CTRL, ENDIANNESS)) {
        /* byte swapping disabled */
        return;
    }

    if (len & 0x3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "xlnx-csu-dma: endianness swapping on "
                      "non 32 bits aligned data is undefined behavior\n");
        /* we choose to skip the swapping */
        return;
    }

    b = (uint32_t *) buf;

    while (len) {
        bswap32s(b);
        b++;
        len -= 4;
    }
}

static void xlnx_csu_dma_update_irq(XlnxCSUDMA *s)
{
    qemu_set_irq(s->irq, !!(s->regs[R_INT_STATUS] & ~s->regs[R_INT_MASK]));
}

/* len is in bytes */
static uint32_t xlnx_csu_dma_read(XlnxCSUDMA *s, uint8_t *buf, uint32_t len)
{
    hwaddr addr = (hwaddr)s->regs[R_ADDR_MSB] << 32 | s->regs[R_ADDR];
    MemTxResult result = MEMTX_OK;

    if (xlnx_csu_dma_burst_is_fixed(s)) {
        uint32_t i;

        for (i = 0; i < len && (result == MEMTX_OK); i += s->width) {
            uint32_t mlen = MIN(len - i, s->width);

            result = address_space_rw(&s->dma_as, addr, *s->attr_r,
                                      buf + i, mlen, false);
        }
    } else {
        result = address_space_rw(&s->dma_as, addr, *s->attr_r, buf, len, false);
    }

    if (result == MEMTX_OK) {
        update_crc(s, buf, len);
        do_byte_swap(s, buf, len);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address " HWADDR_FMT_plx
                      " for mem read", __func__, addr);
        s->regs[R_INT_STATUS] |= R_INT_STATUS_AXI_BRESP_ERR_MASK;
        xlnx_csu_dma_update_irq(s);
    }
    return len;
}

/* len is in bytes */
static uint32_t xlnx_csu_dma_write(XlnxCSUDMA *s, uint8_t *buf, uint32_t len)
{
    hwaddr addr = (hwaddr)s->regs[R_ADDR_MSB] << 32 | s->regs[R_ADDR];
    MemTxResult result = MEMTX_OK;

    do_byte_swap(s, buf, len);

    if (xlnx_csu_dma_burst_is_fixed(s)) {
        uint32_t i;

        for (i = 0; i < len && (result == MEMTX_OK); i += s->width) {
            uint32_t mlen = MIN(len - i, s->width);

            result = address_space_rw(&s->dma_as, addr, *s->attr_w,
                                      buf, mlen, true);
            buf += mlen;
        }
    } else {
        result = address_space_rw(&s->dma_as, addr, *s->attr_w, buf, len, true);
    }

    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad address " HWADDR_FMT_plx
                      " for mem write", __func__, addr);
        s->regs[R_INT_STATUS] |= R_INT_STATUS_AXI_BRESP_ERR_MASK;
        xlnx_csu_dma_update_irq(s);
    }
    return len;
}

static void xlnx_csu_dma_done(XlnxCSUDMA *s)
{
    s->regs[R_STATUS] &= ~R_STATUS_BUSY_MASK;
    s->regs[R_INT_STATUS] |= R_INT_STATUS_DONE_MASK;

    if (!s->is_dst) {
        s->regs[R_INT_STATUS] |= R_INT_STATUS_MEM_DONE_MASK;
    }

    xlnx_csu_dma_update_done_cnt(s, 1);
}

static uint32_t xlnx_csu_dma_advance(XlnxCSUDMA *s, uint32_t len)
{
    uint32_t size = s->regs[R_SIZE];
    hwaddr dst = (hwaddr)s->regs[R_ADDR_MSB] << 32 | s->regs[R_ADDR];

    assert(len <= size);

    size -= len;
    s->regs[R_SIZE] = size;

    if (!xlnx_csu_dma_burst_is_fixed(s)) {
        dst += len;
        s->regs[R_ADDR] = (uint32_t) dst;
        s->regs[R_ADDR_MSB] = dst >> 32;
    }

    if (size == 0) {
        xlnx_csu_dma_done(s);
    }

    return size;
}

static void xlnx_csu_dma_src_notify(void *opaque)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(opaque);
    unsigned char buf[4 * 1024];
    size_t rlen = 0;

    ptimer_transaction_begin(s->src_timer);
    /* Stop the backpreassure timer */
    ptimer_stop(s->src_timer);

    while (s->regs[R_SIZE] && !xlnx_csu_dma_is_paused(s) &&
           stream_can_push(s->tx_dev, xlnx_csu_dma_src_notify, s)) {
        uint32_t plen = MIN(s->regs[R_SIZE], sizeof buf);
        bool eop = false;

        /* Did we fit it all? */
        if (s->regs[R_SIZE] == plen && xlnx_csu_dma_get_eop(s)) {
            eop = true;
        }

        /* DMA transfer */
        xlnx_csu_dma_read(s, buf, plen);
        rlen = stream_push(s->tx_dev, buf, plen, eop);
        xlnx_csu_dma_advance(s, rlen);
    }

    if (xlnx_csu_dma_timeout_enabled(s) && s->regs[R_SIZE] &&
        !stream_can_push(s->tx_dev, xlnx_csu_dma_src_notify, s)) {
        uint32_t timeout = ARRAY_FIELD_EX32(s->regs, CTRL, TIMEOUT_VAL);
        uint32_t div = ARRAY_FIELD_EX32(s->regs, CTRL2, TIMEOUT_PRE) + 1;
        uint32_t freq = XLNX_CSU_DMA_TIMER_FREQ;

        freq /= div;
        ptimer_set_freq(s->src_timer, freq);
        ptimer_set_count(s->src_timer, timeout);
        ptimer_run(s->src_timer, 1);
    }

    ptimer_transaction_commit(s->src_timer);
    xlnx_csu_dma_update_irq(s);
}

static uint64_t addr_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    if (s->allow_unaligned) {
        return val & R_ADDR_UNALIGNED_ADDR_MASK;
    } else {
        /* Address is word aligned */
        return val & R_ADDR_ADDR_MASK;
    }
}

static uint64_t size_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    if (s->regs[R_SIZE] != 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Starting DMA while already running.\n", __func__);
    }

    if (!s->is_dst) {
        uint32_t mask = s->allow_unaligned ? R_SIZE_UNALIGNED_LAST_WORD_MASK
                                           : R_SIZE_LAST_WORD_MASK;
        s->r_size_last_word = !!(val & mask);
    }

    /* Size is word aligned */
    return val & (s->allow_unaligned ? R_SIZE_UNALIGNED_SIZE_MASK
                                     : R_SIZE_SIZE_MASK);
}

static uint64_t size_post_read(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);
    int shift = s->allow_unaligned ? R_SIZE_UNALIGNED_LAST_WORD_SHIFT
                                   : R_SIZE_LAST_WORD_SHIFT;
    uint32_t last_word = s->r_size_last_word;

    return val | (last_word << shift);
}

static void size_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    s->regs[R_STATUS] |= R_STATUS_BUSY_MASK;

    /*
     * Note that if SIZE is programmed to 0, and the DMA is started,
     * the interrupts DONE and MEM_DONE will be asserted.
     */
    if (s->regs[R_SIZE] == 0) {
        xlnx_csu_dma_done(s);
        xlnx_csu_dma_update_irq(s);
        return;
    }

    /* Set SIZE is considered the last step in transfer configuration */
    if (!s->is_dst) {
        xlnx_csu_dma_src_notify(s);
    } else {
        if (s->notify) {
            s->notify(s->notify_opaque);
        }
    }
}

static uint64_t status_pre_write(RegisterInfo *reg, uint64_t val)
{
    return val & (R_STATUS_DONE_CNT_MASK | R_STATUS_BUSY_MASK);
}

static void ctrl_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    if (!s->is_dst) {
        if (!xlnx_csu_dma_is_paused(s)) {
            xlnx_csu_dma_src_notify(s);
        }
    } else {
        if (!xlnx_csu_dma_is_paused(s) && s->notify) {
            s->notify(s->notify_opaque);
        }
    }
}

static uint64_t int_status_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    /* DMA counter decrements when flag 'DONE' is cleared */
    if ((val & s->regs[R_INT_STATUS] & R_INT_STATUS_DONE_MASK)) {
        xlnx_csu_dma_update_done_cnt(s, -1);
    }

    return s->regs[R_INT_STATUS] & ~val;
}

static void int_status_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    xlnx_csu_dma_update_irq(s);
}

static uint64_t int_enable_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);
    uint32_t v32 = val;

    /*
     * R_INT_ENABLE doesn't have its own state.
     * It is used to indirectly modify R_INT_MASK.
     *
     * 1: Enable this interrupt field (the mask bit will be cleared to 0)
     * 0: No effect
     */
    s->regs[R_INT_MASK] &= ~v32;
    return 0;
}

static void int_enable_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    xlnx_csu_dma_update_irq(s);
}

static uint64_t int_disable_pre_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);
    uint32_t v32 = val;

    /*
     * R_INT_DISABLE doesn't have its own state.
     * It is used to indirectly modify R_INT_MASK.
     *
     * 1: Disable this interrupt field (the mask bit will be set to 1)
     * 0: No effect
     */
    s->regs[R_INT_MASK] |= v32;
    return 0;
}

static void int_disable_post_write(RegisterInfo *reg, uint64_t val)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(reg->opaque);

    xlnx_csu_dma_update_irq(s);
}

static uint64_t addr_msb_pre_write(RegisterInfo *reg, uint64_t val)
{
    return val & R_ADDR_MSB_ADDR_MSB_MASK;
}

static MemTxResult xlnx_csu_dma_class_read(XlnxCSUDMA *s, hwaddr addr,
                                           uint32_t len)
{
    RegisterInfo *reg = &s->regs_info[R_SIZE];
    uint64_t we = MAKE_64BIT_MASK(0, 4 * 8);

    s->regs[R_ADDR] = addr;
    s->regs[R_ADDR_MSB] = (uint64_t)addr >> 32;

    register_write(reg, len, we, object_get_typename(OBJECT(s)), false);

    return (s->regs[R_SIZE] == 0) ? MEMTX_OK : MEMTX_ERROR;
}

static const RegisterAccessInfo *xlnx_csu_dma_regs_info[] = {
#define DMACH_REGINFO(NAME, snd)                                              \
    (const RegisterAccessInfo []) {                                           \
        {                                                                     \
            .name = #NAME "_ADDR",                                            \
            .addr = A_ADDR,                                                   \
            .pre_write = addr_pre_write                                       \
        }, {                                                                  \
            .name = #NAME "_SIZE",                                            \
            .addr = A_SIZE,                                                   \
            .pre_write = size_pre_write,                                      \
            .post_write = size_post_write,                                    \
            .post_read = size_post_read                                       \
        }, {                                                                  \
            .name = #NAME "_STATUS",                                          \
            .addr = A_STATUS,                                                 \
            .pre_write = status_pre_write,                                    \
            .w1c = R_STATUS_DONE_CNT_MASK,                                    \
            .ro = (R_STATUS_BUSY_MASK                                         \
                   | R_STATUS_FIFO_LEVEL_MASK                                 \
                   | R_STATUS_OUTSTANDING_MASK)                               \
        }, {                                                                  \
            .name = #NAME "_CTRL",                                            \
            .addr = A_CTRL,                                                   \
            .post_write = ctrl_post_write,                                    \
            .reset = ((R_CTRL_TIMEOUT_VAL_RESET << R_CTRL_TIMEOUT_VAL_SHIFT)  \
                      | (R_CTRL_FIFO_THRESH_RESET << R_CTRL_FIFO_THRESH_SHIFT)\
                      | (snd ? 0 : R_CTRL_FIFOTHRESH_RESET                    \
                         << R_CTRL_FIFOTHRESH_SHIFT))                         \
        }, {                                                                  \
            .name = #NAME "_CRC",                                             \
            .addr = A_CRC,                                                    \
        }, {                                                                  \
            .name =  #NAME "_INT_STATUS",                                     \
            .addr = A_INT_STATUS,                                             \
            .pre_write = int_status_pre_write,                                \
            .post_write = int_status_post_write                               \
        }, {                                                                  \
            .name = #NAME "_INT_ENABLE",                                      \
            .addr = A_INT_ENABLE,                                             \
            .pre_write = int_enable_pre_write,                                \
            .post_write = int_enable_post_write                               \
        }, {                                                                  \
            .name = #NAME "_INT_DISABLE",                                     \
            .addr = A_INT_DISABLE,                                            \
            .pre_write = int_disable_pre_write,                               \
            .post_write = int_disable_post_write                              \
        }, {                                                                  \
            .name = #NAME "_INT_MASK",                                        \
            .addr = A_INT_MASK,                                               \
            .ro = ~0,                                                         \
            .reset = XLNX_CSU_DMA_INT_R_MASK                                  \
        }, {                                                                  \
            .name = #NAME "_CTRL2",                                           \
            .addr = A_CTRL2,                                                  \
            .reset = ((R_CTRL2_TIMEOUT_PRE_RESET                              \
                       << R_CTRL2_TIMEOUT_PRE_SHIFT)                          \
                      | (R_CTRL2_MAX_OUTS_CMDS_RESET                          \
                         << R_CTRL2_MAX_OUTS_CMDS_SHIFT))                     \
        }, {                                                                  \
            .name = #NAME "_ADDR_MSB",                                        \
            .addr = A_ADDR_MSB,                                               \
            .pre_write = addr_msb_pre_write                                   \
        }, {                                                                  \
            .name = #NAME "_CRC1",                                            \
            .addr = A_CRC1,                                                   \
        }, {                                                                  \
            .name = #NAME "_CRC2",                                            \
            .addr = A_CRC2,                                                   \
        }, {                                                                  \
            .name = #NAME "_CRC3",                                            \
            .addr = A_CRC3,                                                   \
        }                                                                     \
    }

    DMACH_REGINFO(DMA_SRC, true),
    DMACH_REGINFO(DMA_DST, false)
};

static const MemoryRegionOps xlnx_csu_dma_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void xlnx_csu_dma_src_timeout_hit(void *opaque)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(opaque);

    /* Ignore if the timeout is masked */
    if (!xlnx_csu_dma_timeout_enabled(s)) {
        return;
    }

    s->regs[R_INT_STATUS] |= R_INT_STATUS_TIMEOUT_STRM_MASK;
    xlnx_csu_dma_update_irq(s);
}

static size_t xlnx_csu_dma_stream_push(StreamSink *obj, uint8_t *buf,
                                       size_t len, bool eop)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(obj);
    uint32_t size = s->regs[R_SIZE];
    uint32_t mlen = MIN(size, len);

    /* Be called when it's DST */
    assert(s->is_dst);

    if (!s->allow_unaligned) {
        mlen &= R_SIZE_SIZE_MASK; /* size is word aligned */
    }

    if (size == 0 || len <= 0) {
        return 0;
    }

    if (len && (xlnx_csu_dma_is_paused(s) || mlen == 0)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csu-dma: DST channel dropping %zd b of data.\n", len);
        s->regs[R_INT_STATUS] |= R_INT_STATUS_FIFO_OVERFLOW_MASK;
        return len;
    }

    if (xlnx_csu_dma_write(s, buf, mlen) != mlen) {
        return 0;
    }

    xlnx_csu_dma_advance(s, mlen);
    xlnx_csu_dma_update_irq(s);

    return mlen;
}

static bool xlnx_csu_dma_stream_can_push(StreamSink *obj,
                                         StreamCanPushNotifyFn notify,
                                         void *notify_opaque)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(obj);

    if (s->regs[R_SIZE] != 0) {
        return true;
    } else {
        s->notify = notify;
        s->notify_opaque = notify_opaque;
        return false;
    }
}

static void xlnx_csu_dma_reset(DeviceState *dev)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void xlnx_csu_dma_realize(DeviceState *dev, Error **errp)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(dev);
    RegisterInfoArray *reg_array;
    StreamSink * const TX_DEVS[] = { s->tx_dev, s->tx_dev0, s->tx_dev1 };
    static const char TX_DEVS_NAME[] = { ' ', '0', '1' };
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(TX_DEVS) != ARRAY_SIZE(TX_DEVS_NAME));

    if (s->width != 4 && s->width != 16) {
        error_setg(errp, TYPE_XLNX_CSU_DMA ": unsupported value for "
                         "`width' property");
        return;
    }

    if (!s->is_dst) {
        size_t i, target = 0;

        for (i = 1; i < ARRAY_SIZE(TX_DEVS); i++) {
            if (TX_DEVS[i] != NULL) {
                if (TX_DEVS[target] != NULL) {
                    error_setg(errp, "zynqmp.csu-dma: both tx_dev%c "
                               "and tx_dev%c StreamSinks are defined",
                               TX_DEVS_NAME[target], TX_DEVS_NAME[i]);
                    return;
                }
                target = i;
            }
        }

        s->tx_dev = TX_DEVS[target];

        if (s->tx_dev == NULL) {
            error_setg(errp, "zynqmp.csu-dma: Stream not connected");
            return;
        }
    }

    if (!s->dma_mr) {
        error_setg(errp, TYPE_XLNX_CSU_DMA " 'dma' link not set");
        return;
    }
    address_space_init(&s->dma_as, s->dma_mr, "csu-dma");

    reg_array =
        register_init_block32(dev, xlnx_csu_dma_regs_info[!!s->is_dst],
                              XLNX_CSU_DMA_R_MAX,
                              s->regs_info, s->regs,
                              &xlnx_csu_dma_ops,
                              XLNX_CSU_DMA_ERR_DEBUG,
                              XLNX_CSU_DMA_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->src_timer = ptimer_init(xlnx_csu_dma_src_timeout_hit,
                               s, PTIMER_POLICY_LEGACY);

    if (!s->attr_r) {
        Object *attr = object_new(TYPE_MEMORY_TRANSACTION_ATTR);
        s->attr_r = MEMORY_TRANSACTION_ATTR(attr);
        *s->attr_r = MEMTXATTRS_UNSPECIFIED;
    }

    if (!s->attr_w) {
        s->attr_w = s->attr_r;
    }

    s->r_size_last_word = 0;
}

static const VMStateDescription vmstate_xlnx_csu_dma = {
    .name = TYPE_XLNX_CSU_DMA,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(src_timer, XlnxCSUDMA),
        VMSTATE_UINT16(width, XlnxCSUDMA),
        VMSTATE_BOOL(is_dst, XlnxCSUDMA),
        VMSTATE_BOOL(r_size_last_word, XlnxCSUDMA),
        VMSTATE_UINT32_ARRAY(regs, XlnxCSUDMA, XLNX_CSU_DMA_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property xlnx_csu_dma_properties[] = {
    /*
     * Ref PG021, Stream Data Width:
     * Data width in bits of the AXI S2MM AXI4-Stream Data bus.
     * This value must be equal or less than the Memory Map Data Width.
     * Valid values are 4 and 16. When set to 16, the DMA will expose 4 32 bits
     * CRC registers instead of one.
     * "dma-width" is the byte value of the "Stream Data Width".
     */
    DEFINE_PROP_UINT16("dma-width", XlnxCSUDMA, width, 4),
    /*
     * The CSU DMA is a two-channel, simple DMA, allowing separate control of
     * the SRC (read) channel and DST (write) channel. "is-dst" is used to mark
     * which channel the device is connected to.
     */
    DEFINE_PROP_BOOL("is-dst", XlnxCSUDMA, is_dst, false),
    /*
     * The DMA can either have a 4-bytes alignement constraint on the address
     * and size registers, or allows unaligned accesses. When byte-align is
     * false, the accesses are 4 bytes aligned. When true, unaligned accesses
     * are allowed. In case of a source DMA (is-dst == false), this also means
     * that the LAST_WORD bit in the size register moves to bit 29.
     */
    DEFINE_PROP_BOOL("byte-align", XlnxCSUDMA, allow_unaligned, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void xlnx_csu_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSinkClass *ssc = STREAM_SINK_CLASS(klass);
    XlnxCSUDMAClass *xcdc = XLNX_CSU_DMA_CLASS(klass);

    dc->reset = xlnx_csu_dma_reset;
    dc->realize = xlnx_csu_dma_realize;
    dc->vmsd = &vmstate_xlnx_csu_dma;
    device_class_set_props(dc, xlnx_csu_dma_properties);

    ssc->push = xlnx_csu_dma_stream_push;
    ssc->can_push = xlnx_csu_dma_stream_can_push;

    xcdc->read = xlnx_csu_dma_class_read;
}

static void xlnx_csu_dma_init(Object *obj)
{
    XlnxCSUDMA *s = XLNX_CSU_DMA(obj);

    memory_region_init(&s->iomem, obj, TYPE_XLNX_CSU_DMA,
                       XLNX_CSU_DMA_R_MAX * 4);

    object_property_add_link(obj, "stream-connected-dma", TYPE_STREAM_SINK,
                             (Object **)&s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "stream-connected-dma0", TYPE_STREAM_SINK,
                             (Object **)&s->tx_dev0,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "stream-connected-dma1", TYPE_STREAM_SINK,
                             (Object **)&s->tx_dev1,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "memattr", TYPE_MEMORY_TRANSACTION_ATTR,
                             (Object **)&s->attr_r,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "memattr-write", TYPE_MEMORY_TRANSACTION_ATTR,
                             (Object **)&s->attr_w,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static const TypeInfo xlnx_csu_dma_info = {
    .name          = TYPE_XLNX_CSU_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxCSUDMA),
    .class_init    = xlnx_csu_dma_class_init,
    .class_size    = sizeof(XlnxCSUDMAClass),
    .instance_init = xlnx_csu_dma_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SINK },
        { }
    }
};

static const TypeInfo xlnx_csu_dma_alias_info = {
    .name   = TYPE_XLNX_CSU_DMA_ALIAS,
    .parent = TYPE_XLNX_CSU_DMA,
};

static void xlnx_csu_dma_register_types(void)
{
    type_register_static(&xlnx_csu_dma_info);
    type_register_static(&xlnx_csu_dma_alias_info);
}

type_init(xlnx_csu_dma_register_types)
