/*
 * QEMU model of the DDRMC_XMPU MC that supports DDR4 and LPDDR4
 *
 * Copyright (c) 2020 Xilinx Inc.
 *
 * Autogenerated by xregqemu.py 2020-02-07.
 * Written by Joe Komlodi <komlodi@xilinx.com>
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
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "sysemu/dma.h"
#include "exec/address-spaces.h"
#include "hw/fdt_generic_util.h"

#include "hw/misc/xlnx-xmpu.h"

#define TYPE_XILINX_DDRMC_XMPU "xlnx,versal-ddrmc-xmpu"
#define TYPE_XILINX_XMPU_IOMMU_MEMORY_REGION \
        "xlnx,versal-ddrmc-xmpu-iommu-memory-region"

#define XILINX_DDRMC_XMPU(obj) \
     OBJECT_CHECK(XMPU, (obj), TYPE_XILINX_DDRMC_XMPU)

/*
 * Register definitions shared between ZynqMP and Versal are in
 * the XMPU header file
 */
REG32(ERR_STATUS, 0x4)
    FIELD(ERR_STATUS, REGIONVIO, 4, 5)
    FIELD(ERR_STATUS, SECURITYVIO, 3, 1)
    FIELD(ERR_STATUS, WRPERMVIO, 2, 1)
    FIELD(ERR_STATUS, RDPERMVIO, 1, 1)
REG32(ERR_ADD_LO, 0x8)
REG32(ERR_ADD_HI, 0xc)
    FIELD(ERR_ADD_HI, ERR_ADD_HI, 0, 16)
REG32(ERR_AXI_ID, 0x10)
    FIELD(ERR_AXI_ID, ERR_SMID, 0, 10)
REG32(R00_START_LO, 0x100)
    FIELD(R00_START_LO, ADDR_LO, 12, 20)
REG32(R00_START_HI, 0x104)
    FIELD(R00_START_HI, ADDR_HI, 0, 16)
REG32(R00_END_LO, 0x108)
    FIELD(R00_END_LO, ADDR_LO, 12, 20)
REG32(R00_END_HI, 0x10c)
    FIELD(R00_END_HI, ADDR_HI, 0, 16)
REG32(R00_MASTER, 0x110)
    FIELD(R00_MASTER, MASK, 16, 10)
    FIELD(R00_MASTER, ID, 0, 10)
REG32(R00_CONFIG, 0x114)
    FIELD(R00_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R00_CONFIG, REGIONNS, 3, 1)
    FIELD(R00_CONFIG, WRALLOWED, 2, 1)
    FIELD(R00_CONFIG, RDALLOWED, 1, 1)
    FIELD(R00_CONFIG, ENABLE, 0, 1)
REG32(R01_START_LO, 0x118)
    FIELD(R01_START_LO, ADDR_LO, 12, 20)
REG32(R01_START_HI, 0x11c)
    FIELD(R01_START_HI, ADDR_HI, 0, 16)
REG32(R01_END_LO, 0x120)
    FIELD(R01_END_LO, ADDR_LO, 12, 20)
REG32(R01_END_HI, 0x124)
    FIELD(R01_END_HI, ADDR_HI, 0, 16)
REG32(R01_MASTER, 0x128)
    FIELD(R01_MASTER, MASK, 16, 10)
    FIELD(R01_MASTER, ID, 0, 10)
REG32(R01_CONFIG, 0x12c)
    FIELD(R01_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R01_CONFIG, REGIONNS, 3, 1)
    FIELD(R01_CONFIG, WRALLOWED, 2, 1)
    FIELD(R01_CONFIG, RDALLOWED, 1, 1)
    FIELD(R01_CONFIG, ENABLE, 0, 1)
REG32(R02_START_LO, 0x130)
    FIELD(R02_START_LO, ADDR_LO, 12, 20)
REG32(R02_START_HI, 0x134)
    FIELD(R02_START_HI, ADDR_HI, 0, 16)
REG32(R02_END_LO, 0x138)
    FIELD(R02_END_LO, ADDR_LO, 12, 20)
REG32(R02_END_HI, 0x13c)
    FIELD(R02_END_HI, ADDR_HI, 0, 16)
REG32(R02_MASTER, 0x140)
    FIELD(R02_MASTER, MASK, 16, 10)
    FIELD(R02_MASTER, ID, 0, 10)
