/*
 * QEMU model of the Xilinx Zynq SPI controller
 *
 * Copyright (c) 2012 Peter A. G. Crosthwaite
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
#include "sysemu/sysemu.h"
#include "hw/ptimer.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "hw/ssi/xilinx_spips.h"
#include "qapi/error.h"
#include "hw/register-dep.h"
#include "sysemu/dma.h"
#include "migration/blocker.h"

#ifndef XILINX_SPIPS_ERR_DEBUG
#define XILINX_SPIPS_ERR_DEBUG 0
#endif

#define DB_PRINT_L(level, ...) do { \
    if (XILINX_SPIPS_ERR_DEBUG > (level)) { \
        qemu_log_mask(DEV_LOG_SPI, ": %s: ", __func__); \
        qemu_log_mask(DEV_LOG_SPI, ## __VA_ARGS__); \
    } \
} while (0);

/* config register */
#define R_CONFIG            (0x00 / 4)
#define IFMODE              (1U << 31)
#define R_CONFIG_ENDIAN     (1 << 26)
#define MODEFAIL_GEN_EN     (1 << 17)
#define MAN_START_COM       (1 << 16)
#define MAN_START_EN        (1 << 15)
#define MANUAL_CS           (1 << 14)
#define CS                  (0xF << 10)
#define CS_SHIFT            (10)
#define PERI_SEL            (1 << 9)
#define REF_CLK             (1 << 8)
#define FIFO_WIDTH          (3 << 6)
#define BAUD_RATE_DIV       (7 << 3)
#define CLK_PH              (1 << 2)
#define CLK_POL             (1 << 1)
#define MODE_SEL            (1 << 0)
#define R_CONFIG_RSVD       (0x7bf40000)

/* interrupt mechanism */
#define R_INTR_STATUS       (0x04 / 4)
#define R_INTR_EN           (0x08 / 4)
#define R_INTR_DIS          (0x0C / 4)
#define R_INTR_MASK         (0x10 / 4)
#define IXR_TX_FIFO_UNDERFLOW   (1 << 6)
/* FIXME: Poll timeout not implemented */
#define IXR_RX_FIFO_EMPTY       (1 << 11)
#define IXR_GENERIC_FIFO_FULL   (1 << 10)
#define IXR_GENERIC_FIFO_NOT_FULL (1 << 9)
#define IXR_TX_FIFO_EMPTY       (1 << 8)
#define IXR_GENERIC_FIFO_EMPTY  (1 << 7)
#define IXR_RX_FIFO_FULL        (1 << 5)
#define IXR_RX_FIFO_NOT_EMPTY   (1 << 4)
#define IXR_TX_FIFO_FULL        (1 << 3)
#define IXR_TX_FIFO_NOT_FULL    (1 << 2)
#define IXR_TX_FIFO_MODE_FAIL   (1 << 1)
#define IXR_RX_FIFO_OVERFLOW    (1 << 0)
#define IXR_ALL                 ((1 << 13) - 1)
#define GQSPI_IXR_MASK          0xFBE

#define IXR_SELF_CLEAR \
( IXR_GENERIC_FIFO_EMPTY \
| IXR_GENERIC_FIFO_FULL  \
| IXR_GENERIC_FIFO_NOT_FULL \
| IXR_TX_FIFO_EMPTY \
| IXR_TX_FIFO_FULL  \
| IXR_TX_FIFO_NOT_FULL \
| IXR_RX_FIFO_EMPTY \
| IXR_RX_FIFO_FULL  \
| IXR_RX_FIFO_NOT_EMPTY)

#define R_EN                (0x14 / 4)
#define R_DELAY             (0x18 / 4)
#define R_TX_DATA           (0x1C / 4)
#define R_RX_DATA           (0x20 / 4)
#define R_SLAVE_IDLE_COUNT  (0x24 / 4)
#define R_TX_THRES          (0x28 / 4)
#define R_RX_THRES          (0x2C / 4)
#define R_TXD1              (0x80 / 4)
#define R_TXD2              (0x84 / 4)
#define R_TXD3              (0x88 / 4)

#define R_LQSPI_CFG         (0xa0 / 4)
#define R_LQSPI_CFG_RESET       0x03A002EB
#define LQSPI_CFG_LQ_MODE       (1U << 31)
#define LQSPI_CFG_TWO_MEM       (1 << 30)
#define LQSPI_CFG_SEP_BUS       (1 << 29)
#define LQSPI_CFG_U_PAGE        (1 << 28)
#define LQSPI_CFG_ADDR4         (1 << 27)
#define LQSPI_CFG_MODE_EN       (1 << 25)
#define LQSPI_CFG_MODE_WIDTH    8
#define LQSPI_CFG_MODE_SHIFT    16
#define LQSPI_CFG_DUMMY_WIDTH   3
#define LQSPI_CFG_DUMMY_SHIFT   8
#define LQSPI_CFG_INST_CODE     0xFF

#define R_CMND        (0xc0 / 4)
    #define R_CMND_RXFIFO_DRAIN   (1 << 19)
    /* FIXME: Implement */
    DEP_FIELD(CMND, PARTIAL_BYTE_LEN, 3, 16)
#define R_CMND_EXT_ADD        (1 << 15)
    /* FIXME: implement on finer grain than byte level */
    DEP_FIELD(CMND, RX_DISCARD, 7, 8)
    /* FIXME: Implement */
    DEP_FIELD(CMND, DUMMY_CYCLES, 6, 2)
#define R_CMND_DMA_EN         (1 << 1)
#define R_CMND_PUSH_WAIT      (1 << 0)

#define R_TRANSFER_SIZE     (0xc4 / 4)

#define R_LQSPI_STS         (0xA4 / 4)
#define LQSPI_STS_WR_RECVD      (1 << 1)

#define R_MOD_ID            (0xFC / 4)

#define R_GQSPI_SELECT          (0x144 / 4)
    DEP_FIELD(GQSPI_SELECT, GENERIC_QSPI_EN,          1, 0)
#define R_GQSPI_ISR         (0x104 / 4)
#define R_GQSPI_IER         (0x108 / 4)
#define R_GQSPI_IDR         (0x10c / 4)
#define R_GQSPI_IMR         (0x110 / 4)
#define R_GQSPI_TX_THRESH   (0x128 / 4)
#define R_GQSPI_RX_THRESH   (0x12c / 4)

#define R_GQSPI_CNFG        (0x100 / 4)
    DEP_FIELD(GQSPI_CNFG, MODE_EN,               2, 30)
    DEP_FIELD(GQSPI_CNFG, GEN_FIFO_START_MODE,   1, 29)
    DEP_FIELD(GQSPI_CNFG, GEN_FIFO_START,        1, 28)
    DEP_FIELD(GQSPI_CNFG, ENDIAN,                1, 26)
    /* FIXME: Poll timeout not implemented this phase */
    DEP_FIELD(GQSPI_CNFG, EN_POLL_TIMEOUT,       1, 20)
    /* QEMU doesnt care about any of these last three */
    DEP_FIELD(GQSPI_CNFG, BR,                    3, 3)
    DEP_FIELD(GQSPI_CNFG, CPH,                   1, 2)
    DEP_FIELD(GQSPI_CNFG, CPL,                   1, 1)

