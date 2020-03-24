/*
 * QEMU model of Xilinx CSU IPCores RSA5 4K accelerator.
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
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
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "hw/irq.h"
#include "qemu/log.h"

#include "hw/misc/ipcores-rsa5-4k.h"

#ifndef XILINX_CSU_RSA_ERR_DEBUG
#define XILINX_CSU_RSA_ERR_DEBUG 0
#endif

#define D(x)

#define TYPE_XILINX_CSU_RSA "zynqmp.csu-rsa"

#define XILINX_CSU_RSA(obj) \
     OBJECT_CHECK(XilinxRSA, (obj), TYPE_XILINX_CSU_RSA)

REG32(RSA_WR_DATA, 0x0)
    FIELD(RSA_WR_DATA, WR_DATA, 0, 8)
REG32(RSA_WR_ADDR, 0x4)
    FIELD(RSA_WR_ADDR, WR_ADDR, 0, 7)
REG32(RSA_RD_DATA, 0x8)
    FIELD(RSA_RD_DATA, RD_DATA, 0, 8)
REG32(RSA_RD_ADDR, 0xc)
    FIELD(RSA_RD_ADDR, RD_ADDR, 0, 7)
REG32(CTRL, 0x10)
    FIELD(CTRL, LEN_CODE, 4, 4)
    FIELD(CTRL, DONE_CLR_ABORT, 3, 1)
    FIELD(CTRL, OPCODE, 0, 3)
REG32(STATUS, 0x14)
    FIELD(STATUS, PROG_CNT, 3, 5)
    FIELD(STATUS, ERROR_RSA, 2, 1)
    FIELD(STATUS, BUSY, 1, 1)
    FIELD(STATUS, DONE, 0, 1)
REG32(MINV0, 0x18)
    FIELD(MINV0, MINV0, 0, 8)
REG32(MINV1, 0x1c)
    FIELD(MINV1, MINV1, 0, 8)
REG32(MINV2, 0x20)
    FIELD(MINV2, MINV2, 0, 8)
REG32(MINV3, 0x24)
    FIELD(MINV3, MINV2, 0, 8)
REG32(ZERO, 0x28)
    FIELD(ZERO, ZERO, 0, 1)

REG32(WR_DATA_0, 0x2c)
REG32(WR_DATA_1, 0x30)
REG32(WR_DATA_2, 0x34)
REG32(WR_DATA_3, 0x38)
REG32(WR_DATA_4, 0x3c)
REG32(WR_DATA_5, 0x40)
REG32(WR_ADDR, 0x44)
REG32(RD_DATA_0, 0x48)
REG32(RD_DATA_1, 0x4c)
REG32(RD_DATA_2, 0x50)
REG32(RD_DATA_3, 0x54)
REG32(RD_DATA_4, 0x58)
REG32(RD_DATA_5, 0x5c)
REG32(RD_ADDR, 0x60)

#define RSA_CORE_R_MAX (R_RD_ADDR + 1)

static const struct {
    unsigned int digits;
    unsigned int bits;
} len_code_map[] = {
    [0] = { 4, 512 },
    [1] = { 4, 576 },
    [2] = { 4, 705 },
    [3] = { 5, 768 },
    [4] = { 6, 992 },
    [5] = { 6, 1024 },
    [6] = { 7, 1152 },
    [7] = { 8, 1408 },
    [8] = { 9, 1536 },
    [9] = { 11, 1984 },
    [10] = { 11, 2048 },
    [11] = { 17, 3072 },
    [12] = { 22, 4096 },
};

typedef struct XilinxRSA {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq parent_irq;

    IPCoresRSA rsa;
    RegisterInfo regs_info[RSA_CORE_R_MAX];
    struct word wbuf;

    uint32_t regs[RSA_CORE_R_MAX];

    const char *prefix;
} XilinxRSA;

#define R_CONTROL_nop         0x00
#define R_CONTROL_exp         0x01
#define R_CONTROL_mod         0x02
#define R_CONTROL_mul         0x03
#define R_CONTROL_rrmod       0x04
#define R_CONTROL_exppre      0x05

typedef int (*ALUFunc)(IPCoresRSA *, unsigned int, unsigned int);

static const ALUFunc alu_ops[] = {
    [R_CONTROL_nop] = rsa_do_nop,
    [R_CONTROL_exp] = rsa_do_exp,
    [R_CONTROL_mod] = rsa_do_mod,
    [R_CONTROL_mul] = rsa_do_mul,
    [R_CONTROL_rrmod] = rsa_do_rrmod,
    [R_CONTROL_exppre] = rsa_do_exppre,
};

static void rsa_update_irq(XilinxRSA *s)
{
    bool v = ARRAY_FIELD_EX32(s->regs, STATUS, DONE);
    qemu_set_irq(s->parent_irq, v);
}

static void rsa_wdata_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);

    /* Shift.  */
    memmove(&s->wbuf.u8[0], &s->wbuf.u8[1],
            sizeof(s->wbuf.u8) - 1);
    s->wbuf.u8[sizeof(s->wbuf.u8) - 1] = val64;
}

