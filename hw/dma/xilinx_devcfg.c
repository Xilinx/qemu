/*
 * QEMU model of the Xilinx Devcfg Interface
 *
 * (C) 2011 PetaLogix Pty Ltd
 * (C) 2014-2020 Xilinx Inc.
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "hw/register.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/bitops.h"

#define TYPE_XILINX_DEVCFG "xlnx.ps7-dev-cfg"

#define XILINX_DEVCFG(obj) \
    OBJECT_CHECK(XilinxDevcfg, (obj), TYPE_XILINX_DEVCFG)

/* FIXME: get rid of hardcoded nastiness */

#define FREQ_HZ 900000000

#define BTT_MAX 0x400 /* bytes to transfer per delay inteval */

#ifndef XILINX_DEVCFG_ERR_DEBUG
#define XILINX_DEVCFG_ERR_DEBUG 0
#endif
#define DB_PRINT(...) do { \
    if (XILINX_DEVCFG_ERR_DEBUG) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

REG32(CTRL, 0x00)
    FIELD(CTRL,     FORCE_RST,          31,  1) /* Not supported, wr ignored */
    FIELD(CTRL,     PCAP_PR,            27,  1) /* Forced to 0 on bad unlock */
    FIELD(CTRL,     PCAP_MODE,          26,  1)
    FIELD(CTRL,     MULTIBOOT_EN,       24,  1)
    FIELD(CTRL,     USER_MODE,          15,  1)
    FIELD(CTRL,     PCFG_AES_FUSE,      12,  1)
    FIELD(CTRL,     PCFG_AES_EN,         9,  3)
    FIELD(CTRL,     SEU_EN,              8,  1)
    FIELD(CTRL,     SEC_EN,              7,  1)
    FIELD(CTRL,     SPNIDEN,             6,  1)
    FIELD(CTRL,     SPIDEN,              5,  1)
    FIELD(CTRL,     NIDEN,               4,  1)
    FIELD(CTRL,     DBGEN,               3,  1)
    FIELD(CTRL,     DAP_EN,              0,  3)

REG32(LOCK, 0x04)
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

REG32(CFG, 0x08)
    FIELD(CFG,      RFIFO_TH,           10,  2)
    FIELD(CFG,      WFIFO_TH,            8,  2)
    FIELD(CFG,      RCLK_EDGE,           7,  1)
    FIELD(CFG,      WCLK_EDGE,           6,  1)
    FIELD(CFG,      DISABLE_SRC_INC,     5,  1)
    FIELD(CFG,      DISABLE_DST_INC,     4,  1)
#define R_CFG_RO 0xFFFFF800
#define R_CFG_RESET 0x50B

REG32(INT_STS, 0x0C)
    FIELD(INT_STS,  PSS_GTS_USR_B,      31,  1)
    FIELD(INT_STS,  PSS_FST_CFG_B,      30,  1)
    FIELD(INT_STS,  PSS_CFG_RESET_B,    27,  1)
    FIELD(INT_STS,  RX_FIFO_OV,         18,  1)
    FIELD(INT_STS,  WR_FIFO_LVL,        17,  1)
    FIELD(INT_STS,  RD_FIFO_LVL,        16,  1)
    FIELD(INT_STS,  DMA_CMD_ERR,        15,  1)
    FIELD(INT_STS,  DMA_Q_OV,           14,  1)
    FIELD(INT_STS,  DMA_DONE,           13,  1)
    FIELD(INT_STS,  DMA_P_DONE,         12,  1)
    FIELD(INT_STS,  P2D_LEN_ERR,        11,  1)
    FIELD(INT_STS,  PCFG_DONE,           2,  1)
#define R_INT_STS_RSVD       ((0x7 << 24) | (0x1 << 19) | (0xF < 7))

REG32(INT_MASK, 0x10)

