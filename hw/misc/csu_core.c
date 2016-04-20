/*
 * QEMU model of Xilinx CSU Core Functionality
 *
 * For the most part, a dummy device model.
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

#include "hw/sysbus.h"
#include "qemu/log.h"

#include "qemu/bitops.h"
#include "qapi/qmp/qerror.h"
#include "hw/register.h"

#ifndef XILINX_CSU_CORE_ERR_DEBUG
#define XILINX_CSU_CORE_ERR_DEBUG 0
#endif

#define TYPE_XILINX_CSU_CORE "xlnx.zynqmp-csu-core"

#define XILINX_CSU_CORE(obj) \
     OBJECT_CHECK(CSU, (obj), TYPE_XILINX_CSU_CORE)

#define XFSBL_PLATFORM_QEMU  0X00003000U
#define QEMU_IDCODE          0x4600093

REG32(CSU_STATUS, 0x0)
    FIELD(CSU_STATUS, BOOT_ENC, 1, 1)
    FIELD(CSU_STATUS, BOOT_AUTH, 1, 0)
REG32(CSU_CTRL, 0x4)
    FIELD(CSU_CTRL, SLVERR_ENABLE, 1, 4)
    FIELD(CSU_CTRL, CSU_CLK_SEL, 1, 0)
REG32(CSU_SSS_CFG, 0x8)
    FIELD(CSU_SSS_CFG, PSTP_SSS, 4, 16)
    FIELD(CSU_SSS_CFG, SHA_SSS, 4, 12)
    FIELD(CSU_SSS_CFG, AES_SSS, 4, 8)
    FIELD(CSU_SSS_CFG, DMA_SSS, 4, 4)
    FIELD(CSU_SSS_CFG, PCAP_SSS, 4, 0)
REG32(CSU_DMA_RESET, 0xc)
    FIELD(CSU_DMA_RESET, RESET, 1, 0)
REG32(CSU_MULTI_BOOT, 0x10)
REG32(CSU_TAMPER_TRIG, 0x14)
    FIELD(CSU_TAMPER_TRIG, TAMPER, 1, 0)
REG32(CSU_FT_STATUS, 0x18)
    FIELD(CSU_FT_STATUS, R_UE, 1, 31)
    FIELD(CSU_FT_STATUS, R_VOTER_ERROR, 1, 30)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_23, 1, 29)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_13, 1, 28)
    FIELD(CSU_FT_STATUS, R_COMP_ERR_12, 1, 27)
    FIELD(CSU_FT_STATUS, R_MISMATCH_23_A, 1, 26)
    FIELD(CSU_FT_STATUS, R_MISMATCH_13_A, 1, 25)
    FIELD(CSU_FT_STATUS, R_MISMATCH_12_A, 1, 24)
    FIELD(CSU_FT_STATUS, R_FT_ST_MISMATCH, 1, 23)
    FIELD(CSU_FT_STATUS, R_CPU_ID_MISMATCH, 1, 22)
    FIELD(CSU_FT_STATUS, R_SLEEP_RESET, 1, 19)
    FIELD(CSU_FT_STATUS, R_MISMATCH_23_B, 1, 18)
    FIELD(CSU_FT_STATUS, R_MISMATCH_13_B, 1, 17)
    FIELD(CSU_FT_STATUS, R_MISMATCH_12_B, 1, 16)
    FIELD(CSU_FT_STATUS, N_UE, 1, 15)
    FIELD(CSU_FT_STATUS, N_VOTER_ERROR, 1, 14)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_23, 1, 13)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_13, 1, 12)
    FIELD(CSU_FT_STATUS, N_COMP_ERR_12, 1, 11)
    FIELD(CSU_FT_STATUS, N_MISMATCH_23_A, 1, 10)
    FIELD(CSU_FT_STATUS, N_MISMATCH_13_A, 1, 9)
    FIELD(CSU_FT_STATUS, N_MISMATCH_12_A, 1, 8)
    FIELD(CSU_FT_STATUS, N_FT_ST_MISMATCH, 1, 7)
    FIELD(CSU_FT_STATUS, N_CPU_ID_MISMATCH, 1, 6)
    FIELD(CSU_FT_STATUS, N_SLEEP_RESET, 1, 3)
    FIELD(CSU_FT_STATUS, N_MISMATCH_23_B, 1, 2)
    FIELD(CSU_FT_STATUS, N_MISMATCH_13_B, 1, 1)
    FIELD(CSU_FT_STATUS, N_MISMATCH_12_B, 1, 0)
REG32(CSU_ISR, 0x20)
    FIELD(CSU_ISR, CSU_PL_ISO, 1, 15)
    FIELD(CSU_ISR, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_ISR, TAMPER, 1, 13)
    FIELD(CSU_ISR, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_ISR, APB_SLVERR, 1, 11)
    FIELD(CSU_ISR, TMR_FATAL, 1, 10)
    FIELD(CSU_ISR, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_ISR, AES_ERROR, 1, 8)
    FIELD(CSU_ISR, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_ISR, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_ISR, PL_POR_B, 1, 5)
    FIELD(CSU_ISR, PL_INIT, 1, 4)
    FIELD(CSU_ISR, PL_DONE, 1, 3)
    FIELD(CSU_ISR, SHA_DONE, 1, 2)
    FIELD(CSU_ISR, RSA_DONE, 1, 1)
    FIELD(CSU_ISR, AES_DONE, 1, 0)
REG32(CSU_IMR, 0x24)
    FIELD(CSU_IMR, CSU_PL_ISO, 1, 15)
    FIELD(CSU_IMR, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_IMR, TAMPER, 1, 13)
    FIELD(CSU_IMR, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_IMR, APB_SLVERR, 1, 11)
    FIELD(CSU_IMR, TMR_FATAL, 1, 10)
    FIELD(CSU_IMR, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_IMR, AES_ERROR, 1, 8)
    FIELD(CSU_IMR, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_IMR, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_IMR, PL_POR_B, 1, 5)
    FIELD(CSU_IMR, PL_INIT, 1, 4)
    FIELD(CSU_IMR, PL_DONE, 1, 3)
    FIELD(CSU_IMR, SHA_DONE, 1, 2)
    FIELD(CSU_IMR, RSA_DONE, 1, 1)
    FIELD(CSU_IMR, AES_DONE, 1, 0)
REG32(CSU_IER, 0x28)
    FIELD(CSU_IER, CSU_PL_ISO, 1, 15)
    FIELD(CSU_IER, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_IER, TAMPER, 1, 13)
    FIELD(CSU_IER, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_IER, APB_SLVERR, 1, 11)
    FIELD(CSU_IER, TMR_FATAL, 1, 10)
    FIELD(CSU_IER, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_IER, AES_ERROR, 1, 8)
    FIELD(CSU_IER, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_IER, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_IER, PL_POR_B, 1, 5)
    FIELD(CSU_IER, PL_INIT, 1, 4)
    FIELD(CSU_IER, PL_DONE, 1, 3)
    FIELD(CSU_IER, SHA_DONE, 1, 2)
    FIELD(CSU_IER, RSA_DONE, 1, 1)
    FIELD(CSU_IER, AES_DONE, 1, 0)
REG32(CSU_IDR, 0x2c)
    FIELD(CSU_IDR, CSU_PL_ISO, 1, 15)
    FIELD(CSU_IDR, CSU_RAM_ECC_ERROR, 1, 14)
    FIELD(CSU_IDR, TAMPER, 1, 13)
    FIELD(CSU_IDR, PUF_ACC_ERROR, 1, 12)
    FIELD(CSU_IDR, APB_SLVERR, 1, 11)
    FIELD(CSU_IDR, TMR_FATAL, 1, 10)
    FIELD(CSU_IDR, PL_SEU_ERROR, 1, 9)
    FIELD(CSU_IDR, AES_ERROR, 1, 8)
    FIELD(CSU_IDR, PCAP_WR_OVERFLOW, 1, 7)
    FIELD(CSU_IDR, PCAP_RD_OVERFLOW, 1, 6)
    FIELD(CSU_IDR, PL_POR_B, 1, 5)
    FIELD(CSU_IDR, PL_INIT, 1, 4)
    FIELD(CSU_IDR, PL_DONE, 1, 3)
    FIELD(CSU_IDR, SHA_DONE, 1, 2)
    FIELD(CSU_IDR, RSA_DONE, 1, 1)
    FIELD(CSU_IDR, AES_DONE, 1, 0)
REG32(JTAG_CHAIN_STATUS, 0x34)
    FIELD(JTAG_CHAIN_STATUS, ARM_DAP, 1, 1)
    FIELD(JTAG_CHAIN_STATUS, PL_TAP, 1, 0)
REG32(JTAG_SEC, 0x38)
    FIELD(JTAG_SEC, SSSS_DDRPHY_SEC, 3, 12)
    FIELD(JTAG_SEC, SSSS_PLTAP_EN, 3, 9)
    FIELD(JTAG_SEC, SSSS_PMU_SEC, 3, 6)
    FIELD(JTAG_SEC, SSSS_PLTAP_SEC, 3, 3)
    FIELD(JTAG_SEC, SSSS_DAP_SEC, 3, 0)
REG32(JTAG_DAP_CFG, 0x3c)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_SPNIDEN, 1, 7)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_SPIDEN, 1, 6)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_NIDEN, 1, 5)
    FIELD(JTAG_DAP_CFG, SSSS_RPU_DBGEN, 1, 4)
    FIELD(JTAG_DAP_CFG, SSSS_APU_SPNIDEN, 1, 3)
    FIELD(JTAG_DAP_CFG, SSSS_APU_SPIDEN, 1, 2)
    FIELD(JTAG_DAP_CFG, SSSS_APU_NIDEN, 1, 1)
    FIELD(JTAG_DAP_CFG, SSSS_APU_DBGEN, 1, 0)
REG32(IDCODE, 0x40)
REG32(VERSION, 0x44)
    FIELD(VERSION, PLATFORM_VERSION, 4, 16)
    FIELD(VERSION, PLATFORM, 4, 12)
    FIELD(VERSION, RTL_VERSION, 8, 4)
    FIELD(VERSION, PS_VERSION, 4, 0)

#define R_MAX (R_VERSION + 1)

typedef struct CSU {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq irq_csu;

    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} CSU;

static void csu_update_irq(CSU *s)
{
    bool pending = s->regs[R_CSU_ISR] & ~s->regs[R_CSU_IMR];
    qemu_set_irq(s->irq_csu, pending);
}

static void csu_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XILINX_CSU_CORE(reg->opaque);
    csu_update_irq(s);
}

static uint64_t int_enable_pre_write(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XILINX_CSU_CORE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CSU_IMR] &= ~val;
    csu_update_irq(s);
    return 0;
}

static uint64_t int_disable_pre_write(RegisterInfo *reg, uint64_t val64)
{
    CSU *s = XILINX_CSU_CORE(reg->opaque);
    uint32_t val = val64;

    s->regs[R_CSU_IMR] |= val;
    csu_update_irq(s);
    return 0;
}

static const RegisterAccessInfo csu_core_regs_info[] = {
    {   .name = "CSU_STATUS",  .decode.addr = A_CSU_STATUS,
        .ro = 0xffffffff,
    },{ .name = "CSU_CTRL",  .decode.addr = A_CSU_CTRL,
        .rsvd = 0xe,
    },{ .name = "CSU_SSS_CFG",  .decode.addr = A_CSU_SSS_CFG,
    },{ .name = "CSU_DMA_RESET",  .decode.addr = A_CSU_DMA_RESET,
    },{ .name = "CSU_MULTI_BOOT",  .decode.addr = A_CSU_MULTI_BOOT,
    },{ .name = "CSU_TAMPER_TRIG",  .decode.addr = A_CSU_TAMPER_TRIG,
    },{ .name = "CSU_FT_STATUS",  .decode.addr = A_CSU_FT_STATUS,
        .rsvd = 0x300030,
        .ro = 0xffffffff,
    },{ .name = "Interrupt Status",  .decode.addr = A_CSU_ISR,
        .w1c = 0xffffffff,
        .post_write = csu_isr_postw,
    },{ .name = "Interrupt Mask",  .decode.addr = A_CSU_IMR,
        .reset = 0xffffffff,
        .ro = 0xffffffff,
    },{ .name = "Interrupt Enable",  .decode.addr = A_CSU_IER,
        .pre_write = int_enable_pre_write,
    },{ .name = "Interrupt Disable",  .decode.addr = A_CSU_IDR,
        .pre_write = int_disable_pre_write,
    },{ .name = "JTAG_CHAIN_STATUS",  .decode.addr = A_JTAG_CHAIN_STATUS,
        .ro = 0x3,
    },{ .name = "JTAG_SEC",  .decode.addr = A_JTAG_SEC,
    },{ .name = "JTAG_DAP_CFG",  .decode.addr = A_JTAG_DAP_CFG,
    },{ .name = "IDCODE",  .decode.addr = A_IDCODE,
        .ro = 0xffffffff, .reset = QEMU_IDCODE,
    },{ .name = "VERSION",  .decode.addr = A_VERSION,
        .ro = 0xfffff,
        .reset = XFSBL_PLATFORM_QEMU,
    },
};

static const MemoryRegionOps csu_core_ops = {
    .read = register_read_memory_le,
    .write = register_write_memory_le,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static void csu_core_reset(DeviceState *dev)
{
    CSU *s = XILINX_CSU_CORE(dev);
    int i;

    for (i = 0; i < R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }

    csu_update_irq(s);
}


static void csu_core_realize(DeviceState *dev, Error **errp)
{
    CSU *s = XILINX_CSU_CORE(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    int i;

    for (i = 0; i < ARRAY_SIZE(csu_core_regs_info); ++i) {
        RegisterInfo *r = &s->regs_info[i];

        *r = (RegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    csu_core_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &csu_core_regs_info[i],
            .debug = XILINX_CSU_CORE_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
        memory_region_init_io(&r->mem, OBJECT(dev), &csu_core_ops, r,
                              r->access->name, 4);
        memory_region_add_subregion(&s->iomem, r->access->decode.addr, &r->mem);
    }
    return;
}

static void csu_core_init(Object *obj)
{
    CSU *s = XILINX_CSU_CORE(obj);

    memory_region_init(&s->iomem, obj, TYPE_XILINX_CSU_CORE, R_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_csu);
}

static const VMStateDescription vmstate_csu_core = {
    .name = TYPE_XILINX_CSU_CORE,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CSU, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void csu_core_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = csu_core_reset;
    dc->realize = csu_core_realize;
    dc->vmsd = &vmstate_csu_core;
}

static const TypeInfo csu_core_info = {
    .name          = TYPE_XILINX_CSU_CORE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CSU),
    .class_init    = csu_core_class_init,
    .instance_init = csu_core_init,
};

static void csu_core_register_types(void)
{
    type_register_static(&csu_core_info);
}

type_init(csu_core_register_types)