static void rsa_waddr_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);

    if (val64 > ARRAY_SIZE(s->rsa.mem.words)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Wrong word address!",
                      s->prefix);
    }
    memcpy(&s->rsa.mem.words[val64], &s->wbuf.u8[0],
            sizeof(s->wbuf.u8));
    s->rsa.word_def[val64] = true;
}

static void rsa_wr_addr32_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);

    if (val64 > ARRAY_SIZE(s->rsa.mem.words)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Wrong word address!",
                      s->prefix);
    }
    memcpy(&s->rsa.mem.words[val64], &s->regs[R_WR_DATA_0],
           BYTES_PER_WORD);
    s->rsa.word_def[val64] = true;
}

static uint64_t rsa_rdata_pr(RegisterInfo *reg, uint64_t val)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);
    uint8_t r;

    r = s->wbuf.u8[0];
    memmove(&s->wbuf.u8[0], &s->wbuf.u8[1],
            sizeof(s->wbuf.u8) - 1);
    return r;
}

static void rsa_raddr_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);

    if (val64 > ARRAY_SIZE(s->rsa.mem.words)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Wrong word address!",
                      s->prefix);
    }
    memcpy(&s->wbuf.u8[0], &s->rsa.mem.words[val64].u8[0],
            sizeof s->wbuf.u8);
}

static void rsa_rd_addr32_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);

    if (val64 > ARRAY_SIZE(s->rsa.mem.words)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Wrong word address!",
                      s->prefix);
    }
    memcpy(&s->regs[R_RD_DATA_0], &s->rsa.mem.words[val64].u8[0],
           BYTES_PER_WORD);
}

static uint64_t rsa_zero_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);
    uint32_t v = val64;

    if (v & 1) {
        memset(&s->wbuf.u8[0], 0, sizeof s->wbuf.u8);
        memset(&s->regs[R_WR_DATA_0], 0, BYTES_PER_WORD);
    }
    return 0;
}

static void rsa_control_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);
    uint32_t v = val64;
    unsigned int op = v & 7;
    unsigned int lencode, digits, bitlen;
    unsigned int abort = v & 8;
    int err;

    if (op > R_CONTROL_exppre) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Undefined ALU op",
                      s->prefix);
        return;
    }

    lencode = (v >> 4) & 0xf;
    assert(lencode <= 0xC);
    bitlen = len_code_map[lencode].bits;
    digits = len_code_map[lencode].digits * 6;

    /* Clear the error status for every new op.  */
    ARRAY_FIELD_DP32(s->regs, STATUS, ERROR_RSA, false);

    err = alu_ops[op](&s->rsa, bitlen, digits);
    if (err) {
        ARRAY_FIELD_DP32(s->regs, STATUS, ERROR_RSA, true);
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Detected an error: %s\n",
                      s->prefix, rsa_strerror(err));
    } else {
        ARRAY_FIELD_DP32(s->regs, STATUS, DONE, true);
    }

    if (abort) {
        ARRAY_FIELD_DP32(s->regs, STATUS, DONE, false);
    }

    rsa_update_irq(s);
}

static void rsa_minv_pw(RegisterInfo *reg, uint64_t val64)
{
    XilinxRSA *s = XILINX_CSU_RSA(reg->opaque);
    uint32_t minv;

    minv = s->regs[R_MINV0];
    minv |= s->regs[R_MINV1] << 8;
    minv |= s->regs[R_MINV2] << 16;
    minv |= s->regs[R_MINV3] << 24;
    rsa_set_minv(&s->rsa, minv);
}