REG32(R02_CONFIG, 0x144)
    FIELD(R02_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R02_CONFIG, REGIONNS, 3, 1)
    FIELD(R02_CONFIG, WRALLOWED, 2, 1)
    FIELD(R02_CONFIG, RDALLOWED, 1, 1)
    FIELD(R02_CONFIG, ENABLE, 0, 1)
REG32(R03_START_LO, 0x148)
    FIELD(R03_START_LO, ADDR_LO, 12, 20)
REG32(R03_START_HI, 0x14c)
    FIELD(R03_START_HI, ADDR_HI, 0, 16)
REG32(R03_END_LO, 0x150)
    FIELD(R03_END_LO, ADDR_LO, 12, 20)
REG32(R03_END_HI, 0x154)
    FIELD(R03_END_HI, ADDR_HI, 0, 16)
REG32(R03_MASTER, 0x158)
    FIELD(R03_MASTER, MASK, 16, 10)
    FIELD(R03_MASTER, ID, 0, 10)
REG32(R03_CONFIG, 0x15c)
    FIELD(R03_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R03_CONFIG, REGIONNS, 3, 1)
    FIELD(R03_CONFIG, WRALLOWED, 2, 1)
    FIELD(R03_CONFIG, RDALLOWED, 1, 1)
    FIELD(R03_CONFIG, ENABLE, 0, 1)
REG32(R04_START_LO, 0x160)
    FIELD(R04_START_LO, ADDR_LO, 12, 20)
REG32(R04_START_HI, 0x164)
    FIELD(R04_START_HI, ADDR_HI, 0, 16)
REG32(R04_END_LO, 0x168)
    FIELD(R04_END_LO, ADDR_LO, 12, 20)
REG32(R04_END_HI, 0x16c)
    FIELD(R04_END_HI, ADDR_HI, 0, 16)
REG32(R04_MASTER, 0x170)
    FIELD(R04_MASTER, MASK, 16, 10)
    FIELD(R04_MASTER, ID, 0, 10)
REG32(R04_CONFIG, 0x174)
    FIELD(R04_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R04_CONFIG, REGIONNS, 3, 1)
    FIELD(R04_CONFIG, WRALLOWED, 2, 1)
    FIELD(R04_CONFIG, RDALLOWED, 1, 1)
    FIELD(R04_CONFIG, ENABLE, 0, 1)
REG32(R05_START_LO, 0x178)
    FIELD(R05_START_LO, ADDR_LO, 12, 20)
REG32(R05_START_HI, 0x17c)
    FIELD(R05_START_HI, ADDR_HI, 0, 16)
REG32(R05_END_LO, 0x180)
    FIELD(R05_END_LO, ADDR_LO, 12, 20)
REG32(R05_END_HI, 0x184)
    FIELD(R05_END_HI, ADDR_HI, 0, 16)
REG32(R05_MASTER, 0x188)
    FIELD(R05_MASTER, MASK, 16, 10)
    FIELD(R05_MASTER, ID, 0, 10)
REG32(R05_CONFIG, 0x18c)
    FIELD(R05_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R05_CONFIG, REGIONNS, 3, 1)
    FIELD(R05_CONFIG, WRALLOWED, 2, 1)
    FIELD(R05_CONFIG, RDALLOWED, 1, 1)
    FIELD(R05_CONFIG, ENABLE, 0, 1)
REG32(R06_START_LO, 0x190)
    FIELD(R06_START_LO, ADDR_LO, 12, 20)
REG32(R06_START_HI, 0x194)
    FIELD(R06_START_HI, ADDR_HI, 0, 16)
REG32(R06_END_LO, 0x198)
    FIELD(R06_END_LO, ADDR_LO, 12, 20)
REG32(R06_END_HI, 0x19c)
    FIELD(R06_END_HI, ADDR_HI, 0, 16)
REG32(R06_MASTER, 0x1a0)
    FIELD(R06_MASTER, MASK, 16, 10)
    FIELD(R06_MASTER, ID, 0, 10)
REG32(R06_CONFIG, 0x1a4)
    FIELD(R06_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R06_CONFIG, REGIONNS, 3, 1)
    FIELD(R06_CONFIG, WRALLOWED, 2, 1)
    FIELD(R06_CONFIG, RDALLOWED, 1, 1)
    FIELD(R06_CONFIG, ENABLE, 0, 1)