#define R_GQSPI_GEN_FIFO        (0x140 / 4)

#define R_GQSPI_TXD             (0x11c / 4)
#define R_GQSPI_RXD             (0x120 / 4)

#define R_GQSPI_FIFO_CTRL       (0x14c / 4)
    DEP_FIELD(GQSPI_FIFO_CTRL, RX_FIFO_RESET,       1, 2)
    DEP_FIELD(GQSPI_FIFO_CTRL, TX_FIFO_RESET,       1, 1)
    DEP_FIELD(GQSPI_FIFO_CTRL, GENERIC_FIFO_RESET,  1, 0)

#define R_GQSPI_GFIFO_THRESH    (0x150 / 4)

#define R_GQSPI_DATA_STS (0x15c / 4)

/* We use the snapshot register to hold the core state for the currently
 * or most recently executed command. So the generic fifo format is defined
 * for the snapshot register
 */
#define R_GQSPI_GF_SNAPSHOT (0x160 / 4)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, POLL,              1, 19)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, STRIPE,            1, 18)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, RECIEVE,           1, 17)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, TRANSMIT,          1, 16)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, DATA_BUS_SELECT,   2, 14)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, CHIP_SELECT,       2, 12)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, SPI_MODE,          2, 10)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, EXPONENT,          1, 9)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, DATA_XFER,         1, 8)
    DEP_FIELD(GQSPI_GF_SNAPSHOT, IMMEDIATE_DATA,    8, 0)

#define R_GQSPI_MOD_ID        (0x168 / 4)
#define R_GQSPI_MOD_ID_VALUE  0x010A0000

/* size of TXRX FIFOs */
#define RXFF_A          (128)
#define TXFF_A          (128)

#define RXFF_A_Q          (64 * 4)
#define TXFF_A_Q          (64 * 4)

/* 16MB per linear region */
#define LQSPI_ADDRESS_BITS 24

#define SNOOP_CHECKING 0xFF
#define SNOOP_NONE 0xFE
#define SNOOP_STRIPING 0

static inline int num_effective_busses(XilinxSPIPS *s)
{
    return (s->regs[R_LQSPI_CFG] & LQSPI_CFG_SEP_BUS &&
            s->regs[R_LQSPI_CFG] & LQSPI_CFG_TWO_MEM) ? s->num_busses : 1;
}

static void xilinx_spips_update_cs_lines_legacy_mangle(XilinxSPIPS *s,
                                                       int *field) {
    *field = ~((s->regs[R_CONFIG] & CS) >> CS_SHIFT);
    /* In dual parallel, mirror low CS to both */
    if (num_effective_busses(s) == 2) {
        /* Signle bit chip-select for qspi */
        *field &= 0x1;
        *field &= ~(1 << 1);
        *field |= *field << 1;
    /* Dual stack U-Page */
    } else if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_TWO_MEM &&
               s->regs[R_LQSPI_STS] & LQSPI_CFG_U_PAGE) {
        /* Signle bit chip-select for qspi */
        *field &= 0x1;
        /* change from CS0 to CS1 */
        *field <<= 1;
    }
    /* Auto CS */
    if (!(s->regs[R_CONFIG] & MANUAL_CS) &&
        fifo_is_empty(&s->tx_fifo)) {
        *field = 0;
    }
}

static void xilinx_spips_update_cs_lines_generic_mangle(XilinxSPIPS *s,
                                                        int *field) {
    *field = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, CHIP_SELECT);
}

static void xilinx_spips_update_cs_lines(XilinxSPIPS *s)
{
    int i;
    int field = 0;

    if (!DEP_AF_EX32(s->regs, GQSPI_SELECT, GENERIC_QSPI_EN)) {
        xilinx_spips_update_cs_lines_legacy_mangle(s, &field);
    } else {
        if (!s->regs[R_GQSPI_GF_SNAPSHOT]) {
            return;
        }
        xilinx_spips_update_cs_lines_generic_mangle(s, &field);
    }

    for (i = 0; i < s->num_cs; i++) {
        bool old_state = s->cs_lines_state[i];
        bool new_state = field & (1 << i);

        if (old_state != new_state) {
            s->cs_lines_state[i] = new_state;
            s->rx_discard = DEP_AF_EX32(s->regs, CMND, RX_DISCARD);
            DB_PRINT_L(0, "%sselecting slave %d\n", new_state ? "" : "de", i);
        }
        qemu_set_irq(s->cs_lines[i], !new_state);
    }

    if (!(field & ((1 << s->num_cs) - 1))) {
        s->snoop_state = SNOOP_CHECKING;
        s->link_state = 1;
        s->link_state_next = 1;
        s->link_state_next_when = 0;
        DB_PRINT_L(1, "moving to snoop check state\n");
    }
}


#define ZYNQMP_ONLY(a) ((zynqmp) ? (a) : (0))

static void xilinx_spips_update_ixr(XilinxSPIPS *s)
{
    int new_irqline;
    uint32_t qspi_int, gqspi_int;
    bool zynqmp = false;

    if (object_dynamic_cast(OBJECT(s), TYPE_ZYNQMP_QSPIPS)) {
        zynqmp = true;
    }

    /* these are pure functions of fifo state, set them here */
    s->regs[R_GQSPI_ISR] &= ~IXR_SELF_CLEAR;
    s->regs[R_GQSPI_ISR] |=
        (fifo_is_empty(&s->fifo_g) ? IXR_GENERIC_FIFO_EMPTY : 0) |
        (fifo_is_full(&s->fifo_g) ? IXR_GENERIC_FIFO_FULL : 0) |
        (s->fifo_g.num < s->regs[R_GQSPI_GFIFO_THRESH] ?
                                    IXR_GENERIC_FIFO_NOT_FULL : 0) |

        (fifo_is_empty(&s->rx_fifo_g) ? IXR_RX_FIFO_EMPTY : 0) |
        (fifo_is_full(&s->rx_fifo_g) ? IXR_RX_FIFO_FULL : 0) |
        (s->rx_fifo_g.num >= s->regs[R_GQSPI_RX_THRESH] ?
                                    IXR_RX_FIFO_NOT_EMPTY : 0) |

        (fifo_is_empty(&s->tx_fifo_g) ? IXR_TX_FIFO_EMPTY : 0) |
        (fifo_is_full(&s->tx_fifo_g) ? IXR_TX_FIFO_FULL : 0) |
        (s->tx_fifo_g.num < s->regs[R_GQSPI_TX_THRESH] ?
                                    IXR_TX_FIFO_NOT_FULL : 0);

    if (!(s->regs[R_LQSPI_CFG] & LQSPI_CFG_LQ_MODE)) {
        s->regs[R_INTR_STATUS] &= ~IXR_SELF_CLEAR;
        s->regs[R_INTR_STATUS] |=
            (fifo_is_full(&s->rx_fifo) ? IXR_RX_FIFO_FULL : 0) |
            (s->rx_fifo.num >= s->regs[R_RX_THRES] ?
                                    IXR_RX_FIFO_NOT_EMPTY : 0) |
            ZYNQMP_ONLY(fifo_is_empty(&s->tx_fifo) ? IXR_TX_FIFO_EMPTY : 0) |
            (fifo_is_full(&s->tx_fifo) ? IXR_TX_FIFO_FULL : 0) |
            (s->tx_fifo.num < s->regs[R_TX_THRES] ? IXR_TX_FIFO_NOT_FULL : 0);
    }

    /* QSPI/SPI Interrupt Trigger Status */
    qspi_int = s->regs[R_INTR_MASK] & s->regs[R_INTR_STATUS];
    /* GQSPI Interrupt Trigger Status */
    gqspi_int = (~s->regs[R_GQSPI_IMR]) & s->regs[R_GQSPI_ISR] &
                   GQSPI_IXR_MASK;
    /* drive external interrupt pin */
    new_irqline = !!((qspi_int | gqspi_int) & IXR_ALL);
    if (new_irqline != s->irqline) {
        DB_PRINT_L(0, "IRQ state is changing %" PRIx32 " -> %" PRIx32 "\n",
                   s->irqline, new_irqline);
        s->irqline = new_irqline;
        qemu_set_irq(s->irq, s->irqline);
    }
}

