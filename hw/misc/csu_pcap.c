/*
 * QEMU model of ZynqMP CSU Stream PCAP
 *
 * For the most part, a dummy device model. Consumes as much data off the stream
 * interface as you can throw at it and produces zeros as fast as the sink is
 * willing to accept them.
 *
 * Copyright (c) 2013 Peter Xilinx Inc
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
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
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "hw/register.h"
#include "hw/sysbus.h"

#include "hw/stream.h"
#include "qemu/bitops.h"

#ifndef ZYNQMP_CSU_PCAP_ERR_DEBUG
#define ZYNQMP_CSU_PCAP_ERR_DEBUG 0
#endif

#define TYPE_ZYNQMP_CSU_PCAP "zynqmp.csu-pcap"

#define ZYNQMP_CSU_PCAP(obj) \
     OBJECT_CHECK(ZynqMPCSUPCAP, (obj), TYPE_ZYNQMP_CSU_PCAP)

/* FIXME: This is a random number, maybe match to PCAP fifo size or just pick
 * something reasonable that keep QEMU performing well.
 */
#define CHUNK_SIZE (8 << 10)

REG32(PCAP_PROG, 0x0)
    FIELD(PCAP_PROG, PCFG_PROG_B, 0, 1)
REG32(PCAP_RDWR, 0x4)
    FIELD(PCAP_RDWR, PCAP_RDWR_B, 0, 1)
REG32(PCAP_CTRL, 0x8)
    FIELD(PCAP_CTRL, PCFG_GSR, 3, 1)
    FIELD(PCAP_CTRL, PCFG_GTS, 2, 1)
    FIELD(PCAP_CTRL, PCFG_POR_CNT_4K, 1, 1)
    FIELD(PCAP_CTRL, PCAP_PR, 0, 1)
REG32(PCAP_RESET, 0xc)
    FIELD(PCAP_RESET, RESET, 0, 1)
REG32(PCAP_STATUS, 0x10)
    FIELD(PCAP_STATUS, PCFG_FUSE_PL_DIS, 31, 1)
    FIELD(PCAP_STATUS, PCFG_PL_CFG_USED, 30, 1)
    FIELD(PCAP_STATUS, PCFG_IS_ZYNQ, 29, 1)
    FIELD(PCAP_STATUS, PCFG_GWE, 13, 1)
    FIELD(PCAP_STATUS, PCFG_MCAP_MODE, 12, 1)
    FIELD(PCAP_STATUS, PL_GTS_USR_B, 11, 1)
    FIELD(PCAP_STATUS, PL_GTS_CFG_B, 10, 1)
    FIELD(PCAP_STATUS, PL_GPWRDWN_B, 9, 1)
    FIELD(PCAP_STATUS, PL_GHIGH_B, 8, 1)
    FIELD(PCAP_STATUS, PL_FST_CFG, 7, 1)
    FIELD(PCAP_STATUS, PL_CFG_RESET_B, 6, 1)
    FIELD(PCAP_STATUS, PL_SEU_ERROR, 5, 1)
    FIELD(PCAP_STATUS, PL_EOS, 4, 1)
    FIELD(PCAP_STATUS, PL_DONE, 3, 1)
    FIELD(PCAP_STATUS, PL_INIT, 2, 1)
    FIELD(PCAP_STATUS, PCAP_RD_IDLE, 1, 1)
    FIELD(PCAP_STATUS, PCAP_WR_IDLE, 0, 1)

#define R_MAX (R_PCAP_STATUS + 1)

typedef struct ZynqMPCSUPCAP {
    SysBusDevice parent_obj;
    StreamSlave *tx_dev;
    MemoryRegion iomem;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} ZynqMPCSUPCAP;

static void zynqmp_csu_pcap_reset(DeviceState *dev);

static void pcap_prog_post_wr(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(reg->opaque);

    ARRAY_FIELD_DP32(s->regs, PCAP_STATUS, PL_CFG_RESET_B,
                     val & R_PCAP_PROG_PCFG_PROG_B_MASK);
}