REG32(R07_START_LO, 0x1a8)
    FIELD(R07_START_LO, ADDR_LO, 12, 20)
REG32(R07_START_HI, 0x1ac)
    FIELD(R07_START_HI, ADDR_HI, 0, 16)
REG32(R07_END_LO, 0x1b0)
    FIELD(R07_END_LO, ADDR_LO, 12, 20)
REG32(R07_END_HI, 0x1b4)
    FIELD(R07_END_HI, ADDR_HI, 0, 16)
REG32(R07_MASTER, 0x1b8)
    FIELD(R07_MASTER, MASK, 16, 10)
    FIELD(R07_MASTER, ID, 0, 10)
REG32(R07_CONFIG, 0x1bc)
    FIELD(R07_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R07_CONFIG, REGIONNS, 3, 1)
    FIELD(R07_CONFIG, WRALLOWED, 2, 1)
    FIELD(R07_CONFIG, RDALLOWED, 1, 1)
    FIELD(R07_CONFIG, ENABLE, 0, 1)
REG32(R08_START_LO, 0x1c0)
    FIELD(R08_START_LO, ADDR_LO, 12, 20)
REG32(R08_START_HI, 0x1c4)
    FIELD(R08_START_HI, ADDR_HI, 0, 16)
REG32(R08_END_LO, 0x1c8)
    FIELD(R08_END_LO, ADDR_LO, 12, 20)
REG32(R08_END_HI, 0x1cc)
    FIELD(R08_END_HI, ADDR_HI, 0, 16)
REG32(R08_MASTER, 0x1d0)
    FIELD(R08_MASTER, MASK, 16, 10)
    FIELD(R08_MASTER, ID, 0, 10)
REG32(R08_CONFIG, 0x1d4)
    FIELD(R08_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R08_CONFIG, REGIONNS, 3, 1)
    FIELD(R08_CONFIG, WRALLOWED, 2, 1)
    FIELD(R08_CONFIG, RDALLOWED, 1, 1)
    FIELD(R08_CONFIG, ENABLE, 0, 1)
REG32(R09_START_LO, 0x1d8)
    FIELD(R09_START_LO, ADDR_LO, 12, 20)
REG32(R09_START_HI, 0x1dc)
    FIELD(R09_START_HI, ADDR_HI, 0, 16)
REG32(R09_END_LO, 0x1e0)
   FIELD(R09_END_LO, ADDR_LO, 12, 20)
REG32(R09_END_HI, 0x1e4)
    FIELD(R09_END_HI, ADDR_HI, 0, 16)
REG32(R09_MASTER, 0x1e8)
    FIELD(R09_MASTER, MASK, 16, 10)
    FIELD(R09_MASTER, ID, 0, 10)
REG32(R09_CONFIG, 0x1ec)
    FIELD(R09_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R09_CONFIG, REGIONNS, 3, 1)
    FIELD(R09_CONFIG, WRALLOWED, 2, 1)
    FIELD(R09_CONFIG, RDALLOWED, 1, 1)
    FIELD(R09_CONFIG, ENABLE, 0, 1)
REG32(R10_START_LO, 0x1f0)
    FIELD(R10_START_LO, ADDR_LO, 12, 20)
REG32(R10_START_HI, 0x1f4)
    FIELD(R10_START_HI, ADDR_HI, 0, 16)
REG32(R10_END_LO, 0x1f8)
    FIELD(R10_END_LO, ADDR_LO, 12, 20)
REG32(R10_END_HI, 0x1fc)
    FIELD(R10_END_HI, ADDR_HI, 0, 16)
REG32(R10_MASTER, 0x200)
    FIELD(R10_MASTER, MASK, 16, 10)
    FIELD(R10_MASTER, ID, 0, 10)
REG32(R10_CONFIG, 0x204)
    FIELD(R10_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R10_CONFIG, REGIONNS, 3, 1)
    FIELD(R10_CONFIG, WRALLOWED, 2, 1)
    FIELD(R10_CONFIG, RDALLOWED, 1, 1)
    FIELD(R10_CONFIG, ENABLE, 0, 1)
REG32(R11_START_LO, 0x208)
    FIELD(R11_START_LO, ADDR_LO, 12, 20)
REG32(R11_START_HI, 0x20c)
    FIELD(R11_START_HI, ADDR_HI, 0, 16)
