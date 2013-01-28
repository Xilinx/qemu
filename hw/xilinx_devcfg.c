/*
 * QEMU model of the Xilinx Devcfg Interface
 *
 * Copyright (c) 2011 Peter A.G. Crosthwaite (peter.crosthwaite@petalogix.com)
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

#include "sysemu/sysemu.h"
#include "sysbus.h"
#include "ptimer.h"

/* FIXME: get rid of hardcoded nastiness */

#define FREQ_HZ 900000000

#ifndef XILINX_DEVCFG_ERR_DEBUG
#define XILINX_DEVCFG_ERR_DEBUG 0
#endif
#define DB_PRINT(...) do { \
    if (XILINX_DEVCFG_ERR_DEBUG) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

/* ctrl register */
#define R_CTRL            (0x00 /4)
    #define FORCE_RST            (1 << 31) /* Not supported, writes ignored -- ADD TO TICKET */
    #define PCAP_PR              (1 << 27) /* Forced to 0 on bad unlock */
    #define PCAP_MODE            (1 << 26)
    #define USER_MODE            (1 << 15)
    #define PCFG_AES_FUSE        (1 << 12) /* locked by AES_FUSE_LOCK */
    #define PCFG_AES_EN          (7 << 9) /* locked by AES_EN_LOCK, forced to 0 on bad unlock */
    #define SEU_EN               (1 << 8) /* locked by SEU_LOCK */
    #define SEC_EN               (1 << 7) /* locked by SEC_LOCK */
    #define SPNIDEN              (1 << 6) /* locked by DBG_LOCK */
    #define SPIDEN               (1 << 5) /* locked by DBG_LOCK */
    #define NIDEN                (1 << 4) /* locked by DBG_LOCK */
    #define DBGEN                (1 << 3) /* locked by DBG_LOCK */
    #define DAP_EN               (7 << 0) /* locked by DBG_LOCK */
#define R_CTRL_RO       (1 << 28) | (0x7f << 16) | USER_MODE
#define R_CTRL_RESET    PCAP_PR | PCAP_MODE

/* lock register */
#define R_LOCK          (0x04 /4)
    #define AES_FUSE_LOCK        (1 << 4)
    #define AES_EN_LOCK          (1 << 3)
    #define SEU_LOCK             (1 << 2)
    #define SEC_LOCK             (1 << 1)
    #define DBG_LOCK             (1 << 0)
#define R_LOCK_RO (~0x1F)

/* bits in ctrl affected by DBG_LOCK */
#define R_CTRL_DBG_LOCK_MASK SPNIDEN | SPIDEN | NIDEN | DBGEN | DAP_EN

/* CFG register */
#define R_CFG           (0x08 /4)
    #define RFIFO_TH             (2 << 10)
    #define WFIFO_TH             (2 << 8)
    #define DISABLE_SRC_INC      (1 << 5)
    #define DISABLE_DST_INC      (1 << 4)
#define R_CFG_RO 0xFFFFF800
#define R_CFG_RESET 0x50B

/* INT_STS register */
#define R_INT_STS       (0x0C /4)
    #define PSS_FST_CFG_B_INT    (1 << 30)
    #define RX_FIFO_OV_INT       (1 << 18)
    #define WR_FIFO_LVL_INT      (1 << 17)
    #define RD_FIFO_LVL_INT      (1 << 16)
    #define DMA_CMD_ERR_INT      (1 << 15)
    #define DMA_Q_OV_INT         (1 << 14)
    #define DMA_DONE_INT         (1 << 13)
    #define DMA_P_DONE_INT       (1 << 12)
    #define P2D_LEN_ERR_INT      (1 << 11)
    #define PCFG_DONE_INT        (1 << 2)
#define R_INT_STS_RO ~0 /* INT_STS uses a different write handler so its all read only */
#define R_INT_STS_RESET 0x88020010

/* INT_MASK register */
#define R_INT_MASK      (0x10 / 4)
#define R_INT_MASK_RESET (~0)

/* STATUS register */
#define R_STATUS        (0x14 / 4)
    #define DMA_CMD_Q_F         (1 << 31)
    #define DMA_CMD_Q_E         (1 << 30)
    #define DMA_DONE_CNT        (2 << 28)
    #define RX_FIFO_LVL         (0x1f << 20)
    #define TX_FIFO_LVL         (0x7f << 12)
    #define PSS_FST_CFG_B       (1 << 10)
#define R_STATUS_RESET 0x40000820
#define R_STATUS_RO ~0

#define R_DMA_SRC_ADDR  (0x18 / 4)
#define R_DMA_DST_ADDR  (0x1C / 4)
#define R_DMA_SRC_LEN   (0x20 / 4)
#define R_DMA_SRC_LEN_RO 0xF8000000
#define R_DMA_DEST_LEN  (0x24 / 4)
#define R_DMA_DEST_LEN_RO 0xF8000000
#define R_ROM_SHADOW    (0x28 / 4)
#define R_SW_ID         (0x30 / 4)
#define R_UNLOCK        (0x34 / 4)

