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
#include "sysemu/dma.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qemu/bitops.h"
#include "exec/register.h"
#include "qemu/bitops.h"

#define TYPE_XILINX_DEVCFG "xlnx.ps7-dev-cfg"

#define XILINX_DEVCFG(obj) \
    OBJECT_CHECK(XilinxDevcfg, (obj), TYPE_XILINX_DEVCFG)

/* FIXME: get rid of hardcoded nastiness */

#define FREQ_HZ 900000000

#define BTT_MAX 0x400 /* bytes to transfer per delay inteval */
#define CYCLES_BTT_MAX 10000 /*approximate 10k cycles per delay interval */

#ifndef XILINX_DEVCFG_ERR_DEBUG
#define XILINX_DEVCFG_ERR_DEBUG 1
#endif
#define DB_PRINT(...) do { \
    if (XILINX_DEVCFG_ERR_DEBUG) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

#define R_CTRL            (0x00/4)
    #define FORCE_RST            (1 << 31) /* Not supported, writes ignored */
    #define PCAP_PR              (1 << 27) /* Forced to 0 on bad unlock */
    #define PCAP_MODE            (1 << 26)
    #define MULTIBOOT_EN         (1 << 24)
    #define USER_MODE            (1 << 15)
    #define PCFG_AES_FUSE        (1 << 12) /* locked by AES_FUSE_LOCK */
    #define PCFG_AES_EN_SHIFT    9 /* locked by AES_EN_LOCK */
    #define PCFG_AES_EN_LEN      3 /* locked by AES_EN_LOCK */
    #define PCFG_AES_EN_MASK     (((1 << PCFG_AES_EN_LEN) - 1) \
                                    << PCFG_AES_EN_SHIFT)
    #define SEU_EN               (1 << 8) /* locked by SEU_LOCK */
    #define SEC_EN               (1 << 7) /* locked by SEC_LOCK */
    #define SPNIDEN              (1 << 6) /* locked by DBG_LOCK */
    #define SPIDEN               (1 << 5) /* locked by DBG_LOCK */
    #define NIDEN                (1 << 4) /* locked by DBG_LOCK */
    #define DBGEN                (1 << 3) /* locked by DBG_LOCK */
    #define DAP_EN               (7 << 0) /* locked by DBG_LOCK */

#define R_LOCK          (0x04/4)
    #define AES_FUSE_LOCK        4
    #define AES_EN_LOCK          3
    #define SEU_LOCK             2
    #define SEC_LOCK             1
    #define DBG_LOCK             0

/* mapping bits in R_LOCK to what they lock in R_CTRL */
static const uint32_t lock_ctrl_map[] = {
    [AES_FUSE_LOCK] = PCFG_AES_FUSE,
    [AES_EN_LOCK] = PCFG_AES_EN_MASK,
    [SEU_LOCK] = SEU_LOCK,
    [SEC_LOCK] = SEC_LOCK,
    [DBG_LOCK] =  SPNIDEN | SPIDEN | NIDEN | DBGEN | DAP_EN,
};

#define R_CFG           (0x08/4)
    #define RFIFO_TH_SHIFT       10
    #define RFIFO_TH_LEN         2
    #define WFIFO_TH_SHIFT       8
    #define WFIFO_TH_LEN         2
    #define DISABLE_SRC_INC      (1 << 5)
    #define DISABLE_DST_INC      (1 << 4)
#define R_CFG_RO 0xFFFFF800
#define R_CFG_RESET 0x50B

#define R_INT_STS       (0x0C/4)
    #define PSS_GTS_USR_B_INT    (1 << 31)
    #define PSS_FST_CFG_B_INT    (1 << 30)
    #define PSS_CFG_RESET_B_INT  (1 << 27)
    #define RX_FIFO_OV_INT       (1 << 18)
    #define WR_FIFO_LVL_INT      (1 << 17)
    #define RD_FIFO_LVL_INT      (1 << 16)
    #define DMA_CMD_ERR_INT      (1 << 15)
    #define DMA_Q_OV_INT         (1 << 14)
    #define DMA_DONE_INT         (1 << 13)
    #define DMA_P_DONE_INT       (1 << 12)
    #define P2D_LEN_ERR_INT      (1 << 11)
    #define PCFG_DONE_INT        (1 << 2)
    #define R_INT_STS_RSVD       ((0x7 << 24) | (0x1 << 19) | (0xF < 7))

#define R_INT_MASK      (0x10/4)