static void xilinx_spips_reset(DeviceState *d)
{
    XilinxSPIPS *s = XILINX_SPIPS(d);

    int i;
    for (i = 0; i < XLNX_SPIPS_R_MAX; i++) {
        s->regs[i] = 0;
    }

    fifo_reset(&s->rx_fifo);
    fifo_reset(&s->rx_fifo);
    fifo_reset(&s->rx_fifo_g);
    fifo_reset(&s->rx_fifo_g);
    fifo_reset(&s->fifo_g);
    /* non zero resets */
    s->regs[R_CONFIG] |= MODEFAIL_GEN_EN;
    s->regs[R_SLAVE_IDLE_COUNT] = 0xFF;
    s->regs[R_TX_THRES] = 1;
    s->regs[R_RX_THRES] = 1;
    s->regs[R_GQSPI_TX_THRESH] = 1;
    s->regs[R_GQSPI_RX_THRESH] = 1;
    s->regs[R_GQSPI_GFIFO_THRESH] = 1;
    s->regs[R_GQSPI_IMR] = GQSPI_IXR_MASK;
    /* FIXME: move magic number definition somewhere sensible */
    s->regs[R_MOD_ID] = 0x01090106;
    s->regs[R_LQSPI_CFG] = R_LQSPI_CFG_RESET;
    s->link_state = 1;
    s->link_state_next = 1;
    s->link_state_next_when = 0;
    s->snoop_state = SNOOP_CHECKING;
    s->man_start_com = false;
    s->man_start_com_g = false;
    xilinx_spips_update_ixr(s);
    xilinx_spips_update_cs_lines(s);
}

/* N way (num) in place bit striper. Lay out row wise bits column wise
 * (from element 0 to N-1). num is the length of x, and dir reverses the
 * direction of the transform. be determines the bit endianess scheme.
 * false to lay out bits LSB to MSB (little endian) and true for big endian.
 *
 * Best illustrated by examples:
 * Each digit in the below array is a single bit (num == 3, be == false):
 *
 * {{ 76543210, }  ----- stripe (dir == false) -----> {{ FCheb630, }
 *  { hgfedcba, }                                      { GDAfc741, }
 *  { HGFEDCBA, }} <---- upstripe (dir == true) -----  { HEBgda52, }}
 *
 * Same but with be == true:
 *
 * {{ 76543210, }  ----- stripe (dir == false) -----> {{ 741gdaFC, }
 *  { hgfedcba, }                                      { 630fcHEB, }
 *  { HGFEDCBA, }} <---- upstripe (dir == true) -----  { 52hebGDA, }}
 */

static inline void stripe8(uint8_t *x, int num, bool dir, bool be)
{
    uint8_t r[num];
    memset(r, 0, sizeof(uint8_t) * num);
    int idx[2] = {0, 0};
    int bit[2] = {0, be ? 7 : 0};
    int d = dir;

    for (idx[0] = 0; idx[0] < num; ++idx[0]) {
        for (bit[0] = be ? 7 : 0; bit[0] != (be ? -1 : 8); bit[0] += be ? -1 : 1) {
            r[idx[!d]] |= x[idx[d]] & 1 << bit[d] ? 1 << bit[!d] : 0;
            idx[1] = (idx[1] + 1) % num;
            if (!idx[1]) {
                bit[1] += be ? -1 : 1;
            }
        }
    }
    memcpy(x, r, sizeof(uint8_t) * num);
}