static void pcap_reset_post_wr(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(reg->opaque);

    if (!(val & R_PCAP_RESET_RESET_MASK)) {
        zynqmp_csu_pcap_reset(DEVICE(s));
        ARRAY_FIELD_DP32(s->regs, PCAP_RESET, RESET, 0);
    }
}

static uint64_t pcap_status_post_rd(RegisterInfo *reg, uint64_t val)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(reg->opaque);

    if (ARRAY_FIELD_EX32(s->regs, PCAP_STATUS, PL_CFG_RESET_B)) {
        ARRAY_FIELD_DP32(s->regs, PCAP_STATUS, PL_CFG_RESET_B, 0);
        ARRAY_FIELD_DP32(s->regs, PCAP_STATUS, PL_INIT, 1);
    }
    return val;
}

static RegisterAccessInfo pcap_regs_info[] = {
    { .name = "PCAP_PROG",  .addr = A_PCAP_PROG,
      .post_write = pcap_prog_post_wr,
    },{ .name = "PCAP_RDWR",  .addr = A_PCAP_RDWR,
    },{ .name = "PCAP_CTRL",  .addr = A_PCAP_CTRL,
        .reset = 0x1,
    },{ .name = "PCAP_RESET",  .addr = A_PCAP_RESET,
        .reset = 0x1,
        .post_write = pcap_reset_post_wr,
    },{ .name = "PCAP_STATUS",  .addr = A_PCAP_STATUS,
        .reset = 0x3,
        .rsvd = 0x1fffc000,
        .ro = 0xffffffff,
        .post_read = pcap_status_post_rd,
    }
};

static void zynqmp_csu_pcap_notify(void *opaque)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(opaque);
    static uint8_t zeros[CHUNK_SIZE];

    /* blast away - fire as many zeros as the target wants to accept */
    while (stream_can_push(s->tx_dev, zynqmp_csu_pcap_notify, s)) {
        size_t ret = stream_push(s->tx_dev, zeros, CHUNK_SIZE, true);
        /* FIXME: Check - assuming PCAP must be 32-bit aligned xactions */
        assert(!(ret % 4));
    }
}

static void zynqmp_csu_pcap_reset(DeviceState *dev)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    zynqmp_csu_pcap_notify(s);
}

static size_t zynqmp_csu_pcap_stream_push(StreamSlave *obj, uint8_t *buf,
                                          size_t len, bool eop)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(obj);
    assert(!(len % 4));

    ARRAY_FIELD_DP32(s->regs, PCAP_STATUS, PL_DONE, 1);
    /* consume all the data with no action */
    return len;
}

static const MemoryRegionOps pcap_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void zynqmp_csu_pcap_init(Object *obj)
{
    ZynqMPCSUPCAP *s = ZYNQMP_CSU_PCAP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    /* Real HW has a link, but no way of initiating this link */
    object_property_add_link(obj, "stream-connected-pcap", TYPE_STREAM_SLAVE,
                             (Object **)&s->tx_dev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);

    memory_region_init(&s->iomem, obj, TYPE_ZYNQMP_CSU_PCAP, R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), pcap_regs_info,
                              ARRAY_SIZE(pcap_regs_info),
                              s->regs_info, s->regs,
                              &pcap_ops,
                              ZYNQMP_CSU_PCAP_ERR_DEBUG,
                              R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_zynqmp_csu_pcap = {
    .name = "zynqmp_csu_pcap",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static void zynqmp_csu_pcap_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    StreamSlaveClass *ssc = STREAM_SLAVE_CLASS(klass);

    dc->reset = zynqmp_csu_pcap_reset;
    dc->vmsd = &vmstate_zynqmp_csu_pcap;

    ssc->push = zynqmp_csu_pcap_stream_push;
}

static const TypeInfo zynqmp_csu_pcap_info = {
    .name          = TYPE_ZYNQMP_CSU_PCAP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ZynqMPCSUPCAP),
    .class_init    = zynqmp_csu_pcap_class_init,
    .instance_init = zynqmp_csu_pcap_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_STREAM_SLAVE },
        { }
    }
};

static void zynqmp_csu_pcap_register_types(void)
{
    type_register_static(&zynqmp_csu_pcap_info);
}

type_init(zynqmp_csu_pcap_register_types)