#define R_STATUS        (0x14/4)
    #define DMA_CMD_Q_F         (1 << 31)
    #define DMA_CMD_Q_E         (1 << 30)
    #define DMA_DONE_CNT_SHIFT  28
    #define DMA_DONE_CNT_LEN    2
    #define RX_FIFO_LVL_SHIFT   20
    #define RX_FIFO_LVL_LEN     5
    #define TX_FIFO_LVL_SHIFT   12
    #define TX_FIFO_LVL_LEN     7
    #define TX_FIFO_LVL         (0x7f << 12)
    #define PSS_GTS_USR_B       (1 << 11)
    #define PSS_FST_CFG_B       (1 << 10)
    #define PSS_CFG_RESET_B     (1 << 5)

#define R_DMA_SRC_ADDR  (0x18/4)
#define R_DMA_DST_ADDR  (0x1C/4)
#define R_DMA_SRC_LEN   (0x20/4)
#define R_DMA_DST_LEN   (0x24/4)
#define R_ROM_SHADOW    (0x28/4)
#define R_SW_ID         (0x30/4)
#define R_UNLOCK        (0x34/4)

#define R_UNLOCK_MAGIC 0x757BDF0D

#define R_MCTRL         (0x80/4)
    #define PS_VERSION_SHIFT    28
    #define PS_VERSION_MASK     (0xf << PS_VERSION_SHIFT)
    #define PCFG_POR_B          (1 << 8)
    #define INT_PCAP_LPBK       (1 << 4)

#define R_MAX (0x118/4+1)

#define RX_FIFO_LEN 32
#define TX_FIFO_LEN 128

#define DMA_COMMAND_FIFO_LEN 10

typedef struct XilinxDevcfgDMACommand {
    uint32_t src_addr;
    uint32_t dest_addr;
    uint32_t src_len;
    uint32_t dest_len;
} XilinxDevcfgDMACommand;