REG32(STATUS, 0x14)
    FIELD(STATUS,   DMA_CMD_Q_F,        31,  1)
    FIELD(STATUS,   DMA_CMD_Q_E,        30,  1)
    FIELD(STATUS,   DMA_DONE_CNT,       28,  2)
    FIELD(STATUS,   RX_FIFO_LVL,        20,  5)
    FIELD(STATUS,   TX_FIFO_LVL,        12,  7)
    FIELD(STATUS,   PSS_GTS_USR_B,      11,  1)
    FIELD(STATUS,   PSS_FST_CFG_B,      10,  1)
    FIELD(STATUS,   PSS_CFG_RESET_B,     5,  1)

REG32(DMA_SRC_ADDR, 0x18)
REG32(DMA_DST_ADDR, 0x1C)
REG32(DMA_SRC_LEN, 0x20)
REG32(DMA_DST_LEN, 0x24)
REG32(ROM_SHADOW, 0x28)
REG32(SW_ID, 0x30)
REG32(UNLOCK, 0x34)

#define R_UNLOCK_MAGIC 0x757BDF0D

REG32(MCTRL, 0x80)
    FIELD(MCTRL,    PS_VERSION,         28,  4)
    FIELD(MCTRL,    PCFG_POR_B,          8,  1)
    FIELD(MCTRL,    INT_PCAP_LPBK,       4,  1)
    FIELD(MCTRL,    QEMU,                3,  1)

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
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace *dma_as;
    qemu_irq irq;

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

static void xilinx_devcfg_dma_go(XilinxDevcfg *s)
{
    for (;;) {
        uint8_t buf[BTT_MAX];
        XilinxDevcfgDMACommand *dmah = s->dma_command_fifo;
        uint32_t btt = BTT_MAX;

        btt = MIN(btt, dmah->src_len);
        if (s->regs[R_MCTRL] & INT_PCAP_LPBK) {
            btt = MIN(btt, dmah->dest_len);
        }
        DB_PRINT("reading %x bytes from %x\n", btt, dmah->src_addr);
        dma_memory_read(s->dma_as, dmah->src_addr, buf, btt);
        dmah->src_len -= btt;
        dmah->src_addr += btt;
        if (s->regs[R_MCTRL] & INT_PCAP_LPBK) {
            DB_PRINT("writing %x bytes from %x\n", btt, dmah->dest_addr);
            dma_memory_write(s->dma_as, dmah->dest_addr, buf, btt);
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
        if (!s->dma_command_fifo_num) { /* there is still work to do */
            return;
        }
    }
}

static void r_ixr_post_write(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);

    xilinx_devcfg_update_ixr(s);
}

static uint64_t r_ctrl_pre_write(RegisterInfo *reg, uint64_t val)
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
    } else { /* bad unlock attempt */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: failed unlock\n", reg->prefix);
        s->regs[R_CTRL] &= ~PCAP_PR;
        s->regs[R_CTRL] &= ~PCFG_AES_EN_MASK;
    }
}

static uint64_t r_lock_pre_write(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);

    /* once bits are locked they stay locked */
    return s->regs[R_LOCK] | val;
}

static void r_dma_dst_len_post_write(RegisterInfo *reg, uint64_t val)
{
    XilinxDevcfg *s = XILINX_DEVCFG(reg->opaque);

    s->dma_command_fifo[s->dma_command_fifo_num] = (XilinxDevcfgDMACommand) {
            .src_addr = s->regs[R_DMA_SRC_ADDR] & ~0x3UL,
            .dest_addr = s->regs[R_DMA_DST_ADDR] & ~0x3UL,
            .src_len = s->regs[R_DMA_SRC_LEN] << 2,
            .dest_len = s->regs[R_DMA_DST_LEN] << 2,
    };
    s->dma_command_fifo_num++;
    DB_PRINT("dma transfer started; %d total transfers pending\n",
             s->dma_command_fifo_num);
    xilinx_devcfg_dma_go(s);
}