static void xilinx_spips_flush_fifo_g(XilinxSPIPS *s)
{
    XilinxQSPIPS *qs = XILINX_QSPIPS(s);
    while (s->regs[R_GQSPI_DATA_STS] || !fifo_is_empty(&s->fifo_g)) {
        uint8_t tx_rx[2];
        int num_stripes;
        int i;
        uint8_t busses;
        uint8_t spi_mode = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, SPI_MODE);

        /* memset() get's optimised out and results in the kernel seeing bogus
         * data and complaining. So until memset_s() is supported let's just do
         * it like this.
         */
        memset(tx_rx, 0, sizeof(tx_rx));
        i = tx_rx[0];
        i += tx_rx[1];

        if (!s->regs[R_GQSPI_DATA_STS]) {
            uint8_t imm;

            s->regs[R_GQSPI_GF_SNAPSHOT] = fifo_pop32(&s->fifo_g);
            DB_PRINT_L(0, "Popped GQSPI command %" PRIx32 "\n",
                       s->regs[R_GQSPI_GF_SNAPSHOT]);
            if (!s->regs[R_GQSPI_GF_SNAPSHOT]) {
                DB_PRINT_L(0, "Dummy GQSPI Delay Command Entry, Do nothing");
                continue;
            }
            xilinx_spips_update_cs_lines(s);

            busses = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_BUS_SELECT);
            if (qs->spi_mode != spi_mode) {
                qs->spi_mode = spi_mode;
                switch (busses) {
                case 0:
                    break;
                case 3:
                    ssi_set_datalines(s->spi[0], 1 << (qs->spi_mode - 1));
                    ssi_set_datalines(s->spi[1], 1 << (qs->spi_mode - 1));
                    break;
                default:
                    ssi_set_datalines(s->spi[busses - 1],
                                      1 << (qs->spi_mode - 1));
                }
            }

            imm = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, IMMEDIATE_DATA);
            if (!DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_XFER)) {
                /* immedate transfer */
                if (DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT) ||
                    DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE)) {
                    s->regs[R_GQSPI_DATA_STS] = 1;
                /* CS setup/hold - do nothing */
                } else {
                    s->regs[R_GQSPI_DATA_STS] = 0;
                }
            /* exponential transfer */
            } else if (DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, EXPONENT)) {
                if (imm > 31) {
                    qemu_log_mask(LOG_UNIMP, "QSPI exponential transfer too long"
                                  " - 2 ^ %" PRId8 " requested\n", imm);
                }
                s->regs[R_GQSPI_DATA_STS] = 1ul << imm;
            /* non-exponential data transfer */
            } else {
                s->regs[R_GQSPI_DATA_STS] = imm;
            }
            /* Dummy transfers are in terms of clocks rather than bytes */
            if (!DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT) &&
                !DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE)) {
                s->regs[R_GQSPI_DATA_STS] *= 1 << (spi_mode - 1);
                s->regs[R_GQSPI_DATA_STS] /= 8;
            }
        }

        /* Zero length transfer? no thanks! */
        if (!s->regs[R_GQSPI_DATA_STS]) {
            continue;
        }

        if (DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE) &&
            fifo_is_full(&s->rx_fifo_g)) {
            /* No space in RX fifo for transfer - try again later */
            return;
        }

        num_stripes = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, STRIPE) ? 2 : 1;
        if (!DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT) &&
            !DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE)) {
            num_stripes = 1;
        }

        if (!DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_XFER)) {
            tx_rx[0] = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, IMMEDIATE_DATA);
        } else if (DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, TRANSMIT)) {
            for (i = 0; i < num_stripes; ++i) {
                if (!fifo_is_empty(&s->tx_fifo_g)) {
                    tx_rx[i] = fifo_pop8(&s->tx_fifo_g);
                    s->tx_fifo_g_align++;
                } else {
                    return;
                }
            }
        }
        if (num_stripes == 1) {
            /* mirror */
            for (i = 1; i < 2; ++i) {
                tx_rx[i] = tx_rx[0];
            }
        }

        busses = DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, DATA_BUS_SELECT);
        for (i = 0; i < 2; ++i) {
            if (busses & (1 << i)) {
                DB_PRINT_L(1, "bus %d tx = %02x\n", i, tx_rx[i]);
            }
            tx_rx[i] = ssi_transfer(s->spi[i], tx_rx[i]);
            if (busses & (1 << i)) {
                DB_PRINT_L(1, "bus %d rx = %02x\n", i, tx_rx[i]);
            }
        }

        switch (busses) {
        case 0x3:
            if (num_stripes == 2) {
                s->regs[R_GQSPI_DATA_STS]--;
            }
            /* fallthrough */
        default:
            if (s->regs[R_GQSPI_DATA_STS] != 0) {
                /* Don't let this wrap around */
                s->regs[R_GQSPI_DATA_STS]--;
            }
        }

        if (DEP_AF_EX32(s->regs, GQSPI_GF_SNAPSHOT, RECIEVE)) {

            for (i = 0; i < 2; ++i) {
                if (busses & (1 << i)) {
                    DB_PRINT_L(1, "bus %d push_byte = %02x\n",
                               i, tx_rx[i]);
                   fifo_push8(&s->rx_fifo_g, tx_rx[i]);
                   s->rx_fifo_g_align++;
                }
            }
        }

        if (!s->regs[R_GQSPI_DATA_STS]) {
            for (; s->tx_fifo_g_align % 4; s->tx_fifo_g_align++) {
                fifo_pop8(&s->tx_fifo_g);
            }
            for (; s->rx_fifo_g_align % 4; s->rx_fifo_g_align++) {
                fifo_push8(&s->rx_fifo_g, 0);
            }
        }
    }
}

static int xilinx_spips_num_dummies(XilinxQSPIPS *qs, uint8_t command)
{
    if (!qs) {
        /* The SPI device is not a QSPI device */
        return -1;
    }

    switch (command) { /* check for dummies */
    case READ: /* no dummy bytes/cycles */
    case PP:
    case DPP:
    case QPP:
    case READ_4:
    case PP_4:
    case QPP_4:
        return 0;
    case FAST_READ: /* 1 dummy byte */
    case DOR:
    case QOR:
    case DOR_4:
    case QOR_4:
        return 1 * (1 << (qs->spi_mode - 1));
    case DIOR: /* FIXME: these vary between vendor - set to spansion */
    case FAST_READ_4:
    case DIOR_4:
        return 2 * (1 << (qs->spi_mode - 1));
    case QIOR: /* 2 Mode and 1 dummy byte */
    case QIOR_4:
        return 5 * (1 << (qs->spi_mode - 1));
    default:
        return -1;
    }
}

static void xilinx_spips_flush_txfifo(XilinxSPIPS *s)
{
    int debug_level = 0;
    XilinxQSPIPS *q = (XilinxQSPIPS *) object_dynamic_cast(OBJECT(s),
                                                           TYPE_XILINX_QSPIPS);

    for (;;) {
        int i;
        uint8_t tx = 0;
        uint8_t tx_rx[num_effective_busses(s)];
        int num_dummies;

        if (fifo_is_empty(&s->tx_fifo)) {
            xilinx_spips_update_ixr(s);
            return;
        } else if (s->snoop_state == SNOOP_STRIPING) {
            tx_rx[0] = fifo_pop8(&s->tx_fifo);
            stripe8(tx_rx, num_effective_busses(s), false, true);
        } else {
            tx = fifo_pop8(&s->tx_fifo);
            for (i = 0; i < num_effective_busses(s); ++i) {
                tx_rx[i] = tx;
            }
        }

        for (i = 0; i < num_effective_busses(s); ++i) {
            int len = s->snoop_state == SNOOP_STRIPING ? 4 : 8;

            if (s->snoop_state == SNOOP_STRIPING) {
                len = 8 / num_effective_busses(s);
                tx_rx[i] >>= 8 - len;
            }
            DB_PRINT_L(debug_level, "tx = %02x (len = %d)\n", tx_rx[i], len);
            tx_rx[i] = ssi_transfer_bits(s->spi[num_effective_busses(s) - 1 - i],
                                         (uint32_t)tx_rx[i], len);
            DB_PRINT_L(debug_level, "rx = %02x\n", tx_rx[i]);
            if (s->snoop_state == SNOOP_STRIPING) {
                tx_rx[i] <<= 8 - len;
            }
        }

        if (s->regs[R_CMND] & R_CMND_RXFIFO_DRAIN) {
            DB_PRINT_L(debug_level, "dircarding drained rx byte\n");
            /* Do nothing */
        } else if (s->rx_discard) {
            DB_PRINT_L(debug_level, "dircarding discarded rx byte\n");
            s->rx_discard -= 8 / s->link_state;
        } else if (fifo_is_full(&s->rx_fifo)) {
            s->regs[R_INTR_STATUS] |= IXR_RX_FIFO_OVERFLOW;
            DB_PRINT_L(0, "rx FIFO overflow");
        } else if (s->snoop_state == SNOOP_STRIPING) {
            stripe8(tx_rx, num_effective_busses(s), true, true);
            fifo_push8(&s->rx_fifo, (uint8_t)tx_rx[0]);
            DB_PRINT_L(debug_level, "pushing striped rx byte\n");
        } else {
           DB_PRINT_L(debug_level, "pushing unstriped rx byte\n");
           fifo_push8(&s->rx_fifo, (uint8_t)tx_rx[0]);
        }

        if (s->link_state_next_when) {
            s->link_state_next_when--;
            if (!s->link_state_next_when) {
                s->link_state = s->link_state_next;
            }
        }

        DB_PRINT_L(debug_level, "initial snoop state: %x\n",
                   (unsigned)s->snoop_state);
        switch (s->snoop_state) {
        case (SNOOP_CHECKING):
            /* assume 3 address bytes */
            s->snoop_state =  3;
            switch (tx) { /* new instruction code */
            case READ: /* 3 address bytes, no dummy bytes/cycles */
            case PP:
            case DPP:
            case QPP:
            case FAST_READ:
            case DOR:
            case QOR:
            case DIOR:
            case QIOR:
                s->snoop_state += !!(s->regs[R_CMND] & R_CMND_EXT_ADD);
                break;
            case READ_4:
            case PP_4:
            case QPP_4:
            case FAST_READ_4:
            case DOR_4:
            case QOR_4:
            case DIOR_4:
                s->snoop_state++;
                break;
            }
            num_dummies = xilinx_spips_num_dummies(q, tx);
            if (num_dummies == -1) {
                s->snoop_state = SNOOP_NONE;
            } else {
                s->snoop_state += num_dummies;
            }
            switch (tx) {
            case DPP:
            case DOR:
            case DOR_4:
                s->link_state_next = 2;
                s->link_state_next_when = s->snoop_state;
                break;
            case QPP:
            case QPP_4:
            case QOR:
            case QOR_4:
                s->link_state_next = 4;
                s->link_state_next_when = s->snoop_state;
                break;
            case DIOR:
            case DIOR_4:
                s->link_state = 2;
                break;
            case QIOR:
            case QIOR_4:
                s->link_state = 4;
                break;
            }
            break;
        case (SNOOP_STRIPING):
        case (SNOOP_NONE):
            /* Once we hit the boring stuff - squelch debug noise */
            if (!debug_level) {
                DB_PRINT_L(0, "squelching debug info ....\n");
                debug_level = 1;
            }
            break;
        default:
            s->snoop_state--;
        }
        DB_PRINT_L(debug_level, "final snoop state: %x\n",
                   (unsigned)s->snoop_state);
    }
}