REG32(R11_END_LO, 0x210)
    FIELD(R11_END_LO, ADDR_LO, 12, 20)
REG32(R11_END_HI, 0x214)
    FIELD(R11_END_HI, ADDR_HI, 0, 16)
REG32(R11_MASTER, 0x218)
    FIELD(R11_MASTER, MASK, 16, 10)
    FIELD(R11_MASTER, ID, 0, 10)
REG32(R11_CONFIG, 0x21c)
    FIELD(R11_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R11_CONFIG, REGIONNS, 3, 1)
    FIELD(R11_CONFIG, WRALLOWED, 2, 1)
    FIELD(R11_CONFIG, RDALLOWED, 1, 1)
    FIELD(R11_CONFIG, ENABLE, 0, 1)
REG32(R12_START_LO, 0x220)
    FIELD(R12_START_LO, ADDR_LO, 12, 20)
REG32(R12_START_HI, 0x224)
    FIELD(R12_START_HI, ADDR_HI, 0, 16)
REG32(R12_END_LO, 0x228)
    FIELD(R12_END_LO, ADDR_LO, 12, 20)
REG32(R12_END_HI, 0x22c)
    FIELD(R12_END_HI, ADDR_HI, 0, 16)
REG32(R12_MASTER, 0x230)
    FIELD(R12_MASTER, MASK, 16, 10)
    FIELD(R12_MASTER, ID, 0, 10)
REG32(R12_CONFIG, 0x234)
    FIELD(R12_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R12_CONFIG, REGIONNS, 3, 1)
    FIELD(R12_CONFIG, WRALLOWED, 2, 1)
    FIELD(R12_CONFIG, RDALLOWED, 1, 1)
    FIELD(R12_CONFIG, ENABLE, 0, 1)
REG32(R13_START_LO, 0x238)
    FIELD(R13_START_LO, ADDR_LO, 12, 20)
REG32(R13_START_HI, 0x23c)
    FIELD(R13_START_HI, ADDR_HI, 0, 16)
REG32(R13_END_LO, 0x240)
    FIELD(R13_END_LO, ADDR_LO, 12, 20)
REG32(R13_END_HI, 0x244)
    FIELD(R13_END_HI, ADDR_HI, 0, 16)
REG32(R13_MASTER, 0x248)
    FIELD(R13_MASTER, MASK, 16, 10)
    FIELD(R13_MASTER, ID, 0, 10)
REG32(R13_CONFIG, 0x24c)
    FIELD(R13_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R13_CONFIG, REGIONNS, 3, 1)
    FIELD(R13_CONFIG, WRALLOWED, 2, 1)
    FIELD(R13_CONFIG, RDALLOWED, 1, 1)
    FIELD(R13_CONFIG, ENABLE, 0, 1)
REG32(R14_START_LO, 0x250)
    FIELD(R14_START_LO, ADDR_LO, 12, 20)
REG32(R14_START_HI, 0x254)
    FIELD(R14_START_HI, ADDR_HI, 0, 16)
REG32(R14_END_LO, 0x258)
    FIELD(R14_END_LO, ADDR_LO, 12, 20)
REG32(R14_END_HI, 0x25c)
    FIELD(R14_END_HI, ADDR_HI, 0, 16)
REG32(R14_MASTER, 0x260)
    FIELD(R14_MASTER, MASK, 16, 10)
    FIELD(R14_MASTER, ID, 0, 10)
REG32(R14_CONFIG, 0x264)
    FIELD(R14_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R14_CONFIG, REGIONNS, 3, 1)
    FIELD(R14_CONFIG, WRALLOWED, 2, 1)
    FIELD(R14_CONFIG, RDALLOWED, 1, 1)
    FIELD(R14_CONFIG, ENABLE, 0, 1)
REG32(R15_START_LO, 0x268)
    FIELD(R15_START_LO, ADDR_LO, 12, 20)
REG32(R15_START_HI, 0x26c)
    FIELD(R15_START_HI, ADDR_HI, 0, 16)
REG32(R15_END_LO, 0x270)
    FIELD(R15_END_LO, ADDR_LO, 12, 20)
REG32(R15_END_HI, 0x274)
    FIELD(R15_END_HI, ADDR_HI, 0, 16)