#define R_UNLOCK_MAGIC 0x757BDF0D

/* MCTRL register */
#define R_MCTRL         (0x80 / 4)
    #define INT_PCAP_LPBK       (1 << 4)
    #define RFIFO_FLUSH         (1 << 1)
    #define WFIFO_FLUSH         (1 << 0)
#define R_MCTRL_RO (~0x12)
#define R_MCTRL_RESET 0

#define XADCIF_MCTRL    (0x118 / 4)
#define R_MAX (XADCIF_MCTRL + 1)

#define RX_FIFO_LEN 32
#define TX_FIFO_LEN 128

struct XilinxDevcfgDMACommand
{
    uint32_t src_addr;
    uint32_t dest_addr;
    uint32_t src_len;
    uint32_t dest_len;
};

struct XilinxDevcfg
{
    SysBusDevice busdev;
    MemoryRegion iomem;

    qemu_irq irq;
    int irqline;

    int lock;

    QEMUBH *timer_bh;
    ptimer_state *timer;

    /* FIXME: make command qemu length a qdev prop */
    struct XilinxDevcfgDMACommand dma_command_fifo[10];
    int dma_command_fifo_num;

    uint32_t regs[R_MAX];
    uint32_t regs_ro[R_MAX];
};

static int lsb_pos32(uint32_t x) {
    int i;
    for (i = 0; i < 32; i++)
        if (x & (1 << i))
            return i;
    return -1;
}

static void update_ixr (struct XilinxDevcfg *s) {

    int old_status = s->regs[R_INT_STS];
    
    /* FIXME: come up with a policy for setting the FIFO progress interrupts
     * (when there are no fifos */

    /* drive external interupt pin */
    int new_irqline = !!(~s->regs[R_INT_MASK] & s->regs[R_INT_STS]);
    if (new_irqline != s->irqline) {
        s->irqline = new_irqline;
        qemu_set_irq(s->irq, s->irqline);
        DB_PRINT("interrupt change of state: %d isr: %02x -> %02x\n", s->irqline, old_status, s->regs[R_INT_STS]);
    }
    if (old_status != s->regs[R_INT_STS]) DB_PRINT("isr change of state: %02x -> %02x\n", old_status, s->regs[R_INT_STS]);
}

static void reset (struct XilinxDevcfg *s) {
    /* FIXME: zero out s-regs, but figure out presevations for power-on-reset bahaviour ? */
    s->regs[R_CTRL] = R_CTRL_RESET;
    s->regs_ro[R_CTRL] = R_CTRL_RO;

    s->regs_ro[R_LOCK] = R_LOCK_RO;

    s->regs[R_CFG] = R_CFG_RESET;
    s->regs_ro[R_CFG] = R_CFG_RO;

    s->regs[R_INT_STS] = R_INT_STS_RESET;
    s->regs_ro[R_INT_STS] = R_INT_STS_RO;
    
    s->regs[R_INT_MASK] = R_INT_MASK_RESET;
    
    s->regs[R_STATUS] = R_STATUS_RESET;
    s->regs_ro[R_STATUS] = R_STATUS_RO;
    
    s->regs_ro[R_DMA_SRC_LEN] = R_DMA_SRC_LEN_RO;

    s->regs_ro[R_DMA_DEST_LEN] = R_DMA_DEST_LEN_RO;
}

static uint64_t devcfg_read (void *opaque, hwaddr addr,
        unsigned size)
{
    struct XilinxDevcfg *s = opaque;
    uint32_t ret;

    addr >>= 2;
    switch (addr)
    {
        //TODO: implement any read side effects
    }
    ret = s->regs[addr];
    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr * 4, ret);
    return ret;
}

/* FIXME: QDEV prop this magic number */
#define BTT_MAX 0x400

#define min(a,b) ((a)<(b)?(a):(b))

static void xilinx_devcfg_dma_go(void *opaque) {
    struct XilinxDevcfg *s = opaque;
    uint8_t buf[BTT_MAX];
    struct XilinxDevcfgDMACommand *dmah = s->dma_command_fifo;
    uint32_t btt = BTT_MAX;

    btt = min(btt,dmah->src_len);
    if (s->regs[R_MCTRL] & INT_PCAP_LPBK) {
        btt = min(btt,dmah->dest_len);
    }
    /* TODO: implement keyhole mode */
    DB_PRINT("reading %x bytes from %x\n", btt, dmah->src_addr);
    cpu_physical_memory_rw(dmah->src_addr, buf, btt, 0);
    dmah->src_len -= btt;
    dmah->src_addr += btt;
    if (s->regs[R_MCTRL] & INT_PCAP_LPBK) {
        DB_PRINT("writing %x bytes from %x\n", btt, dmah->dest_addr);
        cpu_physical_memory_rw(dmah->dest_addr, buf, btt, 1);
        dmah->dest_len -= btt;
        dmah->dest_addr += btt;
    }
    if (!dmah->src_len && !dmah->dest_len) {
        DB_PRINT("dma operation finished\n");
        s->regs[R_INT_STS] |= DMA_DONE_INT | DMA_P_DONE_INT;
        s->dma_command_fifo_num = s->dma_command_fifo_num - 1;
    }
    update_ixr(s);
    if (s->dma_command_fifo_num) { /* there is still work to do */
        DB_PRINT("dma work remains, setting up callback ptimer\n");
        ptimer_set_freq(s->timer, FREQ_HZ);
        /* FIXME: qdev prop this bandwidth magic number */
        ptimer_set_count(s->timer, 10000);
        ptimer_run(s->timer, 1);
    }
}