static inline void tx_data_bytes(Fifo *fifo, uint32_t value, int num, bool be)
{
    int i;
    for (i = 0; i < num && !fifo_is_full(fifo); ++i) {
        if (be) {
            fifo_push8(fifo, (uint8_t)(value >> 24));
            value <<= 8;
        } else {
            fifo_push8(fifo, (uint8_t)value);
            value >>= 8;
        }
    }
}

static void xilinx_spips_check_zero_pump(XilinxSPIPS *s)
{
    if (!s->regs[R_TRANSFER_SIZE]) {
        return;
    }

    if (!fifo_is_empty(&s->tx_fifo) && s->regs[R_CMND] & R_CMND_PUSH_WAIT) {
        return;
    }

    /*
     * The zero pump must never fill tx fifo such that rx overflow is
     * possible
     */
    while (s->regs[R_TRANSFER_SIZE] &&
           s->rx_fifo.num + s->tx_fifo.num < RXFF_A_Q - 3) {
        /* endianess just doesn't matter when zero pumping */
        tx_data_bytes(&s->tx_fifo, 0, 4, false);
        s->regs[R_TRANSFER_SIZE] &= ~0x03ull;
        s->regs[R_TRANSFER_SIZE] -= 4;
    }
}

static void xilinx_spips_check_flush(XilinxSPIPS *s)
{
    bool gqspi_has_work = s->regs[R_GQSPI_DATA_STS] ||
                          !fifo_is_empty(&s->fifo_g);

    if (DEP_AF_EX32(s->regs, GQSPI_SELECT, GENERIC_QSPI_EN)) {
        if (s->man_start_com_g ||
            (gqspi_has_work &&
             !DEP_AF_EX32(s->regs, GQSPI_CNFG, GEN_FIFO_START_MODE))) {
            xilinx_spips_flush_fifo_g(s);
        }
    } else {
        if (s->man_start_com ||
            (!fifo_is_empty(&s->tx_fifo) &&
             !(s->regs[R_CONFIG] & MAN_START_EN))) {
            xilinx_spips_check_zero_pump(s);
            xilinx_spips_flush_txfifo(s);
        }
    }

    if (fifo_is_empty(&s->tx_fifo) && !s->regs[R_TRANSFER_SIZE]) {
        s->man_start_com = false;
    }

    if (!gqspi_has_work) {
        s->man_start_com_g = false;
    }
    xilinx_spips_update_ixr(s);
}

static inline int rx_data_bytes(XilinxSPIPS *s, uint8_t *value, int max)
{
    int i;

    for (i = 0; i < max && !fifo_is_empty(&s->rx_fifo); ++i) {
        value[i] = fifo_pop8(&s->rx_fifo);
    }

    return max - i;
}

static void zynqmp_qspips_notify(void *opaque)
{
    ZynqMPQSPIPS *rq = ZYNQMP_QSPIPS(opaque);
    XilinxSPIPS *s = XILINX_SPIPS(rq);
    Fifo *recv_fifo;

    if (DEP_AF_EX32(s->regs, GQSPI_SELECT, GENERIC_QSPI_EN)) {
        if (!(DEP_AF_EX32(s->regs, GQSPI_CNFG, MODE_EN) == 2)) {
            return;
        }
        recv_fifo = &s->rx_fifo_g;
    } else {
        if (!(s->regs[R_CMND] & R_CMND_DMA_EN)) {
            return;
        }
        recv_fifo = &s->rx_fifo;
    }

    while (/* FIXME: impelement byte granularity */
           recv_fifo->num >= 4 /* FIXME: APIify */
           && stream_can_push(rq->dma, zynqmp_qspips_notify, rq))
    {
        size_t ret;
        uint32_t num;
        const void *rxd = fifo_pop_buf(recv_fifo, 4, &num);

        memcpy(rq->dma_buf, rxd, num);

        ret = stream_push(rq->dma, rq->dma_buf, 4, 0);
        assert(ret == 4); /* FIXME - implement short return */
        xilinx_spips_check_flush(s);
    }
}