REG32(R15_MASTER, 0x278)
    FIELD(R15_MASTER, MASK, 16, 10)
    FIELD(R15_MASTER, ID, 0, 10)
REG32(R15_CONFIG, 0x27c)
    FIELD(R15_CONFIG, NSCHECKTYPE, 4, 1)
    FIELD(R15_CONFIG, REGIONNS, 3, 1)
    FIELD(R15_CONFIG, WRALLOWED, 2, 1)
    FIELD(R15_CONFIG, RDALLOWED, 1, 1)
    FIELD(R15_CONFIG, ENABLE, 0, 1)

#define XMPU_R_MAX (R_R15_CONFIG + 1)

#define DEFAULT_ADDR_SHIFT 20

static bool xmpu_match(XMPU *s, XMPURegion *xr, uint16_t master_id, hwaddr addr)
{
    bool id_match;

    if (xr->start & s->addr_mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad region start address %" PRIx64 "\n",
                      s->prefix, xr->start);
    }

    if ((xr->end + (1 << 12)) & s->addr_mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad region end address %" PRIx64 "\n",
                      s->prefix, xr->end);
    }

    if (xr->start < s->addr_mask) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Too low region start address %" PRIx64 "\n",
                      s->prefix, xr->end);
    }

    xr->start &= ~s->addr_mask;

    id_match = (xr->master.mask & xr->master.id) ==
               (xr->master.mask & master_id);
    return id_match && (addr >= xr->start && addr <= xr->end + ((1 << 12) - 1));
}

static void xmpu_decode_region(XMPU *s, XMPURegion *xr, unsigned int region)
{
    assert(region < NR_XMPU_REGIONS);
    uint32_t config;
    unsigned int offset = (region * (R_R01_START_LO - R_R00_START_LO));

    xr->start = ((uint64_t)s->regs[offset + R_R00_START_HI]) << 32;
    xr->start |= s->regs[offset + R_R00_START_LO];
    xr->end = ((uint64_t)s->regs[offset + R_R00_END_HI]) << 32;
    xr->end |= s->regs[offset + R_R00_END_LO];
    xr->size = xr->end - xr->start;

    /* If the start and end addrs are the same, we cover 1 page */
    if (xr->start == xr->end) {
        xr->end += s->addr_mask;
    }

    xr->master.u32 = s->regs[offset + R_R00_MASTER];

    config = s->regs[offset + R_R00_CONFIG];
    xr->config.enable = FIELD_EX32(config, R00_CONFIG, ENABLE);
    xr->config.rdallowed = FIELD_EX32(config, R00_CONFIG, RDALLOWED);
    xr->config.wrallowed = FIELD_EX32(config, R00_CONFIG, WRALLOWED);
    xr->config.regionns = FIELD_EX32(config, R00_CONFIG, REGIONNS);
    xr->config.nschecktype = FIELD_EX32(config, R00_CONFIG, NSCHECKTYPE);
}

static void xmpu_setup_postw(RegisterInfo *reg, uint64_t val64)
{
    XMPU *s = XILINX_DDRMC_XMPU(reg->opaque);
    uint32_t align = ARRAY_FIELD_EX32(s->regs, CTRL, ALIGNCFG);

    s->addr_shift = align ? 20 : 12;
    s->addr_mask = (1 << s->addr_shift) - 1;
    xmpu_flush(s);
}

static uint64_t lock_prew(RegisterInfo *reg, uint64_t val64)
{
    XMPU *s = XILINX_DDRMC_XMPU(reg->opaque);
    uint32_t regwrdis = ARRAY_FIELD_EX32(s->regs, LOCK, REGWRDIS);

    /* Once set, REGWRDIS can only be cleared by a POR */
    return (regwrdis ? regwrdis : val64);
}

