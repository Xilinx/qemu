/*
 * xlnx-lpd-gpv.c
 *
 *  Copyright (C) 2016 : GreenSocs
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"

#ifndef XLNX_LPD_GPV_ERR_DEBUG
#define XLNX_LPD_GPV_ERR_DEBUG 0
#endif

#define TYPE_XLNX_LPD_GPV "xlnx.lpd-gpv"

#define XLNX_LPD_GPV(obj) \
     OBJECT_CHECK(XlnxLPDGPVState, (obj), TYPE_XLNX_LPD_GPV)

REG32(PERIPH_ID_4, 0x01FD0)
REG32(PERIPH_ID_5, 0x01FD4)
REG32(PERIPH_ID_6, 0x01FD8)
REG32(PERIPH_ID_7, 0x01FDC)
REG32(PERIPH_ID_0, 0x01FE0)
REG32(PERIPH_ID_1, 0x01FE4)
REG32(PERIPH_ID_2, 0x01FE8)
REG32(PERIPH_ID_3, 0x01FEC)
REG32(COMP_ID_0, 0x01FF0)
REG32(COMP_ID_1, 0x01FF4)
REG32(COMP_ID_2, 0x01FF8)
REG32(COMP_ID_3, 0x01FFC)
REG32(INTLPD_OCM_FN_MOD_ISS_BM, 0x2008)
REG32(INTLPD_RPUS0_FN_MOD_ISS_BM, 0x05008)
REG32(INTLPD_RPUS1_FN_MOD_ISS_BM, 0x06008)
REG32(INTLPD_USB0S_FN_MOD_ISS_BM, 0x07008)
REG32(INTLPD_USB1S_FN_MOD_ISS_BM, 0x08008)
REG32(INTLPD_AFIFS2_FN_MOD_ISS_BM, 0x09008)
REG32(INTLPD_INTIOU_IB_FN_MOD_ISS_BM, 0x0A008)
REG32(INTLPD_INTIOU_IB_FN_MOD, 0x0A108)
REG32(SLAVE_11_IB_FN_MOD_ISS_BM,   0x0D008)
REG32(RPUM0_INTLPD_READ_QOS,   0x42100)
REG32(RPUM0_INTLPD_WRITE_QOS,   0x42104)
REG32(RPUM0_INTLPD_FN_MOD,   0x42108)
REG32(RPUM0_INTLPD_QOS_CTNL,   0x4210C)
REG32(RPUM0_INTLPD_MAX_OT,   0x42110)
REG32(RPUM0_INTLPD_MAX_COMB_OT,   0x42114)
REG32(RPUM0_INTLPD_AW_P,   0x42118)
REG32(RPUM0_INTLPD_AW_B,   0x4211C)
REG32(RPUM0_INTLPD_AW_R,   0x42120)
REG32(RPUM0_INTLPD_AR_P,   0x42124)
REG32(RPUM0_INTLPD_AR_B,   0x42128)
REG32(RPUM0_INTLPD_AR_R,   0x4212C)
REG32(RPUM1_INTLPD_READ_QOS,   0x43100)
REG32(RPUM1_INTLPD_WRITE_QOS,   0x43104)
REG32(RPUM1_INTLPD_FN_MOD,   0x43108)
REG32(RPUM1_INTLPD_QOS_CTNL,   0x4310C)
REG32(RPUM1_INTLPD_MAX_OT,   0x43110)
REG32(RPUM1_INTLPD_MAX_COMB_OT, 0x43114)
REG32(RPUM1_INTLPD_AW_P, 0x43118)
REG32(RPUM1_INTLPD_AW_B, 0x4311C)
REG32(RPUM1_INTLPD_AW_R, 0x43120)
REG32(RPUM1_INTLPD_AR_P,   0x43124)
REG32(RPUM1_INTLPD_AR_B,   0x43128)
REG32(RPUM1_INTLPD_AR_R,   0x4312C)
REG32(ADMAM_INTLPD_IB_FN_MOD2, 0x00044024)
REG32(ADMAM_INTLPD_IB_FN_MOD, 0x00044108)
REG32(ADMAM_INTLPD_IB_QOS_CNTL, 0x0004410C)
REG32(ADMAM_INTLPD_IB_MAX_OT, 0x00044110)
REG32(ADMAM_INTLPD_IB_MAX_COMB_OT, 0x00044114)
REG32(ADMAM_INTLPD_IB_AW_P, 0x00044118)
REG32(ADMAM_INTLPD_IB_AW_B, 0x0004411C)
REG32(ADMAM_INTLPD_IB_AW_R, 0x00044120)
REG32(ADMAM_INTLPD_IB_AR_P, 0x00044124)
REG32(ADMAM_INTLPD_IB_AR_B, 0x00044128)
REG32(ADMAM_INTLPD_IB_AR_R, 0x0004412C)
REG32(AFIFM6M_INTLPD_IB_FN_MOD, 0x00045108)
REG32(AFIFM6M_INTLPD_IB_QOS_CNTL, 0x0004510C)
REG32(AFIFM6M_INTLPD_IB_MAX_OT, 0x00045110)
REG32(AFIFM6M_INTLPD_IB_MAX_COMB_OT, 0x00045114)
REG32(AFIFM6M_INTLPD_IB_AW_P, 0x00045118)
REG32(AFIFM6M_INTLPD_IB_AW_B, 0x0004511C)
REG32(AFIFM6M_INTLPD_IB_AW_R, 0x00045120)
REG32(AFIFM6M_INTLPD_IB_AR_P, 0x00045124)
REG32(AFIFM6M_INTLPD_IB_AR_B, 0x00045128)
REG32(AFIFM6M_INTLPD_IB_AR_R, 0x0004512C)
REG32(DAP_INTLPD_IB_FN_MOD2, 0x00047024)
REG32(DAP_INTLPD_IB_READ_QOS, 0x00047100)
REG32(DAP_INTLPD_IB_WRITE_QOS, 0x00047104)
REG32(DAP_INTLPD_IB_FN_MOD, 0x00047108)
REG32(DAP_INTLPD_IB_QOS_CNTL, 0x0004710C)
REG32(DAP_INTLPD_IB_MAX_OT, 0x00047110)
REG32(DAP_INTLPD_IB_MAX_COMB_OT, 0x00047114)
REG32(DAP_INTLPD_IB_AW_P, 0x00047118)
REG32(DAP_INTLPD_IB_AW_B, 0x0004711C)
REG32(DAP_INTLPD_IB_AW_R, 0x00047120)
REG32(DAP_INTLPD_IB_AR_P, 0x00047124)
REG32(DAP_INTLPD_IB_AR_B, 0x00047128)
REG32(DAP_INTLPD_IB_AR_R, 0x0004712C)
REG32(USB0M_INTLPD_IB_READ_QOS, 0x00048100)
REG32(USB0M_INTLPD_IB_WRITE_QOS, 0x00048104)
REG32(USB0M_INTLPD_IB_FN_MOD, 0x00048108)
REG32(USB0M_INTLPD_IB_QOS_CNTL, 0x0004810C)
REG32(USB0M_INTLPD_IB_MAX_OT, 0x00048110)
REG32(USB0M_INTLPD_IB_MAX_COMB_OT, 0x00048114)
REG32(USB0M_INTLPD_IB_AW_P, 0x00048118)
REG32(USB0M_INTLPD_IB_AW_B, 0x0004811C)
REG32(USB0M_INTLPD_IB_AW_R, 0x00048120)
REG32(USB0M_INTLPD_IB_AR_P, 0x00048124)
REG32(USB0M_INTLPD_IB_AR_B, 0x00048128)
REG32(USB0M_INTLPD_IB_AR_R, 0x0004812C)
REG32(USB1M_INTLPD_IB_READ_QOS, 0x00049100)
REG32(USB1M_INTLPD_IB_WRITE_QOS, 0x00049104)
REG32(USB1M_INTLPD_IB_FN_MOD, 0x00049108)
REG32(USB1M_INTLPD_IB_QOS_CNTL, 0x0004910C)
REG32(USB1M_INTLPD_IB_MAX_OT, 0x00049110)
REG32(USB1M_INTLPD_IB_MAX_COMB_OT, 0x00049114)
REG32(USB1M_INTLPD_IB_AW_P, 0x00049118)
REG32(USB1M_INTLPD_IB_AW_B, 0x0004911C)
REG32(USB1M_INTLPD_IB_AW_R, 0x00049120)
REG32(USB1M_INTLPD_IB_AR_P, 0x00049124)
REG32(USB1M_INTLPD_IB_AR_B, 0x00049128)
REG32(USB1M_INTLPD_IB_AR_R, 0x0004912C)
REG32(INTIOU_INTLPD_IB_FN_MOD, 0x0004A108)
REG32(INTIOU_INTLPD_IB_QOS_CNTL, 0x0004A10C)
REG32(INTIOU_INTLPD_IB_MAX_OT, 0x0004A110)
REG32(INTIOU_INTLPD_IB_MAX_COMB_OT, 0x0004A114)
REG32(INTIOU_INTLPD_IB_AW_P, 0x0004A118)
REG32(INTIOU_INTLPD_IB_AW_B, 0x0004A11C)
REG32(INTIOU_INTLPD_IB_AW_R, 0x0004A120)
REG32(INTIOU_INTLPD_IB_AR_P, 0x0004A124)
REG32(INTIOU_INTLPD_IB_AR_B, 0x0004A128)
REG32(INTIOU_INTLPD_IB_AR_R, 0x0004A12C)
REG32(INTCSUPMU_INTLPD_IB_FN_MOD, 0x0004B108)
REG32(INTCSUPMU_INTLPD_IB_QOS_CNTL, 0x0004B10C)
REG32(INTCSUPMU_INTLPD_IB_MAX_OT, 0x0004B110)
REG32(INTCSUPMU_INTLPD_IB_MAX_COMB_OT, 0x0004B114)
REG32(INTCSUPMU_INTLPD_IB_AW_P, 0x0004B118)
REG32(INTCSUPMU_INTLPD_IB_AW_B, 0x0004B11C)
REG32(INTCSUPMU_INTLPD_IB_AW_R, 0x0004B120)
REG32(INTCSUPMU_INTLPD_IB_AR_P, 0x0004B124)
REG32(INTCSUPMU_INTLPD_IB_AR_B, 0x0004B128)
REG32(INTCSUPMU_INTLPD_IB_AR_R, 0x0004B12C)
REG32(INTLPDINBOUND_INTLPDMAIN_FN_MOD, 0x0004C108)
REG32(INTLPDINBOUND_INTLPDMAIN_QOS_CNTL, 0x0004C10C)
REG32(INTLPDINBOUND_INTLPDMAIN_MAX_OT, 0x0004C110)
REG32(INTLPDINBOUND_INTLPDMAIN_MAX_COMB_OT, 0x0004C114)
REG32(INTLPDINBOUND_INTLPDMAIN_AW_P, 0x0004C118)
REG32(INTLPDINBOUND_INTLPDMAIN_AW_B, 0x0004C11C)
REG32(INTLPDINBOUND_INTLPDMAIN_AW_R, 0x0004C120)
REG32(INTLPDINBOUND_INTLPDMAIN_AR_P, 0x0004C124)
REG32(INTLPDINBOUND_INTLPDMAIN_AR_B, 0x0004C128)
REG32(INTLPDINBOUND_INTLPDMAIN_AR_R, 0x0004C12C)
REG32(INTFPD_INTLPDOCM_FN_MOD, 0x0004D108)
REG32(INTFPD_INTLPDOCM_QOS_CNTL, 0x0004D10C)
REG32(INTFPD_INTLPDOCM_MAX_OT, 0x0004D110)
REG32(INTFPD_INTLPDOCM_MAX_COMB_OT, 0x0004D114)
REG32(INTFPD_INTLPDOCM_AW_P, 0x0004D118)
REG32(INTFPD_INTLPDOCM_AW_B, 0x0004D11C)
REG32(INTFPD_INTLPDOCM_AW_R, 0x0004D120)
REG32(INTFPD_INTLPDOCM_AR_P, 0x0004D124)
REG32(INTFPD_INTLPDOCM_AR_B, 0x0004D128)
REG32(INTFPD_INTLPDOCM_AR_R, 0x0004D12C)
REG32(IB9_FN_MOD_ISS_BM, 0x000C2008)
REG32(IB9_FN_MOD, 0x000C2108)
REG32(IB5_FN_MOD_ISS_BM, 0x000C3008)
REG32(IB5_FN_MOD2, 0x000C3024)
REG32(IB5_FN_MOD, 0x000C3108)
REG32(IB5_QOS_CNTL, 0x000C310C)
REG32(IB5_MAX_OT, 0x000C3110)
REG32(IB5_MAX_COMB_OT, 0x000C3114)
REG32(IB5_AW_P, 0x000C3118)
REG32(IB5_AW_B, 0x000C311C)
REG32(IB5_AW_R, 0x000C3120)
REG32(IB5_AR_P, 0x000C3124)
REG32(IB5_AR_B, 0x000C3128)
REG32(IB5_AR_R, 0x000C312C)
REG32(IB6_FN_MOD_ISS_BM, 0x000C4008)
REG32(IB6_FN_MOD2, 0x000C4024)
REG32(IB6_FN_MOD, 0x000C4108)
REG32(IB6_QOS_CNTL, 0x000C410C)
REG32(IB6_MAX_OT, 0x000C4110)
REG32(IB6_MAX_COMB_OT, 0x000C4114)
REG32(IB6_AW_P, 0x000C4118)
REG32(IB6_AW_B, 0x000C411C)
REG32(IB6_AW_R, 0x000C4120)
REG32(IB6_AR_P, 0x000C4124)
REG32(IB6_AR_B, 0x000C4128)
REG32(IB6_AR_R, 0x000C412C)
REG32(IB8_FN_MOD_ISS_BM, 0x000C5008)
REG32(IB8_FN_MOD2, 0x000C5024)
REG32(IB8_FN_MOD, 0x000C5108)
REG32(IB8_QOS_CNTL, 0x000C510C)
REG32(IB8_MAX_OT, 0x000C5110)
REG32(IB8_MAX_COMB_OT, 0x000C5114)
REG32(IB8_AW_P, 0x000C5118)
REG32(IB8_AW_B, 0x000C511C)
REG32(IB8_AW_R, 0x000C5120)
REG32(IB8_AR_P, 0x000C5124)
REG32(IB8_AR_B, 0x000C5128)
REG32(IB8_AR_R, 0x000C512C)
REG32(IB0_FN_MOD_ISS_BM, 0x000C6008)
REG32(IB0_FN_MOD2, 0x000C6024)
REG32(IB0_FN_MOD, 0x000C6108)
REG32(IB0_QOS_CNTL, 0x000C610C)
REG32(IB0_MAX_OT, 0x000C6110)
REG32(IB0_MAX_COMB_OT, 0x000C6114)
REG32(IB0_AW_P, 0x000C6118)
REG32(IB0_AW_B, 0x000C611C)
REG32(IB0_AW_R, 0x000C6120)
REG32(IB0_AR_P, 0x000C6124)
REG32(IB0_AR_B, 0x000C6128)
REG32(IB0_AR_R, 0x000C612C)
REG32(IB11_FN_MOD_ISS_BM, 0x000C7008)
REG32(IB11_FN_MOD2, 0x000C7024)
REG32(IB11_FN_MOD, 0x000C7108)
REG32(IB11_QOS_CNTL, 0x000C710C)
REG32(IB11_MAX_OT, 0x000C7110)
REG32(IB11_MAX_COMB_OT, 0x000C7114)
REG32(IB11_AW_P, 0x000C7118)
REG32(IB11_AW_B, 0x000C711C)
REG32(IB11_AW_R, 0x000C7120)
REG32(IB11_AR_P, 0x000C7124)
REG32(IB11_AR_B, 0x000C7128)
REG32(IB11_AR_R, 0x000C712C)
REG32(IB12_FN_MOD_ISS_BM, 0x000C8008)
REG32(IB12_FN_MOD2, 0x000C8024)
REG32(IB12_FN_MOD, 0x000C8108)
REG32(IB12_QOS_CNTL, 0x000C810C)
REG32(IB12_MAX_OT, 0x000C8110)
REG32(IB12_MAX_COMB_OT, 0x000C8114)
REG32(IB12_AW_P, 0x000C8118)
REG32(IB12_AW_B, 0x000C811C)
REG32(IB12_AW_R, 0x000C8120)
REG32(IB12_AR_P, 0x000C8124)
REG32(IB12_AR_B, 0x000C8128)
REG32(IB12_AR_R, 0x000C812C)

#define LPD_GPV_R_MAX (R_IB12_AR_R + 1)

typedef struct XlnxLPDGPVState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_isr;

    uint32_t regs[LPD_GPV_R_MAX];
    RegisterInfo regs_info[LPD_GPV_R_MAX];
    const char *prefix;
} XlnxLPDGPVState;

static RegisterAccessInfo lpd_gpv_regs_info[] = {
    {     .name = "PERIPH_ID_4", .addr = A_PERIPH_ID_4,
          .reset = 0x00000004, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_5", .addr = A_PERIPH_ID_5,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_6", .addr = A_PERIPH_ID_6,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_7", .addr = A_PERIPH_ID_7,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_0", .addr = A_PERIPH_ID_0,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_1", .addr = A_PERIPH_ID_1,
          .reset = 0x000000B4, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_2", .addr = A_PERIPH_ID_2,
          .reset = 0x0000002B, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_3", .addr = A_PERIPH_ID_3,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_0", .addr = A_COMP_ID_0,
          .reset = 0x0000000D, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_1", .addr = A_COMP_ID_1,
          .reset = 0x000000F0, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_2", .addr = A_COMP_ID_2,
          .reset = 0x00000005, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_3", .addr = A_COMP_ID_3,
          .reset = 0x000000B1, .ro = 0xFFFFFFFF,
    },{   .name = "INTLPD_OCM_FN_MOD_ISS_BM",
          .addr = A_INTLPD_OCM_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_RPUS0_FN_MOD_ISS_BM",
          .addr = A_INTLPD_RPUS0_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_RPUS1_FN_MOD_ISS_BM",
          .addr = A_INTLPD_RPUS1_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_USB0S_FN_MOD_ISS_BM",
          .addr = A_INTLPD_USB0S_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_USB1S_FN_MOD_ISS_BM",
          .addr = A_INTLPD_USB1S_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_AFIFS2_FN_MOD_ISS_BM",
          .addr = A_INTLPD_AFIFS2_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_INTIOU_IB_FN_MOD_ISS_BM",
          .addr = A_INTLPD_INTIOU_IB_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_INTIOU_IB_FN_MOD",
          .addr = A_INTLPD_INTIOU_IB_FN_MOD,
          .reset = 0,
    },{   .name = "SLAVE_11_IB_FN_MOD_ISS_BM",
          .addr = A_SLAVE_11_IB_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_READ_QOS",
          .addr = A_RPUM0_INTLPD_READ_QOS,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_WRITE_QOS",
          .addr = A_RPUM0_INTLPD_WRITE_QOS,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_FN_MOD",
          .addr = A_RPUM0_INTLPD_FN_MOD,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_QOS_CTNL",
          .addr = A_RPUM0_INTLPD_QOS_CTNL,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_MAX_OT", .addr = A_RPUM0_INTLPD_MAX_OT,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_MAX_COMB_OT",
          .addr = A_RPUM0_INTLPD_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AW_P", .addr = A_RPUM0_INTLPD_AW_P,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AW_B", .addr = A_RPUM0_INTLPD_AW_B,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AW_R", .addr = A_RPUM0_INTLPD_AW_R,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AR_P", .addr = A_RPUM0_INTLPD_AR_P,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AR_B", .addr = A_RPUM0_INTLPD_AR_B,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AR_R", .addr = A_RPUM0_INTLPD_AR_R,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_READ_QOS",
          .addr = A_RPUM1_INTLPD_READ_QOS,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_WRITE_QOS",
          .addr = A_RPUM1_INTLPD_WRITE_QOS,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_FN_MOD", .addr = A_RPUM1_INTLPD_FN_MOD,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_QOS_CTNL",
          .addr = A_RPUM1_INTLPD_QOS_CTNL,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_MAX_OT", .addr = A_RPUM1_INTLPD_MAX_OT,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_MAX_COMB_OT",
          .addr = A_RPUM1_INTLPD_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AW_P", .addr = A_RPUM1_INTLPD_AW_P,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AW_B", .addr = A_RPUM1_INTLPD_AW_B,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AW_R", .addr = A_RPUM1_INTLPD_AW_R,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AR_P", .addr = A_RPUM1_INTLPD_AR_P,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AR_B", .addr = A_RPUM1_INTLPD_AR_B,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AR_R", .addr = A_RPUM1_INTLPD_AR_R,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_FN_MOD2",
          .addr = A_ADMAM_INTLPD_IB_FN_MOD2,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_FN_MOD",
          .addr = A_ADMAM_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_QOS_CNTL",
          .addr = A_ADMAM_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_MAX_OT",
          .addr = A_ADMAM_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_MAX_COMB_OT",
          .addr = A_ADMAM_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AW_P", .addr = A_ADMAM_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AW_B", .addr = A_ADMAM_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AW_R", .addr = A_ADMAM_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AR_P", .addr = A_ADMAM_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AR_B", .addr = A_ADMAM_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AR_R", .addr = A_ADMAM_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_FN_MOD",
          .addr = A_AFIFM6M_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_QOS_CNTL",
          .addr = A_AFIFM6M_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_MAX_OT",
          .addr = A_AFIFM6M_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_MAX_COMB_OT",
          .addr = A_AFIFM6M_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AW_P",
          .addr = A_AFIFM6M_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AW_B",
          .addr = A_AFIFM6M_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AW_R",
          .addr = A_AFIFM6M_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AR_P",
          .addr = A_AFIFM6M_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AR_B",
          .addr = A_AFIFM6M_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AR_R",
          .addr = A_AFIFM6M_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_FN_MOD2",
          .addr = A_DAP_INTLPD_IB_FN_MOD2,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_READ_QOS",
          .addr = A_DAP_INTLPD_IB_READ_QOS,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_WRITE_QOS",
          .addr = A_DAP_INTLPD_IB_WRITE_QOS,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_FN_MOD", .addr = A_DAP_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_QOS_CNTL",
          .addr = A_DAP_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_MAX_OT", .addr = A_DAP_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_MAX_COMB_OT",
          .addr = A_DAP_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AW_P", .addr = A_DAP_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AW_B", .addr = A_DAP_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AW_R", .addr = A_DAP_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AR_P", .addr = A_DAP_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AR_B", .addr = A_DAP_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AR_R", .addr = A_DAP_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_READ_QOS",
          .addr = A_USB0M_INTLPD_IB_READ_QOS,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_WRITE_QOS",
          .addr = A_USB0M_INTLPD_IB_WRITE_QOS,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_FN_MOD",
          .addr = A_USB0M_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_QOS_CNTL",
          .addr = A_USB0M_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_MAX_OT",
          .addr = A_USB0M_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_MAX_COMB_OT",
          .addr = A_USB0M_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AW_P", .addr = A_USB0M_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AW_B", .addr = A_USB0M_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AW_R", .addr = A_USB0M_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AR_P", .addr = A_USB0M_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AR_B", .addr = A_USB0M_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AR_R", .addr = A_USB0M_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_READ_QOS",
          .addr = A_USB1M_INTLPD_IB_READ_QOS,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_WRITE_QOS",
          .addr = A_USB1M_INTLPD_IB_WRITE_QOS,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_FN_MOD",
          .addr = A_USB1M_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_QOS_CNTL",
          .addr = A_USB1M_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_MAX_OT",
          .addr = A_USB1M_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_MAX_COMB_OT",
          .addr = A_USB1M_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AW_P", .addr = A_USB1M_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AW_B", .addr = A_USB1M_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AW_R", .addr = A_USB1M_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AR_P", .addr = A_USB1M_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AR_B", .addr = A_USB1M_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AR_R", .addr = A_USB1M_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_FN_MOD",
          .addr = A_INTIOU_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_QOS_CNTL",
          .addr = A_INTIOU_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_MAX_OT",
          .addr = A_INTIOU_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_MAX_COMB_OT",
          .addr = A_INTIOU_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AW_P",
          .addr = A_INTIOU_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AW_B",
          .addr = A_INTIOU_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AW_R",
          .addr = A_INTIOU_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AR_P",
          .addr = A_INTIOU_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AR_B",
          .addr = A_INTIOU_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AR_R",
          .addr = A_INTIOU_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_FN_MOD",
          .addr = A_INTCSUPMU_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_QOS_CNTL",
          .addr = A_INTCSUPMU_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_MAX_OT",
          .addr = A_INTCSUPMU_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_MAX_COMB_OT",
          .addr = A_INTCSUPMU_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AW_P",
          .addr = A_INTCSUPMU_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AW_B",
          .addr = A_INTCSUPMU_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AW_R",
          .addr = A_INTCSUPMU_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AR_P",
          .addr = A_INTCSUPMU_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AR_B",
          .addr = A_INTCSUPMU_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AR_R",
          .addr = A_INTCSUPMU_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_FN_MOD",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_FN_MOD,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_QOS_CNTL",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_MAX_OT",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_MAX_OT,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_MAX_COMB_OT",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AW_P",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_AW_P,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AW_B",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_AW_B,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AW_R",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_AW_R,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AR_P",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_AR_P,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AR_B",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_AR_B,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AR_R",
          .addr = A_INTLPDINBOUND_INTLPDMAIN_AR_R,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_FN_MOD",
          .addr = A_INTFPD_INTLPDOCM_FN_MOD,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_QOS_CNTL",
          .addr = A_INTFPD_INTLPDOCM_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_MAX_OT",
          .addr = A_INTFPD_INTLPDOCM_MAX_OT,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_MAX_COMB_OT",
          .addr = A_INTFPD_INTLPDOCM_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AW_P",
          .addr = A_INTFPD_INTLPDOCM_AW_P,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AW_B",
          .addr = A_INTFPD_INTLPDOCM_AW_B,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AW_R",
          .addr = A_INTFPD_INTLPDOCM_AW_R,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AR_P",
          .addr = A_INTFPD_INTLPDOCM_AR_P,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AR_B",
          .addr = A_INTFPD_INTLPDOCM_AR_B,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AR_R",
          .addr = A_INTFPD_INTLPDOCM_AR_R,
          .reset = 0,
    },{   .name = "IB9_FN_MOD_ISS_BM", .addr = A_IB9_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB9_FN_MOD", .addr = A_IB9_FN_MOD,
          .reset = 0,
    },{   .name = "IB5_FN_MOD_ISS_BM", .addr = A_IB5_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB5_FN_MOD2", .addr = A_IB5_FN_MOD2,
          .reset = 0,
    },{   .name = "IB5_FN_MOD", .addr = A_IB5_FN_MOD,
          .reset = 0,
    },{   .name = "IB5_QOS_CNTL", .addr = A_IB5_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB5_MAX_OT", .addr = A_IB5_MAX_OT,
          .reset = 0,
    },{   .name = "IB5_MAX_COMB_OT", .addr = A_IB5_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB5_AW_P", .addr = A_IB5_AW_P,
          .reset = 0,
    },{   .name = "IB5_AW_B", .addr = A_IB5_AW_B,
          .reset = 0,
    },{   .name = "IB5_AW_R", .addr = A_IB5_AW_R,
          .reset = 0,
    },{   .name = "IB5_AR_P", .addr = A_IB5_AR_P,
          .reset = 0,
    },{   .name = "IB5_AR_B", .addr = A_IB5_AR_B,
          .reset = 0,
    },{   .name = "IB5_AR_R", .addr = A_IB5_AR_R,
          .reset = 0,
    },{   .name = "IB6_FN_MOD_ISS_BM", .addr = A_IB6_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB6_FN_MOD2", .addr = A_IB6_FN_MOD2,
          .reset = 0,
    },{   .name = "IB6_FN_MOD", .addr = A_IB6_FN_MOD,
          .reset = 0,
    },{   .name = "IB6_QOS_CNTL", .addr = A_IB6_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB6_MAX_OT", .addr = A_IB6_MAX_OT,
          .reset = 0,
    },{   .name = "IB6_MAX_COMB_OT", .addr = A_IB6_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB6_AW_P", .addr = A_IB6_AW_P,
          .reset = 0,
    },{   .name = "IB6_AW_B", .addr = A_IB6_AW_B,
          .reset = 0,
    },{   .name = "IB6_AW_R", .addr = A_IB6_AW_R,
          .reset = 0,
    },{   .name = "IB6_AR_P", .addr = A_IB6_AR_P,
          .reset = 0,
    },{   .name = "IB6_AR_B", .addr = A_IB6_AR_B,
          .reset = 0,
    },{   .name = "IB6_AR_R", .addr = A_IB6_AR_R,
          .reset = 0,
    },{   .name = "IB8_FN_MOD_ISS_BM", .addr = A_IB8_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB8_FN_MOD2", .addr = A_IB8_FN_MOD2,
          .reset = 0,
    },{   .name = "IB8_FN_MOD", .addr = A_IB8_FN_MOD,
          .reset = 0,
    },{   .name = "IB8_QOS_CNTL", .addr = A_IB8_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB8_MAX_OT", .addr = A_IB8_MAX_OT,
          .reset = 0,
    },{   .name = "IB8_MAX_COMB_OT", .addr = A_IB8_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB8_AW_P", .addr = A_IB8_AW_P,
          .reset = 0,
    },{   .name = "IB8_AW_B", .addr = A_IB8_AW_B,
          .reset = 0,
    },{   .name = "IB8_AW_R", .addr = A_IB8_AW_R,
          .reset = 0,
    },{   .name = "IB8_AR_P", .addr = A_IB8_AR_P,
          .reset = 0,
    },{   .name = "IB8_AR_B", .addr = A_IB8_AR_B,
          .reset = 0,
    },{   .name = "IB8_AR_R", .addr = A_IB8_AR_R,
          .reset = 0,
    },{   .name = "IB0_FN_MOD_ISS_BM", .addr = A_IB0_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB0_FN_MOD2", .addr = A_IB0_FN_MOD2,
          .reset = 0,
    },{   .name = "IB0_FN_MOD", .addr = A_IB0_FN_MOD,
          .reset = 0,
    },{   .name = "IB0_QOS_CNTL", .addr = A_IB0_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB0_MAX_OT", .addr = A_IB0_MAX_OT,
          .reset = 0,
    },{   .name = "IB0_MAX_COMB_OT", .addr = A_IB0_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB0_AW_P", .addr = A_IB0_AW_P,
          .reset = 0,
    },{   .name = "IB0_AW_B", .addr = A_IB0_AW_B,
          .reset = 0,
    },{   .name = "IB0_AW_R", .addr = A_IB0_AW_R,
          .reset = 0,
    },{   .name = "IB0_AR_P", .addr = A_IB0_AR_P,
          .reset = 0,
    },{   .name = "IB0_AR_B", .addr = A_IB0_AR_B,
          .reset = 0,
    },{   .name = "IB0_AR_R", .addr = A_IB0_AR_R,
          .reset = 0,
    },{   .name = "IB11_FN_MOD_ISS_BM", .addr = A_IB11_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB11_FN_MOD2", .addr = A_IB11_FN_MOD2,
          .reset = 0,
    },{   .name = "IB11_FN_MOD", .addr = A_IB11_FN_MOD,
          .reset = 0,
    },{   .name = "IB11_QOS_CNTL", .addr = A_IB11_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB11_MAX_OT", .addr = A_IB11_MAX_OT,
          .reset = 0,
    },{   .name = "IB11_MAX_COMB_OT", .addr = A_IB11_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB11_AW_P", .addr = A_IB11_AW_P,
          .reset = 0,
    },{   .name = "IB11_AW_B", .addr = A_IB11_AW_B,
          .reset = 0,
    },{   .name = "IB11_AW_R", .addr = A_IB11_AW_R,
          .reset = 0,
    },{   .name = "IB11_AR_P", .addr = A_IB11_AR_P,
          .reset = 0,
    },{   .name = "IB11_AR_B", .addr = A_IB11_AR_B,
          .reset = 0,
    },{   .name = "IB11_AR_R", .addr = A_IB11_AR_R,
          .reset = 0,
    },{   .name = "IB12_FN_MOD_ISS_BM", .addr = A_IB12_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB12_FN_MOD2", .addr = A_IB12_FN_MOD2,
          .reset = 0,
    },{   .name = "IB12_FN_MOD", .addr = A_IB12_FN_MOD,
          .reset = 0,
    },{   .name = "IB12_QOS_CNTL", .addr = A_IB12_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB12_MAX_OT", .addr = A_IB12_MAX_OT,
          .reset = 0,
    },{   .name = "IB12_MAX_COMB_OT", .addr = A_IB12_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB12_AW_P", .addr = A_IB12_AW_P,
          .reset = 0,
    },{   .name = "IB12_AW_B", .addr = A_IB12_AW_B,
          .reset = 0,
    },{   .name = "IB12_AW_R", .addr = A_IB12_AW_R,
          .reset = 0,
    },{   .name = "IB12_AR_P", .addr = A_IB12_AR_P,
          .reset = 0,
    },{   .name = "IB12_AR_B", .addr = A_IB12_AR_B,
          .reset = 0,
    },{   .name = "IB12_AR_R", .addr = A_IB12_AR_R,
          .reset = 0,
    },
};

static void lpd_gpv_reset(DeviceState *dev)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static uint64_t lpd_gpv_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(opaque);
    RegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: read from %" HWADDR_PRIx "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr);
        return 0;
    }
    return register_read(r, ~0, s->prefix, XLNX_LPD_GPV_ERR_DEBUG);
}

static void lpd_gpv_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(opaque);
    RegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: write to %" HWADDR_PRIx "=%" PRIx64 "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr, value);
        return;
    }
    register_write(r, value, ~0, s->prefix, XLNX_LPD_GPV_ERR_DEBUG);
}

static const MemoryRegionOps lpd_gpv_ops = {
    .read = lpd_gpv_read,
    .write = lpd_gpv_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void lpd_gpv_init(Object *obj)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    s->prefix = object_get_canonical_path(obj);
    memory_region_init_io(&s->iomem, obj, &lpd_gpv_ops, s,
                          TYPE_XLNX_LPD_GPV, LPD_GPV_R_MAX * 4);
    reg_array =
     register_init_block32(DEVICE(obj), lpd_gpv_regs_info,
                           ARRAY_SIZE(lpd_gpv_regs_info),
                           s->regs_info, s->regs,
                           &lpd_gpv_ops,
                           XLNX_LPD_GPV_ERR_DEBUG,
                           LPD_GPV_R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0x0, &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_lpd_gpv = {
    .name = TYPE_XLNX_LPD_GPV,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxLPDGPVState, LPD_GPV_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void lpd_gpv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = lpd_gpv_reset;
    dc->vmsd = &vmstate_lpd_gpv;
}

static const TypeInfo lpd_gpv_info = {
    .name          = TYPE_XLNX_LPD_GPV,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxLPDGPVState),
    .class_init    = lpd_gpv_class_init,
    .instance_init = lpd_gpv_init,
};

static void lpd_gpv_register_types(void)
{
    type_register_static(&lpd_gpv_info);
}

type_init(lpd_gpv_register_types)