static uint64_t xilinx_spips_read(void *opaque, hwaddr addr,
                                                        unsigned size)
{
    XilinxSPIPS *s = opaque;
    uint32_t mask = ~0;
    uint32_t ret;
    uint8_t rx_buf[4];
    int shortfall;
    const void *rxd;
    uint32_t rx_num;

    memset(rx_buf, 0, sizeof(rx_buf));

    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = ~(R_CONFIG_RSVD | MAN_START_COM);
        break;
    case R_INTR_STATUS:
        ret = s->regs[addr] & IXR_ALL;
        s->regs[addr] = 0;
        DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_update_ixr(s);
        return ret;
    case R_INTR_MASK:
        mask = IXR_ALL;
        break;
    case  R_EN:
        mask = 0x1;
        break;
    case R_SLAVE_IDLE_COUNT:
        mask = 0xFF;
        break;
    case R_MOD_ID:
        mask = 0x01FFFFFF;
        break;
    case R_INTR_EN:
    case R_INTR_DIS:
    case R_TX_DATA:
        mask = 0;
        break;
    case R_RX_DATA:
        memset(rx_buf, 0, sizeof(rx_buf));
        shortfall = rx_data_bytes(s, rx_buf, s->num_txrx_bytes);
        ret = s->regs[R_CONFIG] & R_CONFIG_ENDIAN ? cpu_to_be32(*(uint32_t *)rx_buf)
                        : cpu_to_le32(*(uint32_t *)rx_buf);
        if (!(s->regs[R_CONFIG] & R_CONFIG_ENDIAN)) {
            ret <<= 8 * shortfall;
        }
        DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
        xilinx_spips_check_flush(s);
        xilinx_spips_update_ixr(s);
        return ret;
    case R_GQSPI_RXD:
        if (fifo_is_empty(&s->rx_fifo_g)) {
            qemu_log_mask(LOG_GUEST_ERROR, "Read from empty GQSPI RX FIFO\n");
            return 0;
        }
        rxd = fifo_pop_buf(&s->rx_fifo_g, 4, &rx_num);
        assert(!(rx_num % 4));
        memcpy(rx_buf, rxd, rx_num);
        ret = DEP_AF_EX32(s->regs, GQSPI_CNFG, ENDIAN) ?
              cpu_to_be32(*(uint32_t *)rx_buf) :
              cpu_to_le32(*(uint32_t *)rx_buf);
        xilinx_spips_check_flush(s);
        xilinx_spips_update_ixr(s);
        return ret;
    }
    DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr * 4,
               s->regs[addr] & mask);
    return s->regs[addr] & mask;

}

static void xilinx_spips_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned size)
{
    int mask = ~0;
    int tx_btt = 0;
    XilinxSPIPS *s = opaque;

    DB_PRINT_L(0, "addr=" TARGET_FMT_plx " = %x\n", addr, (unsigned)value);
    addr >>= 2;
    switch (addr) {
    case R_CONFIG:
        mask = ~(R_CONFIG_RSVD | MAN_START_COM);
        if ((value & MAN_START_COM) && (s->regs[R_CONFIG] & MAN_START_EN)) {
            s->man_start_com = true;
        }
        break;
    case R_INTR_STATUS:
        mask = IXR_ALL;
        s->regs[R_INTR_STATUS] &= ~(mask & value);
        goto no_reg_update;
    case R_INTR_DIS:
        mask = IXR_ALL;
        s->regs[R_INTR_MASK] &= ~(mask & value);
        goto no_reg_update;
    case R_INTR_EN:
        mask = IXR_ALL;
        s->regs[R_INTR_MASK] |= mask & value;
        goto no_reg_update;
    case R_EN:
        mask = 0x1;
        break;
    case R_SLAVE_IDLE_COUNT:
        mask = 0xFF;
        break;
    case R_RX_DATA:
    case R_INTR_MASK:
    case R_MOD_ID:
        mask = 0;
        break;
    case R_TX_DATA:
        tx_btt = s->num_txrx_bytes;
        tx_data_bytes(&s->tx_fifo, (uint32_t)value, s->num_txrx_bytes,
                      s->regs[R_CONFIG] & R_CONFIG_ENDIAN);
        goto no_reg_update;
    case R_TXD3:
        tx_btt++;
    case R_TXD2:
        tx_btt++;
    case R_TXD1:
        tx_btt++;
        tx_data_bytes(&s->tx_fifo, (uint32_t)value, tx_btt,
                      s->regs[R_CONFIG] & R_CONFIG_ENDIAN);
        goto no_reg_update;
    case R_GQSPI_CNFG:
        mask = ~(R_GQSPI_CNFG_GEN_FIFO_START_MASK);
        if (DEP_F_EX32(value, GQSPI_CNFG, GEN_FIFO_START) &&
            DEP_AF_EX32(s->regs, GQSPI_CNFG, GEN_FIFO_START_MODE)) {
            s->man_start_com_g = true;
        }
        break;
    case R_GQSPI_GEN_FIFO:
        if (!fifo_is_full(&s->fifo_g)) {
            fifo_push32(&s->fifo_g, value);
        }
        goto no_reg_update;
    case R_GQSPI_TXD:
        tx_data_bytes(&s->tx_fifo_g, (uint32_t)value, 4,
                      DEP_AF_EX32(s->regs, GQSPI_CNFG, ENDIAN));
        goto no_reg_update;
    case R_GQSPI_FIFO_CTRL:
        mask = 0;
        if (DEP_F_EX32(value, GQSPI_FIFO_CTRL, GENERIC_FIFO_RESET)) {
           fifo_reset(&s->fifo_g);
        }
        if (DEP_F_EX32(value, GQSPI_FIFO_CTRL, TX_FIFO_RESET)) {
           fifo_reset(&s->tx_fifo_g);
        }
        if (DEP_F_EX32(value, GQSPI_FIFO_CTRL, RX_FIFO_RESET)) {
           fifo_reset(&s->rx_fifo_g);
        }
        break;
    case R_GQSPI_IDR:
        s->regs[R_GQSPI_IMR] |= value;
        goto no_reg_update;
    case R_GQSPI_IER:
        s->regs[R_GQSPI_IMR] &= ~value;
        goto no_reg_update;
    case R_GQSPI_ISR:
        s->regs[R_GQSPI_ISR] &= ~value;
        goto no_reg_update;
    case R_GQSPI_IMR:
    case R_GQSPI_RXD:
    case R_GQSPI_GF_SNAPSHOT:
    case R_GQSPI_MOD_ID:
        mask = 0;
        break;
    }
    s->regs[addr] = (s->regs[addr] & ~mask) | (value & mask);
no_reg_update:
    xilinx_spips_update_cs_lines(s);
    xilinx_spips_check_flush(s);
    xilinx_spips_update_cs_lines(s);
    xilinx_spips_update_ixr(s);
}

static const MemoryRegionOps spips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_spips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void xilinx_qspips_invalidate_mmio_ptr(XilinxQSPIPS *q)
{
    XilinxSPIPS *s = &q->parent_obj;

    if ((q->mmio_execution_enabled) && (q->lqspi_cached_addr != ~0ULL)) {
        /* Invalidate the current mapped mmio */
        memory_region_invalidate_mmio_ptr(&s->mmlqspi, q->lqspi_cached_addr,
                                          LQSPI_CACHE_SIZE);
    }

    q->lqspi_cached_addr = ~0ULL;
}