static const RegisterAccessInfo ddrmc_xmpu_regs_info[] = {
    {   .name = "CTRL",  .addr = A_CTRL,
        .reset = 0xb,
        .rsvd = 0xfffffff0,
        .post_write = xmpu_setup_postw,
    },{ .name = "ERR_STATUS",  .addr = A_ERR_STATUS,
        .rsvd = 0xfffffe01,
        .ro = 0x1,
    },{ .name = "ERR_ADD_LO",  .addr = A_ERR_ADD_LO,
    },{ .name = "ERR_ADD_HI",  .addr = A_ERR_ADD_HI,
        .rsvd = 0xffff0000,
    },{ .name = "ERR_AXI_ID",  .addr = A_ERR_AXI_ID,
        .rsvd = 0xfffffc00,
    },{ .name = "LOCK",  .addr = A_LOCK,
        .pre_write = lock_prew,
    },{ .name = "R00_START_LO",  .addr = A_R00_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R00_START_HI",  .addr = A_R00_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R00_END_LO0",  .addr = A_R00_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R00_END_HI",  .addr = A_R00_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R00_MASTER",  .addr = A_R00_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R00_CONFIG",  .addr = A_R00_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R01_START_LO",  .addr = A_R01_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R01_START_HI",  .addr = A_R01_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R01_END_LO",  .addr = A_R01_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R01_END_HI",  .addr = A_R01_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R01_MASTER",  .addr = A_R01_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R01_CONFIG",  .addr = A_R01_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R02_START_LO",  .addr = A_R02_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R02_START_HI",  .addr = A_R02_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R02_END_LO",  .addr = A_R02_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R02_END_HI",  .addr = A_R02_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R02_MASTER",  .addr = A_R02_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R02_CONFIG",  .addr = A_R02_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R03_START_LO",  .addr = A_R03_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R03_START_HI",  .addr = A_R03_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R03_END_LO",  .addr = A_R03_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R03_END_HI",  .addr = A_R03_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R03_MASTER",  .addr = A_R03_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R03_CONFIG",  .addr = A_R03_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R04_START_LO",  .addr = A_R04_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R04_START_HI",  .addr = A_R04_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R00_END_LO",  .addr = A_R04_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R04_END_HI",  .addr = A_R04_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R04_MASTER",  .addr = A_R04_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R04_CONFIG",  .addr = A_R04_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R05_START_LO",  .addr = A_R05_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R05_START_HI",  .addr = A_R05_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R05_END_LO",  .addr = A_R05_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R05_END_HI",  .addr = A_R05_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R05_MASTER",  .addr = A_R05_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R05_CONFIG",  .addr = A_R05_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R06_START_LO",  .addr = A_R06_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R06_START_HI",  .addr = A_R06_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R06_END_LO",  .addr = A_R06_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R06_END_HI",  .addr = A_R06_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R06_MASTER",  .addr = A_R06_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R06_CONFIG",  .addr = A_R06_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R07_START_LO",  .addr = A_R07_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R07_START_HI",  .addr = A_R07_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R07_END_LO",  .addr = A_R07_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R07_END_HI",  .addr = A_R07_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R07_MASTER",  .addr = A_R07_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R07_CONFIG",  .addr = A_R07_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R08_START_LO",  .addr = A_R08_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R08_START_HI",  .addr = A_R08_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R08_END_LO",  .addr = A_R08_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R08_END_HI",  .addr = A_R08_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R08_MASTER",  .addr = A_R08_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R08_CONFIG",  .addr = A_R08_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R09_START_LO",  .addr = A_R09_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R09_START_HI",  .addr = A_R09_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R09_END_LO",  .addr = A_R09_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R09_END_HI",  .addr = A_R09_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R09_MASTER",  .addr = A_R09_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R09_CONFIG",  .addr = A_R09_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R10_START_LO",  .addr = A_R10_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R10_START_HI",  .addr = A_R10_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R10_END_LO",  .addr = A_R10_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R10_END_HI",  .addr = A_R10_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R10_MASTER",  .addr = A_R10_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R10_CONFIG",  .addr = A_R10_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R11_START_LO",  .addr = A_R11_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R11_START_HI",  .addr = A_R11_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R11_END_LO",  .addr = A_R11_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R11_END_HI",  .addr = A_R11_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R11_MASTER",  .addr = A_R11_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R11_CONFIG",  .addr = A_R11_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R12_START_LO",  .addr = A_R12_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R12_START_HI",  .addr = A_R12_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R12_END_LO",  .addr = A_R12_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R12_END_HI",  .addr = A_R12_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R12_MASTER",  .addr = A_R12_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R12_CONFIG",  .addr = A_R12_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R13_START_LO",  .addr = A_R13_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R13_START_HI",  .addr = A_R13_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R13_END_LO",  .addr = A_R13_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R13_END_HI",  .addr = A_R13_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R13_MASTER",  .addr = A_R13_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R13_CONFIG",  .addr = A_R13_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R14_START_LO",  .addr = A_R14_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R14_START_HI",  .addr = A_R14_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R14_END_LO",  .addr = A_R14_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R14_END_HI",  .addr = A_R14_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R14_MASTER",  .addr = A_R14_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R14_CONFIG",  .addr = A_R14_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    },{ .name = "R15_START_LO",  .addr = A_R15_START_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R15_START_HI",  .addr = A_R15_START_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R15_END_LO",  .addr = A_R15_END_LO,
        .rsvd = 0xfff,
        .ro = 0xfff,
    },{ .name = "R15_END_HI",  .addr = A_R15_END_HI,
        .rsvd = 0xffff0000,
    },{ .name = "R15_MASTER",  .addr = A_R15_MASTER,
        .rsvd = 0xfc00fc00,
        .ro = 0xfc00,
    },{ .name = "R15_CONFIG",  .addr = A_R15_CONFIG,
        .reset = 0x8,
        .rsvd = 0xffffffe0,
    }
};