static const RegisterAccessInfo rsa_regs_info[] = {
    {   .name = "RSA_WR_DATA",  .addr = A_RSA_WR_DATA,
        .post_write = rsa_wdata_pw
    },{ .name = "RSA_WR_ADDR",  .addr = A_RSA_WR_ADDR,
        .post_write = rsa_waddr_pw
    },{ .name = "RSA_RD_DATA",  .addr = A_RSA_RD_DATA,
        .ro = 0xff,
        .post_read = rsa_rdata_pr
    },{ .name = "RSA_RD_ADDR",  .addr = A_RSA_RD_ADDR,
        .post_write = rsa_raddr_pw
    },{ .name = "CTRL",  .addr = A_CTRL,
        .post_write = rsa_control_pw
    },{ .name = "STATUS",  .addr = A_STATUS,
        .ro = 0xff,
    },{ .name = "MINV0",  .addr = A_MINV0,
        .post_write = rsa_minv_pw
    },{ .name = "MINV1",  .addr = A_MINV1,
        .post_write = rsa_minv_pw
    },{ .name = "MINV2",  .addr = A_MINV2,
        .post_write = rsa_minv_pw
    },{ .name = "MINV3",  .addr = A_MINV3,
        .post_write = rsa_minv_pw
    },{ .name = "ZERO",  .addr = A_ZERO,
        .pre_write = rsa_zero_pw,
    },

    {   .name = "WR_DATA_0",  .addr = A_WR_DATA_0,
    },{ .name = "WR_DATA_1",  .addr = A_WR_DATA_1,
    },{ .name = "WR_DATA_2",  .addr = A_WR_DATA_2,
    },{ .name = "WR_DATA_3",  .addr = A_WR_DATA_3,
    },{ .name = "WR_DATA_4",  .addr = A_WR_DATA_4,
    },{ .name = "WR_DATA_5",  .addr = A_WR_DATA_5,
    },{ .name = "WR_ADDR",  .addr = A_WR_ADDR,
        .post_write = rsa_wr_addr32_pw
    },{ .name = "RD_DATA_0",  .addr = A_RD_DATA_0,
        .ro = 0xffffffff,
    },{ .name = "RD_DATA_1",  .addr = A_RD_DATA_1,
        .ro = 0xffffffff,
    },{ .name = "RD_DATA_2",  .addr = A_RD_DATA_2,
        .ro = 0xffffffff,
    },{ .name = "RD_DATA_3",  .addr = A_RD_DATA_3,
        .ro = 0xffffffff,
    },{ .name = "RD_DATA_4",  .addr = A_RD_DATA_4,
        .ro = 0xffffffff,
    },{ .name = "RD_DATA_5",  .addr = A_RD_DATA_5,
        .ro = 0xffffffff,
    },{ .name = "RD_ADDR",  .addr = A_RD_ADDR,
        .post_write = rsa_rd_addr32_pw
    }
};

static void csu_rsa_reset(DeviceState *dev)
{
    XilinxRSA *s = XILINX_CSU_RSA(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    rsa_reset(&s->rsa);
    rsa_set_exp_result_shift(&s->rsa, 64);
    memset(&s->wbuf, 0, sizeof s->wbuf);
}

static const MemoryRegionOps csu_rsa_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void xlx_rsa_init(Object *obj)
{
    XilinxRSA *s = XILINX_CSU_RSA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_CSU_RSA, RSA_CORE_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), rsa_regs_info,
                              ARRAY_SIZE(rsa_regs_info),
                              s->regs_info, s->regs,
                              &csu_rsa_ops,
                              XILINX_CSU_RSA_ERR_DEBUG,
                              RSA_CORE_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->parent_irq);
}

static const VMStateDescription vmstate_xlx_rsa = {
    .name = TYPE_XILINX_CSU_RSA,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(rsa.mem.u8, XilinxRSA, RAMSIZE),
        VMSTATE_UINT8_ARRAY(wbuf.u8, XilinxRSA, BYTES_PER_WORD),
        VMSTATE_UINT32_ARRAY(regs, XilinxRSA, RSA_CORE_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void xlx_rsa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = csu_rsa_reset;
    dc->vmsd = &vmstate_xlx_rsa;
}

static const TypeInfo xlx_rsa_info = {
    .name          = TYPE_XILINX_CSU_RSA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XilinxRSA),
    .class_init    = xlx_rsa_class_init,
    .instance_init = xlx_rsa_init,
};

static void xlx_rsa_register_types(void)
{
    type_register_static(&xlx_rsa_info);
}

type_init(xlx_rsa_register_types)