static void
devcfg_write (void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    uint32_t delta;
    uint8_t aes_en;
    struct XilinxDevcfg *s = opaque;
    DB_PRINT("addr=" TARGET_FMT_plx " = %x\n", addr, (unsigned)value);
    addr >>= 2;
    if (s->lock && addr != R_UNLOCK)
        return;
    delta = value & ~s->regs_ro[addr];
    s->regs[addr] = (s->regs[addr] & s->regs_ro[addr]) | delta;
    switch (addr)
    {
        case R_CTRL:
            aes_en = s->regs[R_CTRL] & PCFG_AES_EN >> lsb_pos32(PCFG_AES_EN);
            if (aes_en != 0 && aes_en != 7) {
                DB_PRINT("warning, aes-en bits inconsistent, unimplemeneted security reset should happen!");
            }

        case R_LOCK:
            s->regs_ro[R_LOCK] |= delta; /* set only */
            if (delta & AES_FUSE_LOCK)
                s->regs_ro[R_CTRL] |= PCFG_AES_FUSE;
            if (delta & AES_EN_LOCK)
                s->regs_ro[R_CTRL] |= PCFG_AES_EN;
            if (delta & SEU_LOCK)
                s->regs_ro[R_CTRL] |= SEU_EN;
            if (delta & SEC_LOCK)
                s->regs_ro[R_CTRL] |= SEC_EN; /* TODO: investigate locking of USER_MODE bit */
            if (delta & DBG_LOCK)
                s->regs[R_CTRL] |= R_CTRL_DBG_LOCK_MASK;
            break;

        case R_INT_STS:
            if (!s->lock)
                s->regs[R_INT_STS] &= ~value; /* write to clear */
            break;

        /* TODO: add state sequence to enforce correct ordering of DMA operand writes as per TRM */
        case R_DMA_DEST_LEN:
            /* TODO: implement command queue overflow check and interrupt */
            s->dma_command_fifo[s->dma_command_fifo_num].src_addr = s->regs[R_DMA_SRC_ADDR] & ~0x3UL;
            s->dma_command_fifo[s->dma_command_fifo_num].dest_addr = s->regs[R_DMA_DST_ADDR] & ~0x3UL;
            s->dma_command_fifo[s->dma_command_fifo_num].src_len = s->regs[R_DMA_SRC_LEN] << 2;
            s->dma_command_fifo[s->dma_command_fifo_num].dest_len = s->regs[R_DMA_DEST_LEN] << 2;
            s->dma_command_fifo_num = s->dma_command_fifo_num + 1;
            DB_PRINT("dma transfer started\n");
            xilinx_devcfg_dma_go(s);

        case R_UNLOCK:
            if (value == R_UNLOCK_MAGIC) {
                s->lock = 0;
                DB_PRINT("successful unlock\n");
            } else if (s->lock) {/* bad unlcok attempt */
                DB_PRINT("failed unlock\n");
                s->regs[R_CTRL] &= ~PCAP_PR;
                s->regs[R_CTRL] &= ~PCFG_AES_EN;
            }
            break;       
    }
    update_ixr(s);
}

static const MemoryRegionOps devcfg_ops = {
    .read = devcfg_read,
    .write = devcfg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static int xilinx_devcfg_init(SysBusDevice *dev)
{
    struct XilinxDevcfg *s = FROM_SYSBUS(typeof (*s), dev);

	DB_PRINT("inited device model\n");

    s->timer_bh = qemu_bh_new(xilinx_devcfg_dma_go, s);
    s->timer = ptimer_init(s->timer_bh);

    s->irqline = -1;
    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->iomem, &devcfg_ops, s, "devcfg", R_MAX*4);
    sysbus_init_mmio(dev, &s->iomem);

    reset(s);
    return 0;
}

static void xilinx_devcfg_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = xilinx_devcfg_init;
}

static TypeInfo xilinx_devcfg_info = {
    .name           = "xlnx.ps7-dev-cfg",
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(struct XilinxDevcfg),
    .class_init     = xilinx_devcfg_class_init,
};

static void xilinx_devcfg_register_types(void)
{
    type_register_static(&xilinx_devcfg_info);
}

type_init(xilinx_devcfg_register_types)