static void xmpu_reset(DeviceState *dev)
{
    XMPU *s = XILINX_DDRMC_XMPU(dev);
    unsigned int i;

    for (i = 0; i < s->regs_size; ++i) {
        register_reset(&s->regs_info[i]);
    }

    xmpu_flush(s);
}

static XMPU *xmpu_from_mr(void *mr_accessor)
{
    RegisterInfoArray *reg_array = mr_accessor;
    Object *obj;

    assert(reg_array != NULL);

    obj = reg_array->mem.owner;
    assert(obj);

    return XILINX_DDRMC_XMPU(obj);
}

static MemTxResult xmpu_read(void *opaque, hwaddr addr, uint64_t *value,
                             unsigned size, MemTxAttrs attr)
{
    XMPU *s = xmpu_from_mr(opaque);

    return xmpu_read_common(opaque, s, addr, value, size, attr);
}

static MemTxResult xmpu_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size, MemTxAttrs attr)
{
    XMPU *s = xmpu_from_mr(opaque);
    bool locked;
    MemTxResult res;

    locked = ARRAY_FIELD_EX32(s->regs, LOCK, REGWRDIS);
    if (locked && (addr >= A_ERR_STATUS) && (addr < A_LOCK)) {
        /* Locked access.  */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: accessing locked register "\
                      "0x%"HWADDR_PRIx"\n",
                      object_get_canonical_path(OBJECT(s)), addr);
        return MEMTX_ERROR;
    }

    res = xmpu_write_common(opaque, s, addr, value, size, attr);

    if (addr > R_R00_MASTER) {
        xmpu_flush(s);
    }

    return res;
}

static const MemoryRegionOps xmpu_ops = {
    .read_with_attrs = xmpu_read,
    .write_with_attrs = xmpu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static MemTxResult zero_read(void *opaque, hwaddr addr, uint64_t *pdata,
                             unsigned size, MemTxAttrs attr)
{
    XMPUMaster *xm = opaque;
    XMPU *s = xm->parent;
    bool sec_vio;
    IOMMUTLBEntry ret;
    int perm;
    MemTxResult status;

    ret = xmpu_master_translate(xm, addr, attr.secure, attr.requester_id,
                                &sec_vio, &perm);
    ret.perm = perm;

    if (ret.perm & IOMMU_RO) {
        dma_memory_read(&xm->down.rw.as, addr, pdata, size, MEMTXATTRS_UNSPECIFIED);
        status = MEMTX_OK;
    } else {
        s->regs[R_ERR_ADD_HI] = (addr + s->cfg.base) >> 32;
        s->regs[R_ERR_ADD_LO] = (addr + s->cfg.base) & 0xFFFFF000;

        ARRAY_FIELD_DP32(s->regs, ERR_STATUS, REGIONVIO, xm->curr_region);
        ARRAY_FIELD_DP32(s->regs, ERR_AXI_ID, ERR_SMID, attr.requester_id);
        if (sec_vio) {
            ARRAY_FIELD_DP32(s->regs, ERR_STATUS, SECURITYVIO, true);
        } else {
            ARRAY_FIELD_DP32(s->regs, ERR_STATUS, RDPERMVIO, true);
        }
        *pdata = 0;
        status = MEMTX_ERROR;
    }

    return status;
}

static MemTxResult zero_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size, MemTxAttrs attr)
{
    XMPUMaster *xm = opaque;
    XMPU *s = xm->parent;
    bool sec_vio;
    IOMMUTLBEntry ret;
    int perm;
    MemTxResult status;

    ret = xmpu_master_translate(xm, addr, attr.secure, attr.requester_id,
                                &sec_vio, &perm);
    ret.perm = perm;

    if (ret.perm & IOMMU_WO) {
        dma_memory_write(&xm->down.rw.as, addr, &value, size, MEMTXATTRS_UNSPECIFIED);
        status = MEMTX_OK;
    } else {
        s->regs[R_ERR_ADD_HI] = (addr + s->cfg.base) >> 32;
        s->regs[R_ERR_ADD_LO] = (addr + s->cfg.base) & 0xFFFFFFFF;

        ARRAY_FIELD_DP32(s->regs, ERR_STATUS, REGIONVIO, xm->curr_region);
        ARRAY_FIELD_DP32(s->regs, ERR_AXI_ID, ERR_SMID, attr.requester_id);
        if (sec_vio) {
            ARRAY_FIELD_DP32(s->regs, ERR_STATUS, SECURITYVIO, true);
        } else {
            ARRAY_FIELD_DP32(s->regs, ERR_STATUS, WRPERMVIO, true);
        }
        status = MEMTX_ERROR;
    }

    return status;
}