static const RegisterAccessInfo xilinx_devcfg_regs_info[] = {
    {   .name = "CTRL",                 .addr = R_CTRL * 4,
        .reset = PCAP_PR | PCAP_MODE | 0x3 << 13,
        .ro = 0x107f6000,
        .rsvd = 0x1 << 15 | 0x3 << 13,
        .ui1 = (RegisterAccessError[]) {
            { .mask = FORCE_RST, .reason = "PS reset not implemented" },
            { .mask = PCAP_MODE, .reason = "FPGA Fabric doesnt exist" },
            { .mask = PCFG_AES_EN_MASK, .reason = "AES not implmented" },
            {},
        },
        .pre_write = r_ctrl_pre_write,
        .post_write = r_ctrl_post_write,
    },
    {   .name = "LOCK",                 .addr = R_LOCK * 4,
        .ro = ~ONES(5),
        .pre_write = r_lock_pre_write,
    },
    {   .name = "CFG",                  .addr = R_CFG * 4,
        .reset = 1 << RFIFO_TH_SHIFT | 1 << WFIFO_TH_SHIFT | 0x8,
        .rsvd = 0xf,
        .ro = 0x00f | ~ONES(12),
    },
    {   .name = "INT_STS",              .addr = R_INT_STS * 4,
        .w1c = ~R_INT_STS_RSVD,
        .reset = PSS_GTS_USR_B_INT | PSS_CFG_RESET_B_INT | WR_FIFO_LVL_INT,
        .ro = R_INT_STS_RSVD,
        .post_write = r_ixr_post_write,
    },
    {    .name = "INT_MASK",            .addr = R_INT_MASK * 4,
        .reset = ~0,
        .ro = R_INT_STS_RSVD,
        .post_write = r_ixr_post_write,
    },
    {   .name = "STATUS",               .addr = R_STATUS * 4,
        .reset = DMA_CMD_Q_E | PSS_GTS_USR_B | PSS_CFG_RESET_B,
        .ro = ~0,
    },
    {   .name = "DMA_SRC_ADDR",         .addr = R_DMA_SRC_ADDR * 4, },
    {   .name = "DMA_DST_ADDR",         .addr = R_DMA_DST_ADDR * 4, },
    {   .name = "DMA_SRC_LEN",          .addr = R_DMA_SRC_LEN * 4,
        .ro = ~ONES(27) },
    {   .name = "DMA_DST_LEN",          .addr = R_DMA_DST_LEN * 4,
        .ro = ~ONES(27),
        .post_write = r_dma_dst_len_post_write,
    },
    {   .name = "ROM_SHADOW",           .addr = R_ROM_SHADOW * 4,
        .rsvd = ~0ull,
    },
    {   .name = "SW_ID",                .addr = R_SW_ID * 4, },
    {   .name = "UNLOCK",               .addr = R_UNLOCK * 4,
        .post_write = r_unlock_post_write,
    },
    {   .name = "MCTRL",                .addr = R_MCTRL * 4,
       /* Silicon 3.0 for version field, and the mysterious reserved bit 23 */
       .reset = 0x2 << PS_VERSION_SHIFT | 1 << 23 | MCTRL_QEMU,
       /* some reserved bits are rw while others are ro */
       .ro = ~INT_PCAP_LPBK,
       .rsvd = 0x00f00303,
    },
};

static const MemoryRegionOps devcfg_reg_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void xilinx_devcfg_realize(DeviceState *dev, Error **errp)
{
    XilinxDevcfg *s = XILINX_DEVCFG(dev);

    if (s->dma_mr) {
        s->dma_as = g_malloc0(sizeof *as);
        address_space_init_shareable(s->dma_as, s->dma_mr, NULL)
    } else {
        s->dma_as = &address_space_memory;
    }
}

static void xilinx_devcfg_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XilinxDevcfg *s = XILINX_DEVCFG(obj);
    RegisterInfoArray *reg_array;

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init(&s->iomem, obj, "devcfg", R_MAX*4);
    reg_array =
        register_init_block32(DEVICE(obj), xilinx_devcfg_regs_info,
                              ARRAY_SIZE(xilinx_devcfg_regs_info),
                              s->regs_info, s->regs,
                              &devcfg_reg_ops,
                              XILINX_ZDMA_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);

    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE);
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