static const VMStateDescription vmstate_xilinx_devcfg_dma_command = {
    .name = "xilinx_devcfg_dma_command",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(src_addr, XilinxDevcfgDMACommand),
        VMSTATE_UINT32(dest_addr, XilinxDevcfgDMACommand),
        VMSTATE_UINT32(src_len, XilinxDevcfgDMACommand),
        VMSTATE_UINT32(dest_len, XilinxDevcfgDMACommand),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct XilinxDevcfg {
    SysBusDevice busdev;
    MemoryRegion iomem;

    qemu_irq irq;

    QEMUBH *timer_bh;
    ptimer_state *timer;

    XilinxDevcfgDMACommand dma_command_fifo[DMA_COMMAND_FIFO_LEN];
    uint8_t dma_command_fifo_num;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} XilinxDevcfg;

static const VMStateDescription vmstate_xilinx_devcfg = {
    .name = "xilinx_devcfg",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(timer, XilinxDevcfg),
        VMSTATE_STRUCT_ARRAY(dma_command_fifo, XilinxDevcfg,
                             DMA_COMMAND_FIFO_LEN, 0,
                             vmstate_xilinx_devcfg_dma_command,
                             XilinxDevcfgDMACommand),
        VMSTATE_UINT8(dma_command_fifo_num, XilinxDevcfg),
        VMSTATE_UINT32_ARRAY(regs, XilinxDevcfg, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void xilinx_devcfg_update_ixr(XilinxDevcfg *s)
{
    qemu_set_irq(s->irq, !!(~s->regs[R_INT_MASK] & s->regs[R_INT_STS]));
}

static void xilinx_devcfg_reset(DeviceState *dev)
{
    XilinxDevcfg *s = XILINX_DEVCFG(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
}

#define min(a, b) ((a) < (b) ? (a) : (b))

static void xilinx_devcfg_dma_go(void *opaque)
{
    XilinxDevcfg *s = opaque;
    uint8_t buf[BTT_MAX];
    XilinxDevcfgDMACommand *dmah = s->dma_command_fifo;
    uint32_t btt = BTT_MAX;

    btt = min(btt, dmah->src_len);
    if (s->regs[R_MCTRL] & INT_PCAP_LPBK) {
        btt = min(btt, dmah->dest_len);
    }
    DB_PRINT("reading %x bytes from %x\n", btt, dmah->src_addr);
    dma_memory_read(&dma_context_memory, dmah->src_addr, buf, btt);
    dmah->src_len -= btt;
    dmah->src_addr += btt;
    if (s->regs[R_MCTRL] & INT_PCAP_LPBK) {
        DB_PRINT("writing %x bytes from %x\n", btt, dmah->dest_addr);
        dma_memory_write(&dma_context_memory, dmah->dest_addr, buf, btt);
        dmah->dest_len -= btt;
        dmah->dest_addr += btt;
    }
    if (!dmah->src_len && !dmah->dest_len) {
        DB_PRINT("dma operation finished\n");
        s->regs[R_INT_STS] |= DMA_DONE_INT | DMA_P_DONE_INT;
        s->dma_command_fifo_num = s->dma_command_fifo_num - 1;
        memcpy(s->dma_command_fifo, &s->dma_command_fifo[1],
               sizeof(*s->dma_command_fifo) * DMA_COMMAND_FIFO_LEN - 1);
    }
    xilinx_devcfg_update_ixr(s);
    if (s->dma_command_fifo_num) { /* there is still work to do */
        DB_PRINT("dma work remains, setting up callback ptimer\n");
        ptimer_set_freq(s->timer, FREQ_HZ);
        ptimer_set_count(s->timer, CYCLES_BTT_MAX);
        ptimer_run(s->timer, 1);
    }
}

static void r_ixr_post_write(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);

    xilinx_devcfg_update_ixr(s);
}

static uint64_t r_ctrl_prewrite(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);
    int i;

    for (i = 0; i < ARRAY_SIZE(lock_ctrl_map); ++i) {
        if (s->regs[R_LOCK] & 1 << i) {
            val &= ~lock_ctrl_map[i];
            val |= lock_ctrl_map[i] & s->regs[R_CTRL];
        }
    }
    return val;
}

static void r_ctrl_post_write(RegisterInfo *reg, uint64_t val)
{
    uint32_t aes_en = extract32(val, PCFG_AES_EN_SHIFT, PCFG_AES_EN_LEN);

    if (aes_en != 0 && aes_en != 7) {
        qemu_log_mask(LOG_UNIMP, "%s: warning, aes-en bits inconsistent,"
                      "unimplemeneted security reset should happen!\n",
                      reg->prefix);
    }
}

static void r_unlock_post_write(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);

    if (val == R_UNLOCK_MAGIC) {
        DB_PRINT("successful unlock\n");
    } else {/* bad unlock attempt */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed unlock\n", reg->prefix);
        qemu_log_mask(LOG_UNIMP, "%s: failed unlock, unimplmeneted crash of"
                      "device should happen\n", reg->prefix);
        s->regs[R_CTRL] &= ~PCAP_PR;
        s->regs[R_CTRL] &= ~PCFG_AES_EN_MASK;
    }
}

static void r_dma_dst_len_post_write(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);

    /* TODO: implement command queue overflow check and interrupt */
    s->dma_command_fifo[s->dma_command_fifo_num].src_addr =
            s->regs[R_DMA_SRC_ADDR] & ~0x3UL;
    s->dma_command_fifo[s->dma_command_fifo_num].dest_addr =
            s->regs[R_DMA_DST_ADDR] & ~0x3UL;
    s->dma_command_fifo[s->dma_command_fifo_num].src_len =
            s->regs[R_DMA_SRC_LEN] << 2;
    s->dma_command_fifo[s->dma_command_fifo_num].dest_len =
            s->regs[R_DMA_DST_LEN] << 2;
    s->dma_command_fifo_num++;
    DB_PRINT("dma transfer started; %d total transfers pending\n",
             s->dma_command_fifo_num);
    xilinx_devcfg_dma_go(s);
}