static const MemoryRegionOps zero_ops = {
    .read_with_attrs = zero_read,
    .write_with_attrs = zero_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    }
};

static void xmpu_realize(DeviceState *dev, Error **errp)
{
    XMPU *s = XILINX_DDRMC_XMPU(dev);

    s->prefix = object_get_canonical_path(OBJECT(dev));
    s->addr_shift = DEFAULT_ADDR_SHIFT;
    s->addr_mask = ((1ULL << s->addr_shift) - 1);
    s->decode_region = xmpu_decode_region;
    s->match = xmpu_match;
    s->masters[0].parent = s;
}

static void xmpu_init(Object *obj)
{
    XMPU *s = XILINX_DDRMC_XMPU(obj);
    s->regs_size = XMPU_R_MAX;
    xmpu_init_common(s, obj, TYPE_XILINX_DDRMC_XMPU, &xmpu_ops,
                     ddrmc_xmpu_regs_info, ARRAY_SIZE(ddrmc_xmpu_regs_info));
}

static bool xmpu_parse_reg(FDTGenericMMap *obj, FDTGenericRegPropInfo reg,
                           Error **errp)
{
    XMPU *s = XILINX_DDRMC_XMPU(obj);

    return xmpu_parse_reg_common(s, TYPE_XILINX_DDRMC_XMPU,
                                 TYPE_XILINX_XMPU_IOMMU_MEMORY_REGION,
                                 &zero_ops, reg, obj, errp);
}

static Property xmpu_properties[] = {
    DEFINE_PROP_UINT64("protected-base", XMPU, cfg.base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_xmpu = {
    .name = TYPE_XILINX_DDRMC_XMPU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XMPU, XMPU_VERSAL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void xmpu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    FDTGenericMMapClass *fmc = FDT_GENERIC_MMAP_CLASS(klass);

    dc->reset = xmpu_reset;
    dc->realize = xmpu_realize;
    dc->vmsd = &vmstate_xmpu;
    device_class_set_props(dc, xmpu_properties);
    fmc->parse_reg = xmpu_parse_reg;
}

static void xmpu_iommu_memory_region_class_init(ObjectClass *klass, void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = xmpu_translate;
    imrc->attrs_to_index = xmpu_attrs_to_index;
    imrc->num_indexes = xmpu_num_indexes;
}

static const TypeInfo xmpu_info = {
    .name          = TYPE_XILINX_DDRMC_XMPU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XMPU),
    .class_init    = xmpu_class_init,
    .instance_init = xmpu_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_FDT_GENERIC_MMAP },
        { },
    },
};

static const TypeInfo xmpu_iommu_memory_region_info = {
    .name = TYPE_XILINX_XMPU_IOMMU_MEMORY_REGION,
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .class_init = xmpu_iommu_memory_region_class_init,
};

static void xmpu_register_types(void)
{
    type_register_static(&xmpu_info);
    type_register_static(&xmpu_iommu_memory_region_info);
}

type_init(xmpu_register_types)