static void xilinx_qspips_write(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    XilinxQSPIPS *q = XILINX_QSPIPS(opaque);
    XilinxSPIPS *s = XILINX_SPIPS(opaque);

    uint32_t lqspi_cfg_old = s->regs[R_LQSPI_CFG];

    xilinx_spips_write(opaque, addr, value, size);
    addr >>= 2;

    if (addr == R_LQSPI_CFG &&
               ((lqspi_cfg_old ^ value) & ~LQSPI_CFG_U_PAGE)) {
        q->lqspi_cached_addr = ~0ULL;
        if (q->lqspi_size) {
#define LQSPI_HACK_CHUNK_SIZE (1 * 1024 * 1024)
            uint32_t src = q->lqspi_src;
            uint32_t dst = q->lqspi_dst;
            uint32_t btt = q->lqspi_size;

            assert(!(btt % LQSPI_HACK_CHUNK_SIZE));
            fprintf(stderr, "QEMU: Syncing LQSPI - this may be slow "
                    "(1 \".\" / MByte):");

            while (btt) {
                uint8_t lqspi_hack_buf[LQSPI_HACK_CHUNK_SIZE];
                dma_memory_read(q->hack_as, src, lqspi_hack_buf,
                                LQSPI_HACK_CHUNK_SIZE);
                dma_memory_write(q->hack_as, dst, lqspi_hack_buf,
                                 LQSPI_HACK_CHUNK_SIZE);
                fprintf(stderr, ".");
                btt -= LQSPI_HACK_CHUNK_SIZE;
                src += LQSPI_HACK_CHUNK_SIZE;
                dst += LQSPI_HACK_CHUNK_SIZE;
            }
            fprintf(stderr, "\n");
        }
    }
    if (s->regs[R_CMND] & R_CMND_RXFIFO_DRAIN) {
        fifo_reset(&s->rx_fifo);
    }
    if (object_dynamic_cast(OBJECT(s), TYPE_ZYNQMP_QSPIPS)) {
        zynqmp_qspips_notify(s);
    }
}

static const MemoryRegionOps qspips_ops = {
    .read = xilinx_spips_read,
    .write = xilinx_qspips_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

#define LQSPI_CACHE_SIZE 1024

static void lqspi_load_cache(void *opaque, hwaddr addr)
{
    XilinxQSPIPS *q = opaque;
    XilinxSPIPS *s = opaque;
    int i;
    int flash_addr = ((addr & ~(LQSPI_CACHE_SIZE - 1))
                   / num_effective_busses(s));
    int slave = flash_addr >> LQSPI_ADDRESS_BITS;
    int cache_entry = 0;
    uint32_t u_page_save = s->regs[R_LQSPI_STS] & ~LQSPI_CFG_U_PAGE;

    if (addr < q->lqspi_cached_addr ||
            addr > q->lqspi_cached_addr + LQSPI_CACHE_SIZE - 4) {
        xilinx_qspips_invalidate_mmio_ptr(q);
        s->regs[R_LQSPI_STS] &= ~LQSPI_CFG_U_PAGE;
        s->regs[R_LQSPI_STS] |= slave ? LQSPI_CFG_U_PAGE : 0;

        DB_PRINT_L(0, "config reg status: %08x\n", s->regs[R_LQSPI_CFG]);

        fifo_reset(&s->tx_fifo);
        fifo_reset(&s->rx_fifo);

        /* instruction */
        DB_PRINT_L(0, "pushing read instruction: %02x\n",
                   (unsigned)(uint8_t)(s->regs[R_LQSPI_CFG] &
                                       LQSPI_CFG_INST_CODE));
        fifo_push8(&s->tx_fifo, s->regs[R_LQSPI_CFG] & LQSPI_CFG_INST_CODE);
        /* read address */
        DB_PRINT_L(0, "pushing read address %06x\n", flash_addr);
        if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_ADDR4) {
            fifo_push8(&s->tx_fifo, (uint8_t)(flash_addr >> 24));
        }
        fifo_push8(&s->tx_fifo, (uint8_t)(flash_addr >> 16));
        fifo_push8(&s->tx_fifo, (uint8_t)(flash_addr >> 8));
        fifo_push8(&s->tx_fifo, (uint8_t)flash_addr);
        /* mode bits */
        if (s->regs[R_LQSPI_CFG] & LQSPI_CFG_MODE_EN) {
            fifo_push8(&s->tx_fifo, extract32(s->regs[R_LQSPI_CFG],
                                              LQSPI_CFG_MODE_SHIFT,
                                              LQSPI_CFG_MODE_WIDTH));
        }
        /* dummy bytes */
        for (i = 0; i < (extract32(s->regs[R_LQSPI_CFG], LQSPI_CFG_DUMMY_SHIFT,
                                   LQSPI_CFG_DUMMY_WIDTH)); ++i) {
            DB_PRINT_L(0, "pushing dummy byte\n");
            fifo_push8(&s->tx_fifo, 0);
        }
        xilinx_spips_update_cs_lines(s);
        xilinx_spips_flush_txfifo(s);
        fifo_reset(&s->rx_fifo);

        DB_PRINT_L(0, "starting QSPI data read\n");

        while (cache_entry < LQSPI_CACHE_SIZE) {
            for (i = 0; i < 64; ++i) {
                tx_data_bytes(&s->tx_fifo, 0, 1, false);
            }
            xilinx_spips_flush_txfifo(s);
            for (i = 0; i < 64; ++i) {
                rx_data_bytes(s, &q->lqspi_buf[cache_entry++], 1);
            }
        }

        s->regs[R_LQSPI_STS] &= ~LQSPI_CFG_U_PAGE;
        s->regs[R_LQSPI_STS] |= u_page_save;
        xilinx_spips_update_cs_lines(s);

        q->lqspi_cached_addr = flash_addr * num_effective_busses(s);
    }
}

static void *lqspi_request_mmio_ptr(void *opaque, hwaddr addr, unsigned *size,
                                    unsigned *offset)
{
    XilinxQSPIPS *q = opaque;
    hwaddr offset_within_the_region;

    if (!q->mmio_execution_enabled) {
        return NULL;
    }

    offset_within_the_region = addr & ~(LQSPI_CACHE_SIZE - 1);
    lqspi_load_cache(opaque, offset_within_the_region);
    *size = LQSPI_CACHE_SIZE;
    *offset = offset_within_the_region;
    return q->lqspi_buf;
}

static uint64_t
lqspi_read(void *opaque, hwaddr addr, unsigned int size)
{
    XilinxQSPIPS *q = opaque;
    uint32_t ret;

    if (addr >= q->lqspi_cached_addr &&
            addr <= q->lqspi_cached_addr + LQSPI_CACHE_SIZE - 4) {
        uint8_t *retp = &q->lqspi_buf[addr - q->lqspi_cached_addr];
        ret = cpu_to_le32(*(uint32_t *)retp);
        DB_PRINT_L(1, "addr: %08x, data: %08x\n", (unsigned)addr,
                   (unsigned)ret);
        return ret;
    } else {
        lqspi_load_cache(opaque, addr);
        return lqspi_read(opaque, addr, size);
    }
}

