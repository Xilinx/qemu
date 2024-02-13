/*
 * QEMU model of the EMMC SD3.0/SDIO3.0/eMMc5.1 Host Controller
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sd/zynqmp-sdhci.h"

#ifndef XLNX_VERSALNET_EMMC_ERR_DEBUG
#define XLNX_VERSALNET_EMMC_ERR_DEBUG 0
#endif

#define TYPE_XLNX_VERSALNET_EMMC "xlnx.versalnet-emmc"

#define XLNX_VERSALNET_EMMC(obj) \
     OBJECT_CHECK(VersalNetEMMC, (obj), TYPE_XLNX_VERSALNET_EMMC)

REG32(REG_CQVERSION, 0x00)
    FIELD(REG_CQVERSION, EMMCMAJORVERSIONNUMBER, 8, 4)
    FIELD(REG_CQVERSION, EMMCMINORVERSIONNUMBER, 4, 4)
    FIELD(REG_CQVERSION, EMMCVERSIONSUFFIX, 0, 4)
REG32(REG_CQCAPABILITIES, 0x04)
    FIELD(REG_CQCAPABILITIES, CORECFG_CQFMUL, 12, 4)
    FIELD(REG_CQCAPABILITIES, CORECFG_CQFVAL, 0, 10)
REG32(REG_CQCONFIG, 0x08)
    FIELD(REG_CQCONFIG, CQCFG_DCMDENABLE, 12, 1)
    FIELD(REG_CQCONFIG, CQCFG_TASKDESCSIZE, 8, 1)
    FIELD(REG_CQCONFIG, CQCFG_CQENABLE, 0, 1)
REG32(REG_CQCONTROL, 0x0c)
    FIELD(REG_CQCONTROL, CQCTRL_CLEARALLTASKS, 8, 1)
    FIELD(REG_CQCONTROL, CQCTRL_HALTBIT, 0, 1)
REG32(REG_CQINTRSTS, 0x10)
    FIELD(REG_CQINTRSTS, CQINTRSTS_TASKERROR, 4, 1)
    FIELD(REG_CQINTRSTS, CQINTRSTS_TASKCLEARED, 3, 1)
    FIELD(REG_CQINTRSTS, CQINTRSTS_RESPERRDET, 2, 1)
    FIELD(REG_CQINTRSTS, CQINTRSTS_TASKCOMPLETE, 1, 1)
    FIELD(REG_CQINTRSTS, CQINTRSTS_HALTCOMPLETE, 0, 1)
REG32(REG_CQINTRSTSENA, 0x14)
    FIELD(REG_CQINTRSTSENA, REG_CQINTRSTSENA4, 4, 1)
    FIELD(REG_CQINTRSTSENA, REG_CQINTRSTSENA3, 3, 1)
    FIELD(REG_CQINTRSTSENA, REG_CQINTRSTSENA2, 2, 1)
    FIELD(REG_CQINTRSTSENA, REG_CQINTRSTSENA1, 1, 1)
    FIELD(REG_CQINTRSTSENA, REG_CQINTRSTSENA0, 0, 1)
REG32(REG_CQINTRSIGENA, 0x18)
    FIELD(REG_CQINTRSIGENA, CQINTRSIG_ENABLEREG4, 4, 1)
    FIELD(REG_CQINTRSIGENA, CQINTRSIG_ENABLEREG3, 3, 1)
    FIELD(REG_CQINTRSIGENA, CQINTRSIG_ENABLEREG2, 2, 1)
    FIELD(REG_CQINTRSIGENA, CQINTRSIG_ENABLEREG1, 1, 1)
    FIELD(REG_CQINTRSIGENA, CQINTRSIG_ENABLEREG0, 0, 1)
REG32(REG_CQINTRCOALESCING, 0x1c)
    FIELD(REG_CQINTRCOALESCING, CQINTRCOALESCING_ENABLE, 31, 1)
    FIELD(REG_CQINTRCOALESCING, SDHCCQCTRL_ICSTATUS, 20, 1)
    FIELD(REG_CQINTRCOALESCING, COUNTERANDTIMERRESET, 16, 1)
    FIELD(REG_CQINTRCOALESCING,
          INTERRUPTCOALESCINGCOUNTERTHRESHOLDWRITEENABLE, 15, 1)
    FIELD(REG_CQINTRCOALESCING, CQINTRCOALESCING_CTRTHRESHOLD, 8, 5)
    FIELD(REG_CQINTRCOALESCING,
          INTERRUPTCOALESCINGTIMEOUTVALUEWRITEENABLE, 7, 1)
    FIELD(REG_CQINTRCOALESCING, CQINTRCOALESCING_TIMEOUTVALUE, 0, 7)
REG32(REG_CQTDLBASEADDRESSLO, 0x20)
REG32(REG_CQTDLBASEADDRESSHI, 0x24)
REG32(REG_CQTASKDOORBELL, 0x28)
REG32(REG_CQTASKCPLNOTIF, 0x2c)
REG32(REG_CQDEVQUEUESTATUS, 0x30)
REG32(REG_CQDEVPENDINGTASKS, 0x34)
REG32(REG_CQTASKCLEAR, 0x38)
REG32(REG_CQSENDSTSCONFIG1, 0x40)
    FIELD(REG_CQSENDSTSCONFIG1, CQSENDSTS_BLKCNT, 16, 4)
    FIELD(REG_CQSENDSTSCONFIG1, CQSENDSTS_TIMER, 0, 16)
REG32(REG_CQSENDSTSCONFIG2, 0x44)
    FIELD(REG_CQSENDSTSCONFIG2, CQSENDSTS_RCA, 0, 16)
REG32(REG_CQDCMDRESPONSE, 0x48)
REG32(REG_CQRESPERRMASK, 0x50)
REG32(REG_CQTASKERRINFO, 0x54)
    FIELD(REG_CQTASKERRINFO, SDHCCQCTRL_DATERRVALID, 31, 1)
    FIELD(REG_CQTASKERRINFO, SDHCCQCTRL_DATERRTASKID, 24, 5)
    FIELD(REG_CQTASKERRINFO, SDHCCQCTRL_DATERRCMDINDEX, 16, 6)
    FIELD(REG_CQTASKERRINFO, SDHCCQCTRL_CMDERRVALID, 15, 1)
    FIELD(REG_CQTASKERRINFO, SDHCCQCTRL_CMDERRTASKID, 8, 5)
    FIELD(REG_CQTASKERRINFO, SDHCCQCTRL_CMDERRCMDINDEX, 0, 6)
REG32(REG_CQLASTCMDINDEX, 0x58)
    FIELD(REG_CQLASTCMDINDEX, SDHCCQCTRL_LASTCMDINDEX, 0, 6)
REG32(REG_CQLASTCMDRESPONSE, 0x5c)
REG32(REG_CQERRORTASKID, 0x60)
    FIELD(REG_CQERRORTASKID, SDHCCQCTRL_TASKERRID, 0, 5)
REG32(REG_PHYCTRLREGISTER1, 0x70)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_TESTCTRL_SIG, 24, 8)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_STRBSEL_SIG, 16, 8)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_OTAPDLYSEL_SIG, 12, 4)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_OTAPDLYENA_SIG, 8, 1)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_ITAPCHGWIN_SIG, 6, 1)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_ITAPDLYSEL_SIG, 1, 5)
    FIELD(REG_PHYCTRLREGISTER1, PHYCTRL_ITAPDLYENA_SIG, 0, 1)
REG32(REG_PHYCTRLREGISTER2, 0x74)
    FIELD(REG_PHYCTRLREGISTER2, PHYCTRL_CLKBUFSEL_SIG, 24, 3)
    FIELD(REG_PHYCTRLREGISTER2, PHYCTRL_SELDLYRXCLK_SIG, 17, 1)
    FIELD(REG_PHYCTRLREGISTER2, PHYCTRL_SELDLYTXCLK_SIG, 16, 1)
    FIELD(REG_PHYCTRLREGISTER2, TRIM_ICP_SIG, 8, 4)
    FIELD(REG_PHYCTRLREGISTER2, FREQ_SEL_SIG, 4, 3)
    FIELD(REG_PHYCTRLREGISTER2, DLL_RDY, 1, 1)
    FIELD(REG_PHYCTRLREGISTER2, EN_DLL_SIG, 0, 1)
REG32(REG_BISTCTRL, 0x78)
    FIELD(REG_BISTCTRL, PHYCTRL_BISTDONE, 16, 1)
    FIELD(REG_BISTCTRL, PHYCTRL_BISTMODE_SIG, 4, 4)
    FIELD(REG_BISTCTRL, PHYCTRL_BISTSTART_SIG, 1, 1)
    FIELD(REG_BISTCTRL, PHYCTRL_BISTENABLE_SIG, 0, 1)
REG32(REG_BISTSTATUS, 0x7c)

#define EMMC_R_MAX (R_REG_BISTSTATUS + 1)

typedef struct VersalNetEMMC {
    ZynqMPSDHCIState parent_obj;
    MemoryRegion iomem;

    uint32_t regs[EMMC_R_MAX];
    RegisterInfo regs_info[EMMC_R_MAX];
} VersalNetEMMC;

static void phyctrlreg2_postw(RegisterInfo *reg, uint64_t val)
{
    VersalNetEMMC *s = XLNX_VERSALNET_EMMC(reg->opaque);

    ARRAY_FIELD_DP32(s->regs, REG_PHYCTRLREGISTER2, DLL_RDY,
                     val & R_REG_PHYCTRLREGISTER2_EN_DLL_SIG_MASK);
}

static const RegisterAccessInfo vn_emmc_regs_info[] = {
    { .name = "REG_CQVERSION",  .addr = A_REG_CQVERSION,
        .reset = 0x510,
        .ro = 0xfff,
    },{ .name = "REG_CQCAPABILITIES",  .addr = A_REG_CQCAPABILITIES,
        .ro = 0xf3ff,
    },{ .name = "REG_CQCONFIG",  .addr = A_REG_CQCONFIG,
    },{ .name = "REG_CQCONTROL",  .addr = A_REG_CQCONTROL,
    },{ .name = "REG_CQINTRSTS",  .addr = A_REG_CQINTRSTS,
    },{ .name = "REG_CQINTRSTSENA",  .addr = A_REG_CQINTRSTSENA,
    },{ .name = "REG_CQINTRSIGENA",  .addr = A_REG_CQINTRSIGENA,
    },{ .name = "REG_CQINTRCOALESCING",  .addr = A_REG_CQINTRCOALESCING,
    },{ .name = "REG_CQTDLBASEADDRESSLO",  .addr = A_REG_CQTDLBASEADDRESSLO,
    },{ .name = "REG_CQTDLBASEADDRESSHI",  .addr = A_REG_CQTDLBASEADDRESSHI,
    },{ .name = "REG_CQTASKDOORBELL",  .addr = A_REG_CQTASKDOORBELL,
    },{ .name = "REG_CQTASKCPLNOTIF",  .addr = A_REG_CQTASKCPLNOTIF,
    },{ .name = "REG_CQDEVQUEUESTATUS",  .addr = A_REG_CQDEVQUEUESTATUS,
        .ro = 0xffffffff,
    },{ .name = "REG_CQDEVPENDINGTASKS",  .addr = A_REG_CQDEVPENDINGTASKS,
        .ro = 0xffffffff,
    },{ .name = "REG_CQTASKCLEAR",  .addr = A_REG_CQTASKCLEAR,
    },{ .name = "REG_CQSENDSTSCONFIG1",  .addr = A_REG_CQSENDSTSCONFIG1,
        .reset = 0x11000,
    },{ .name = "REG_CQSENDSTSCONFIG2",  .addr = A_REG_CQSENDSTSCONFIG2,
        .ro = 0xffff,
    },{ .name = "REG_CQDCMDRESPONSE",  .addr = A_REG_CQDCMDRESPONSE,
        .ro = 0xffffffff,
    },{ .name = "REG_CQRESPERRMASK",  .addr = A_REG_CQRESPERRMASK,
        .reset = 0xfdf9a080,
        .ro = 0xffffffff,
    },{ .name = "REG_CQTASKERRINFO",  .addr = A_REG_CQTASKERRINFO,
        .ro = 0x9f3f9f3f,
    },{ .name = "REG_CQLASTCMDINDEX",  .addr = A_REG_CQLASTCMDINDEX,
        .ro = 0x3f,
    },{ .name = "REG_CQLASTCMDRESPONSE",  .addr = A_REG_CQLASTCMDRESPONSE,
        .ro = 0xffffffff,
    },{ .name = "REG_CQERRORTASKID",  .addr = A_REG_CQERRORTASKID,
        .ro = 0x1f,
    },{ .name = "REG_PHYCTRLREGISTER1",  .addr = A_REG_PHYCTRLREGISTER1,
    },{ .name = "REG_PHYCTRLREGISTER2",  .addr = A_REG_PHYCTRLREGISTER2,
        .reset = 0x800 | R_REG_PHYCTRLREGISTER2_DLL_RDY_MASK,
        .post_write = phyctrlreg2_postw,
        .ro = 0x2,
    },{ .name = "REG_BISTCTRL",  .addr = A_REG_BISTCTRL,
        .ro = 0x10000,
    },{ .name = "REG_BISTSTATUS",  .addr = A_REG_BISTSTATUS,
        .ro = 0xffffffff,
    }
};

static void vn_emmc_reset_enter(Object *obj, ResetType type)
{
    VersalNetEMMC *s = XLNX_VERSALNET_EMMC(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static const MemoryRegionOps vn_emmc_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void vn_emmc_init(Object *obj)
{
    VersalNetEMMC *s = XLNX_VERSALNET_EMMC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XLNX_VERSALNET_EMMC,
                       EMMC_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), vn_emmc_regs_info,
                              ARRAY_SIZE(vn_emmc_regs_info),
                              s->regs_info, s->regs,
                              &vn_emmc_ops,
                              XLNX_VERSALNET_EMMC_ERR_DEBUG,
                              EMMC_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_vn_emmc = {
    .name = TYPE_XLNX_VERSALNET_EMMC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, VersalNetEMMC, EMMC_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void vn_emmc_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_vn_emmc;
    rc->phases.enter = vn_emmc_reset_enter;
}

static const TypeInfo vn_emmc_info = {
    .name          = TYPE_XLNX_VERSALNET_EMMC,
    .parent        = TYPE_ZYNQMP_SDHCI,
    .instance_size = sizeof(VersalNetEMMC),
    .class_init    = vn_emmc_class_init,
    .instance_init = vn_emmc_init,
};

static void vn_emmc_register_types(void)
{
    type_register_static(&vn_emmc_info);
}

type_init(vn_emmc_register_types)