static const RegisterAccessInfo xilinx_devcfg_regs_info[] = {
    [R_CTRL] = { .name = "CTRL",
        .reset = PCAP_PR | PCAP_MODE | 0x3 << 13,
        /* flag this wo bit as ro to handle it separately */
        .ro = 0x107f6000,
        .wo = USER_MODE | 1 << 15,
        .ge1 = (RegisterAccessError[]) {
            { .mask = 0x1 << 15, .reason = "Reserved - do not modify" },
            {},
        },
        .ge0 = (RegisterAccessError[]) {
            { .mask = 0x3 << 13, .reason = "Reserved - always write with 1" },
            {},
        },
        .ui1 = (RegisterAccessError[]) {
            { .mask = FORCE_RST, .reason = "PS reset not implemented" },
            { .mask = PCAP_MODE, .reason = "FPGA Fabric doesnt exist" },
            { .mask = PCFG_AES_EN_MASK, .reason = "AES not implmented" },
            {},
        },
        .pre_write = r_ctrl_prewrite,
        .post_write = r_ctrl_post_write,
    },
    [R_LOCK] = { .name = "LOCK", .ro = ~ONES(5), .nw0 = ~0 },
    [R_CFG] = { .name = "CFG",
        .reset = 1 << RFIFO_TH_SHIFT | 1 << WFIFO_TH_SHIFT | 0x8,
        .ge1 = (RegisterAccessError[]) {
            { .mask = 0x7, .reason = "Reserved - do not modify" },
            {},
        },
        .ge0 = (RegisterAccessError[]) {
            { .mask = 0x8, .reason = "Reserved - do not modify" },
            {},
        },
        .ro = 0x00f | ~ONES(12),
    },
    [R_INT_STS] = { .name = "INT_STS",
        .w1c = ~R_INT_STS_RSVD,
        .reset = PSS_GTS_USR_B_INT | PSS_CFG_RESET_B_INT | WR_FIFO_LVL_INT,
        .ro = R_INT_STS_RSVD,
        .post_write = r_ixr_post_write,
    },
    [R_INT_MASK] = { .name = "INT_MASK",
        .reset = ~0,
        .ro = R_INT_STS_RSVD,
        .post_write = r_ixr_post_write,
    },
    [R_STATUS] = { .name = "STATUS",
        .reset = DMA_CMD_Q_E | PSS_GTS_USR_B | PSS_CFG_RESET_B,
        .ro = ~0,
        .ge1 = (RegisterAccessError[])  {
            {.mask = 0x1, .reason = "Reserved - do not modify" },
            {},
        },
    },
    [R_DMA_SRC_ADDR] = { .name = "DMA_SRC_ADDR" },
    [R_DMA_DST_ADDR] = { .name = "DMA_DST_ADDR" },
    [R_DMA_SRC_LEN] = { .name = "DMA_SRC_LEN", .ro = ~ONES(27) },
    [R_DMA_DST_LEN] = { .name = "DMA_DST_LEN",
        .ro = ~ONES(27),
        .post_write = r_dma_dst_len_post_write,
    },
    [R_ROM_SHADOW] = { .name = "ROM_SHADOW",
        .ge1 = (RegisterAccessError[])  {
            {.mask = ~0, .reason = "Reserved - do not modify" },
            {},
        },
    },
    [R_SW_ID] = { .name = "SW_ID" },
    [R_UNLOCK] = { .name = "UNLOCK",
        .post_write = r_unlock_post_write,
    },
    [R_MCTRL] = { .name = "MCTRL",
        /* Silicon 3.0 for version field, and the mysterious reserved bit 23 */
        .reset = 0x2 << PS_VERSION_SHIFT | 1 << 23,
        /* some reserved bits are rw while others are ro */
        .ro = ~INT_PCAP_LPBK,
        .ge1 = (RegisterAccessError[]) {
            { .mask = 0x00F00300, .reason = "Reserved - do not modify" },
            { .mask = 0x00000003, .reason = "Reserved - always write with 0" },
            {}
        },
        .ge0 = (RegisterAccessError[]) {
            { .mask = 1 << 23, .reason = "Reserved - always write with 1" },
            {}
        },
    },
    [R_MAX] = {}
};

static const MemoryRegionOps devcfg_reg_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void xilinx_devcfg_realize(DeviceState *dev, Error **errp)
{
    XilinxDevcfg *s = XILINX_DEVCFG(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < R_MAX; ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[i],
            .data_size = sizeof(uint32_t),
#if defined(HOST_WORDS_BIGENDIAN)
            .data_big_endian = true,
#endif
            .access = &xilinx_devcfg_regs_info[i],
            .debug = XILINX_DEVCFG_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, &devcfg_reg_ops, r, "devcfg-regs", 4);
        memory_region_add_subregion(&s->iomem, i * 4, &r->mem);
    }
}

static void xilinx_devcfg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XilinxDevcfg *s = XILINX_DEVCFG(obj);

    s->timer_bh = qemu_bh_new(xilinx_devcfg_dma_go, s);
    s->timer = ptimer_init(s->timer_bh);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init(&s->iomem, "devcfg", R_MAX*4);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void xilinx_devcfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xilinx_devcfg_reset;
    dc->vmsd = &vmstate_xilinx_devcfg;
    dc->realize = xilinx_devcfg_realize;
}

static const TypeInfo xilinx_devcfg_info = {
    .name           = TYPE_XILINX_DEVCFG,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(XilinxDevcfg),
    .instance_init  = xilinx_devcfg_init,
    .class_init     = xilinx_devcfg_class_init,
};

static void xilinx_devcfg_register_types(void)
{
    type_register_static(&xilinx_devcfg_info);
}

type_init(xilinx_devcfg_register_types)