static const MemoryRegionOps lqspi_ops = {
    .read = lqspi_read,
    .request_ptr = lqspi_request_mmio_ptr,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void xilinx_spips_realize(DeviceState *dev, Error **errp)
{
    XilinxSPIPS *s = XILINX_SPIPS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_GET_CLASS(s);
    qemu_irq *cs;
    int i;

    DB_PRINT_L(0, "realized spips\n");

    s->spi = g_new(SSIBus *, s->num_busses);
    for (i = 0; i < s->num_busses; ++i) {
        char bus_name[16];
        snprintf(bus_name, 16, "spi%d", i);
        s->spi[i] = ssi_create_bus(dev, bus_name);
    }

    s->cs_lines = g_new0(qemu_irq, s->num_cs * s->num_busses);
    s->cs_lines_state = g_new0(bool, s->num_cs * s->num_busses);
    for (i = 0, cs = s->cs_lines; i < s->num_busses; ++i, cs += s->num_cs) {
        ssi_auto_connect_slaves(DEVICE(s), cs, s->spi[i]);
    }

    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out(dev, s->cs_lines, s->num_cs * s->num_busses);

    memory_region_init_io(&s->iomem, OBJECT(s), xsc->reg_ops, s,
                          "spi", XLNX_SPIPS_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);

    s->irqline = -1;

    fifo_create8(&s->rx_fifo, xsc->rx_fifo_size);
    fifo_create8(&s->tx_fifo, xsc->tx_fifo_size);
    /* FIXME: Move to zynqmp specific state */
    fifo_create8(&s->rx_fifo_g, xsc->rx_fifo_size);
    fifo_create8(&s->tx_fifo_g, xsc->tx_fifo_size);
    fifo_create32(&s->fifo_g, 32);
}

static void xilinx_qspips_realize(DeviceState *dev, Error **errp)
{
    XilinxSPIPS *s = XILINX_SPIPS(dev);
    XilinxQSPIPS *q = XILINX_QSPIPS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    DB_PRINT_L(0, "realized qspips\n");

    s->num_busses = 2;
    s->num_cs = 2;
    s->num_txrx_bytes = 4;

    xilinx_spips_realize(dev, errp);
    q->hack_as = q->hack_dma ? address_space_init_shareable(q->hack_dma,
                NULL) : &address_space_memory;
    memory_region_init_io(&s->mmlqspi, OBJECT(s), &lqspi_ops, s, "lqspi",
                          (1 << LQSPI_ADDRESS_BITS) * 2);
    sysbus_init_mmio(sbd, &s->mmlqspi);

    q->lqspi_cached_addr = ~0ULL;

    /* mmio_execution breaks migration better aborting than having strange
     * bugs.
     */
    if (q->mmio_execution_enabled) {
        error_setg(&q->migration_blocker,
                   "enabling mmio_execution breaks migration");
        migrate_add_blocker(q->migration_blocker, &error_fatal);
    }
}

static void zynqmp_qspips_init(Object *obj)
{
    ZynqMPQSPIPS *rq = ZYNQMP_QSPIPS(obj);

    object_property_add_link(obj, "stream-connected-dma", TYPE_STREAM_SLAVE,
                             (Object **)&rq->dma,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             NULL);
}

static int xilinx_spips_post_load(void *opaque, int version_id)
{
    xilinx_spips_update_ixr((XilinxSPIPS *)opaque);
    xilinx_spips_update_cs_lines((XilinxSPIPS *)opaque);
    return 0;
}

static const VMStateDescription vmstate_xilinx_spips = {
    .name = "xilinx_spips",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = xilinx_spips_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_FIFO(tx_fifo, XilinxSPIPS),
        VMSTATE_FIFO(rx_fifo, XilinxSPIPS),
        VMSTATE_UINT32_ARRAY(regs, XilinxSPIPS, XLNX_SPIPS_R_MAX),
        VMSTATE_UINT8(snoop_state, XilinxSPIPS),
        VMSTATE_END_OF_LIST()
    }
};

static Property xilinx_spips_properties[] = {
    DEFINE_PROP_UINT8("num-busses", XilinxSPIPS, num_busses, 1),
    DEFINE_PROP_UINT8("num-ss-bits", XilinxSPIPS, num_cs, 4),
    DEFINE_PROP_UINT8("num-txrx-bytes", XilinxSPIPS, num_txrx_bytes, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static Property xilinx_qspips_properties[] = {
    DEFINE_PROP_UINT32("lqspi-size", XilinxQSPIPS, lqspi_size, 0),
    DEFINE_PROP_UINT32("lqspi-src", XilinxQSPIPS, lqspi_src, 0),
    DEFINE_PROP_UINT32("lqspi-dst", XilinxQSPIPS, lqspi_dst, 0),
    /* We had to turn this off for 2.10 as it is not compatible with migration.
     * It can be enabled but will prevent the device to be migrated.
     * This will go aways when a fix will be released.
     */
    DEFINE_PROP_BOOL("x-mmio-exec", XilinxQSPIPS, mmio_execution_enabled,
                     false),
    DEFINE_PROP_END_OF_LIST(),
};

static void xilinx_qspips_init(Object *obj)
{
    XilinxQSPIPS *q = XILINX_QSPIPS(obj);

    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **) &q->hack_dma,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void xilinx_qspips_class_init(ObjectClass *klass, void * data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_CLASS(klass);

    dc->realize = xilinx_qspips_realize;
    dc->props = xilinx_qspips_properties;
    xsc->reg_ops = &qspips_ops;
    xsc->rx_fifo_size = RXFF_A_Q;
    xsc->tx_fifo_size = TXFF_A_Q;
}

static void xilinx_spips_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XilinxSPIPSClass *xsc = XILINX_SPIPS_CLASS(klass);

    dc->realize = xilinx_spips_realize;
    dc->reset = xilinx_spips_reset;
    dc->props = xilinx_spips_properties;
    dc->vmsd = &vmstate_xilinx_spips;

    xsc->reg_ops = &spips_ops;
    xsc->rx_fifo_size = RXFF_A;
    xsc->tx_fifo_size = TXFF_A;
}

static const TypeInfo xilinx_spips_info = {
    .name  = TYPE_XILINX_SPIPS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XilinxSPIPS),
    .class_init = xilinx_spips_class_init,
    .class_size = sizeof(XilinxSPIPSClass),
};

static const TypeInfo xilinx_qspips_info = {
    .name  = TYPE_XILINX_QSPIPS,
    .parent = TYPE_XILINX_SPIPS,
    .instance_size  = sizeof(XilinxQSPIPS),
    .class_init = xilinx_qspips_class_init,
    .instance_init = xilinx_qspips_init,
};

static const TypeInfo zynqmp_qspips_info = {
    .name  = TYPE_ZYNQMP_QSPIPS,
    .parent = TYPE_XILINX_QSPIPS,
    .instance_size  = sizeof(ZynqMPQSPIPS),
    .instance_init = zynqmp_qspips_init,
};

static void xilinx_spips_register_types(void)
{
    type_register_static(&xilinx_spips_info);
    type_register_static(&xilinx_qspips_info);
    type_register_static(&zynqmp_qspips_info);
}

type_init(xilinx_spips_register_types)
