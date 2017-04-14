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
#include "hw/register-dep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"

#ifndef XLNX_LPD_GPV_ERR_DEBUG
#define XLNX_LPD_GPV_ERR_DEBUG 0
#endif

#define TYPE_XLNX_LPD_GPV "xlnx.lpd-gpv"

#define XLNX_LPD_GPV(obj) \
     OBJECT_CHECK(XlnxLPDGPVState, (obj), TYPE_XLNX_LPD_GPV)

DEP_REG32(PERIPH_ID_4, 0x01FD0)
DEP_REG32(PERIPH_ID_5, 0x01FD4)
DEP_REG32(PERIPH_ID_6, 0x01FD8)
DEP_REG32(PERIPH_ID_7, 0x01FDC)
DEP_REG32(PERIPH_ID_0, 0x01FE0)
DEP_REG32(PERIPH_ID_1, 0x01FE4)
DEP_REG32(PERIPH_ID_2, 0x01FE8)
DEP_REG32(PERIPH_ID_3, 0x01FEC)
DEP_REG32(COMP_ID_0, 0x01FF0)
DEP_REG32(COMP_ID_1, 0x01FF4)
DEP_REG32(COMP_ID_2, 0x01FF8)
DEP_REG32(COMP_ID_3, 0x01FFC)
DEP_REG32(INTLPD_OCM_FN_MOD_ISS_BM, 0x2008)
DEP_REG32(INTLPD_RPUS0_FN_MOD_ISS_BM, 0x05008)
DEP_REG32(INTLPD_RPUS1_FN_MOD_ISS_BM, 0x06008)
DEP_REG32(INTLPD_USB0S_FN_MOD_ISS_BM, 0x07008)
DEP_REG32(INTLPD_USB1S_FN_MOD_ISS_BM, 0x08008)
DEP_REG32(INTLPD_AFIFS2_FN_MOD_ISS_BM, 0x09008)
DEP_REG32(INTLPD_INTIOU_IB_FN_MOD_ISS_BM, 0x0A008)
DEP_REG32(INTLPD_INTIOU_IB_FN_MOD, 0x0A108)
DEP_REG32(SLAVE_11_IB_FN_MOD_ISS_BM,   0x0D008)
DEP_REG32(RPUM0_INTLPD_READ_QOS,   0x42100)
DEP_REG32(RPUM0_INTLPD_WRITE_QOS,   0x42104)
DEP_REG32(RPUM0_INTLPD_FN_MOD,   0x42108)
DEP_REG32(RPUM0_INTLPD_QOS_CTNL,   0x4210C)
DEP_REG32(RPUM0_INTLPD_MAX_OT,   0x42110)
DEP_REG32(RPUM0_INTLPD_MAX_COMB_OT,   0x42114)
DEP_REG32(RPUM0_INTLPD_AW_P,   0x42118)
DEP_REG32(RPUM0_INTLPD_AW_B,   0x4211C)
DEP_REG32(RPUM0_INTLPD_AW_R,   0x42120)
DEP_REG32(RPUM0_INTLPD_AR_P,   0x42124)
DEP_REG32(RPUM0_INTLPD_AR_B,   0x42128)
DEP_REG32(RPUM0_INTLPD_AR_R,   0x4212C)
DEP_REG32(RPUM1_INTLPD_READ_QOS,   0x43100)
DEP_REG32(RPUM1_INTLPD_WRITE_QOS,   0x43104)
DEP_REG32(RPUM1_INTLPD_FN_MOD,   0x43108)
DEP_REG32(RPUM1_INTLPD_QOS_CTNL,   0x4310C)
DEP_REG32(RPUM1_INTLPD_MAX_OT,   0x43110)
DEP_REG32(RPUM1_INTLPD_MAX_COMB_OT, 0x43114)
DEP_REG32(RPUM1_INTLPD_AW_P, 0x43118)
DEP_REG32(RPUM1_INTLPD_AW_B, 0x4311C)
DEP_REG32(RPUM1_INTLPD_AW_R, 0x43120)
DEP_REG32(RPUM1_INTLPD_AR_P,   0x43124)
DEP_REG32(RPUM1_INTLPD_AR_B,   0x43128)
DEP_REG32(RPUM1_INTLPD_AR_R,   0x4312C)
DEP_REG32(ADMAM_INTLPD_IB_FN_MOD2, 0x00044024)
DEP_REG32(ADMAM_INTLPD_IB_FN_MOD, 0x00044108)
DEP_REG32(ADMAM_INTLPD_IB_QOS_CNTL, 0x0004410C)
DEP_REG32(ADMAM_INTLPD_IB_MAX_OT, 0x00044110)
DEP_REG32(ADMAM_INTLPD_IB_MAX_COMB_OT, 0x00044114)
DEP_REG32(ADMAM_INTLPD_IB_AW_P, 0x00044118)
DEP_REG32(ADMAM_INTLPD_IB_AW_B, 0x0004411C)
DEP_REG32(ADMAM_INTLPD_IB_AW_R, 0x00044120)
DEP_REG32(ADMAM_INTLPD_IB_AR_P, 0x00044124)
DEP_REG32(ADMAM_INTLPD_IB_AR_B, 0x00044128)
DEP_REG32(ADMAM_INTLPD_IB_AR_R, 0x0004412C)
DEP_REG32(AFIFM6M_INTLPD_IB_FN_MOD, 0x00045108)
DEP_REG32(AFIFM6M_INTLPD_IB_QOS_CNTL, 0x0004510C)
DEP_REG32(AFIFM6M_INTLPD_IB_MAX_OT, 0x00045110)
DEP_REG32(AFIFM6M_INTLPD_IB_MAX_COMB_OT, 0x00045114)
DEP_REG32(AFIFM6M_INTLPD_IB_AW_P, 0x00045118)
DEP_REG32(AFIFM6M_INTLPD_IB_AW_B, 0x0004511C)
DEP_REG32(AFIFM6M_INTLPD_IB_AW_R, 0x00045120)
DEP_REG32(AFIFM6M_INTLPD_IB_AR_P, 0x00045124)
DEP_REG32(AFIFM6M_INTLPD_IB_AR_B, 0x00045128)
DEP_REG32(AFIFM6M_INTLPD_IB_AR_R, 0x0004512C)
DEP_REG32(DAP_INTLPD_IB_FN_MOD2, 0x00047024)
DEP_REG32(DAP_INTLPD_IB_READ_QOS, 0x00047100)
DEP_REG32(DAP_INTLPD_IB_WRITE_QOS, 0x00047104)
DEP_REG32(DAP_INTLPD_IB_FN_MOD, 0x00047108)
DEP_REG32(DAP_INTLPD_IB_QOS_CNTL, 0x0004710C)
DEP_REG32(DAP_INTLPD_IB_MAX_OT, 0x00047110)
DEP_REG32(DAP_INTLPD_IB_MAX_COMB_OT, 0x00047114)
DEP_REG32(DAP_INTLPD_IB_AW_P, 0x00047118)
DEP_REG32(DAP_INTLPD_IB_AW_B, 0x0004711C)
DEP_REG32(DAP_INTLPD_IB_AW_R, 0x00047120)
DEP_REG32(DAP_INTLPD_IB_AR_P, 0x00047124)
DEP_REG32(DAP_INTLPD_IB_AR_B, 0x00047128)
DEP_REG32(DAP_INTLPD_IB_AR_R, 0x0004712C)
DEP_REG32(USB0M_INTLPD_IB_READ_QOS, 0x00048100)
DEP_REG32(USB0M_INTLPD_IB_WRITE_QOS, 0x00048104)
DEP_REG32(USB0M_INTLPD_IB_FN_MOD, 0x00048108)
DEP_REG32(USB0M_INTLPD_IB_QOS_CNTL, 0x0004810C)
DEP_REG32(USB0M_INTLPD_IB_MAX_OT, 0x00048110)
DEP_REG32(USB0M_INTLPD_IB_MAX_COMB_OT, 0x00048114)
DEP_REG32(USB0M_INTLPD_IB_AW_P, 0x00048118)
DEP_REG32(USB0M_INTLPD_IB_AW_B, 0x0004811C)
DEP_REG32(USB0M_INTLPD_IB_AW_R, 0x00048120)
DEP_REG32(USB0M_INTLPD_IB_AR_P, 0x00048124)
DEP_REG32(USB0M_INTLPD_IB_AR_B, 0x00048128)
DEP_REG32(USB0M_INTLPD_IB_AR_R, 0x0004812C)
DEP_REG32(USB1M_INTLPD_IB_READ_QOS, 0x00049100)
DEP_REG32(USB1M_INTLPD_IB_WRITE_QOS, 0x00049104)
DEP_REG32(USB1M_INTLPD_IB_FN_MOD, 0x00049108)
DEP_REG32(USB1M_INTLPD_IB_QOS_CNTL, 0x0004910C)
DEP_REG32(USB1M_INTLPD_IB_MAX_OT, 0x00049110)
DEP_REG32(USB1M_INTLPD_IB_MAX_COMB_OT, 0x00049114)
DEP_REG32(USB1M_INTLPD_IB_AW_P, 0x00049118)
DEP_REG32(USB1M_INTLPD_IB_AW_B, 0x0004911C)
DEP_REG32(USB1M_INTLPD_IB_AW_R, 0x00049120)
DEP_REG32(USB1M_INTLPD_IB_AR_P, 0x00049124)
DEP_REG32(USB1M_INTLPD_IB_AR_B, 0x00049128)
DEP_REG32(USB1M_INTLPD_IB_AR_R, 0x0004912C)
DEP_REG32(INTIOU_INTLPD_IB_FN_MOD, 0x0004A108)
DEP_REG32(INTIOU_INTLPD_IB_QOS_CNTL, 0x0004A10C)
DEP_REG32(INTIOU_INTLPD_IB_MAX_OT, 0x0004A110)
DEP_REG32(INTIOU_INTLPD_IB_MAX_COMB_OT, 0x0004A114)
DEP_REG32(INTIOU_INTLPD_IB_AW_P, 0x0004A118)
DEP_REG32(INTIOU_INTLPD_IB_AW_B, 0x0004A11C)
DEP_REG32(INTIOU_INTLPD_IB_AW_R, 0x0004A120)
DEP_REG32(INTIOU_INTLPD_IB_AR_P, 0x0004A124)
DEP_REG32(INTIOU_INTLPD_IB_AR_B, 0x0004A128)
DEP_REG32(INTIOU_INTLPD_IB_AR_R, 0x0004A12C)
DEP_REG32(INTCSUPMU_INTLPD_IB_FN_MOD, 0x0004B108)
DEP_REG32(INTCSUPMU_INTLPD_IB_QOS_CNTL, 0x0004B10C)
DEP_REG32(INTCSUPMU_INTLPD_IB_MAX_OT, 0x0004B110)
DEP_REG32(INTCSUPMU_INTLPD_IB_MAX_COMB_OT, 0x0004B114)
DEP_REG32(INTCSUPMU_INTLPD_IB_AW_P, 0x0004B118)
DEP_REG32(INTCSUPMU_INTLPD_IB_AW_B, 0x0004B11C)
DEP_REG32(INTCSUPMU_INTLPD_IB_AW_R, 0x0004B120)
DEP_REG32(INTCSUPMU_INTLPD_IB_AR_P, 0x0004B124)
DEP_REG32(INTCSUPMU_INTLPD_IB_AR_B, 0x0004B128)
DEP_REG32(INTCSUPMU_INTLPD_IB_AR_R, 0x0004B12C)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_FN_MOD, 0x0004C108)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_QOS_CNTL, 0x0004C10C)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_MAX_OT, 0x0004C110)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_MAX_COMB_OT, 0x0004C114)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_AW_P, 0x0004C118)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_AW_B, 0x0004C11C)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_AW_R, 0x0004C120)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_AR_P, 0x0004C124)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_AR_B, 0x0004C128)
DEP_REG32(INTLPDINBOUND_INTLPDMAIN_AR_R, 0x0004C12C)
DEP_REG32(INTFPD_INTLPDOCM_FN_MOD, 0x0004D108)
DEP_REG32(INTFPD_INTLPDOCM_QOS_CNTL, 0x0004D10C)
DEP_REG32(INTFPD_INTLPDOCM_MAX_OT, 0x0004D110)
DEP_REG32(INTFPD_INTLPDOCM_MAX_COMB_OT, 0x0004D114)
DEP_REG32(INTFPD_INTLPDOCM_AW_P, 0x0004D118)
DEP_REG32(INTFPD_INTLPDOCM_AW_B, 0x0004D11C)
DEP_REG32(INTFPD_INTLPDOCM_AW_R, 0x0004D120)
DEP_REG32(INTFPD_INTLPDOCM_AR_P, 0x0004D124)
DEP_REG32(INTFPD_INTLPDOCM_AR_B, 0x0004D128)
DEP_REG32(INTFPD_INTLPDOCM_AR_R, 0x0004D12C)
DEP_REG32(IB9_FN_MOD_ISS_BM, 0x000C2008)
DEP_REG32(IB9_FN_MOD, 0x000C2108)
DEP_REG32(IB5_FN_MOD_ISS_BM, 0x000C3008)
DEP_REG32(IB5_FN_MOD2, 0x000C3024)
DEP_REG32(IB5_FN_MOD, 0x000C3108)
DEP_REG32(IB5_QOS_CNTL, 0x000C310C)
DEP_REG32(IB5_MAX_OT, 0x000C3110)
DEP_REG32(IB5_MAX_COMB_OT, 0x000C3114)
DEP_REG32(IB5_AW_P, 0x000C3118)
DEP_REG32(IB5_AW_B, 0x000C311C)
DEP_REG32(IB5_AW_R, 0x000C3120)
DEP_REG32(IB5_AR_P, 0x000C3124)
DEP_REG32(IB5_AR_B, 0x000C3128)
DEP_REG32(IB5_AR_R, 0x000C312C)
DEP_REG32(IB6_FN_MOD_ISS_BM, 0x000C4008)
DEP_REG32(IB6_FN_MOD2, 0x000C4024)
DEP_REG32(IB6_FN_MOD, 0x000C4108)
DEP_REG32(IB6_QOS_CNTL, 0x000C410C)
DEP_REG32(IB6_MAX_OT, 0x000C4110)
DEP_REG32(IB6_MAX_COMB_OT, 0x000C4114)
DEP_REG32(IB6_AW_P, 0x000C4118)
DEP_REG32(IB6_AW_B, 0x000C411C)
DEP_REG32(IB6_AW_R, 0x000C4120)
DEP_REG32(IB6_AR_P, 0x000C4124)
DEP_REG32(IB6_AR_B, 0x000C4128)
DEP_REG32(IB6_AR_R, 0x000C412C)
DEP_REG32(IB8_FN_MOD_ISS_BM, 0x000C5008)
DEP_REG32(IB8_FN_MOD2, 0x000C5024)
DEP_REG32(IB8_FN_MOD, 0x000C5108)
DEP_REG32(IB8_QOS_CNTL, 0x000C510C)
DEP_REG32(IB8_MAX_OT, 0x000C5110)
DEP_REG32(IB8_MAX_COMB_OT, 0x000C5114)
DEP_REG32(IB8_AW_P, 0x000C5118)
DEP_REG32(IB8_AW_B, 0x000C511C)
DEP_REG32(IB8_AW_R, 0x000C5120)
DEP_REG32(IB8_AR_P, 0x000C5124)
DEP_REG32(IB8_AR_B, 0x000C5128)
DEP_REG32(IB8_AR_R, 0x000C512C)
DEP_REG32(IB0_FN_MOD_ISS_BM, 0x000C6008)
DEP_REG32(IB0_FN_MOD2, 0x000C6024)
DEP_REG32(IB0_FN_MOD, 0x000C6108)
DEP_REG32(IB0_QOS_CNTL, 0x000C610C)
DEP_REG32(IB0_MAX_OT, 0x000C6110)
DEP_REG32(IB0_MAX_COMB_OT, 0x000C6114)
DEP_REG32(IB0_AW_P, 0x000C6118)
DEP_REG32(IB0_AW_B, 0x000C611C)
DEP_REG32(IB0_AW_R, 0x000C6120)
DEP_REG32(IB0_AR_P, 0x000C6124)
DEP_REG32(IB0_AR_B, 0x000C6128)
DEP_REG32(IB0_AR_R, 0x000C612C)
DEP_REG32(IB11_FN_MOD_ISS_BM, 0x000C7008)
DEP_REG32(IB11_FN_MOD2, 0x000C7024)
DEP_REG32(IB11_FN_MOD, 0x000C7108)
DEP_REG32(IB11_QOS_CNTL, 0x000C710C)
DEP_REG32(IB11_MAX_OT, 0x000C7110)
DEP_REG32(IB11_MAX_COMB_OT, 0x000C7114)
DEP_REG32(IB11_AW_P, 0x000C7118)
DEP_REG32(IB11_AW_B, 0x000C711C)
DEP_REG32(IB11_AW_R, 0x000C7120)
DEP_REG32(IB11_AR_P, 0x000C7124)
DEP_REG32(IB11_AR_B, 0x000C7128)
DEP_REG32(IB11_AR_R, 0x000C712C)
DEP_REG32(IB12_FN_MOD_ISS_BM, 0x000C8008)
DEP_REG32(IB12_FN_MOD2, 0x000C8024)
DEP_REG32(IB12_FN_MOD, 0x000C8108)
DEP_REG32(IB12_QOS_CNTL, 0x000C810C)
DEP_REG32(IB12_MAX_OT, 0x000C8110)
DEP_REG32(IB12_MAX_COMB_OT, 0x000C8114)
DEP_REG32(IB12_AW_P, 0x000C8118)
DEP_REG32(IB12_AW_B, 0x000C811C)
DEP_REG32(IB12_AW_R, 0x000C8120)
DEP_REG32(IB12_AR_P, 0x000C8124)
DEP_REG32(IB12_AR_B, 0x000C8128)
DEP_REG32(IB12_AR_R, 0x000C812C)

#define R_MAX (R_IB12_AR_R + 1)

typedef struct XlnxLPDGPVState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_isr;

    uint32_t regs[R_MAX];
    DepRegisterInfo regs_info[R_MAX];
} XlnxLPDGPVState;

static DepRegisterAccessInfo lpd_gpv_regs_info[] = {
    {     .name = "PERIPH_ID_4", .decode.addr = A_PERIPH_ID_4,
          .reset = 0x00000004, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_5", .decode.addr = A_PERIPH_ID_5,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_6", .decode.addr = A_PERIPH_ID_6,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_7", .decode.addr = A_PERIPH_ID_7,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_0", .decode.addr = A_PERIPH_ID_0,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_1", .decode.addr = A_PERIPH_ID_1,
          .reset = 0x000000B4, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_2", .decode.addr = A_PERIPH_ID_2,
          .reset = 0x0000002B, .ro = 0xFFFFFFFF,
    },{   .name = "PERIPH_ID_3", .decode.addr = A_PERIPH_ID_3,
          .reset = 0, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_0", .decode.addr = A_COMP_ID_0,
          .reset = 0x0000000D, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_1", .decode.addr = A_COMP_ID_1,
          .reset = 0x000000F0, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_2", .decode.addr = A_COMP_ID_2,
          .reset = 0x00000005, .ro = 0xFFFFFFFF,
    },{   .name = "COMP_ID_3", .decode.addr = A_COMP_ID_3,
          .reset = 0x000000B1, .ro = 0xFFFFFFFF,
    },{   .name = "INTLPD_OCM_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_OCM_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_RPUS0_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_RPUS0_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_RPUS1_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_RPUS1_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_USB0S_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_USB0S_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_USB1S_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_USB1S_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_AFIFS2_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_AFIFS2_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_INTIOU_IB_FN_MOD_ISS_BM",
          .decode.addr = A_INTLPD_INTIOU_IB_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "INTLPD_INTIOU_IB_FN_MOD",
          .decode.addr = A_INTLPD_INTIOU_IB_FN_MOD,
          .reset = 0,
    },{   .name = "SLAVE_11_IB_FN_MOD_ISS_BM",
          .decode.addr = A_SLAVE_11_IB_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_READ_QOS",
          .decode.addr = A_RPUM0_INTLPD_READ_QOS,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_WRITE_QOS",
          .decode.addr = A_RPUM0_INTLPD_WRITE_QOS,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_FN_MOD",
          .decode.addr = A_RPUM0_INTLPD_FN_MOD,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_QOS_CTNL",
          .decode.addr = A_RPUM0_INTLPD_QOS_CTNL,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_MAX_OT", .decode.addr = A_RPUM0_INTLPD_MAX_OT,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_MAX_COMB_OT",
          .decode.addr = A_RPUM0_INTLPD_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AW_P", .decode.addr = A_RPUM0_INTLPD_AW_P,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AW_B", .decode.addr = A_RPUM0_INTLPD_AW_B,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AW_R", .decode.addr = A_RPUM0_INTLPD_AW_R,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AR_P", .decode.addr = A_RPUM0_INTLPD_AR_P,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AR_B", .decode.addr = A_RPUM0_INTLPD_AR_B,
          .reset = 0,
    },{   .name = "RPUM0_INTLPD_AR_R", .decode.addr = A_RPUM0_INTLPD_AR_R,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_READ_QOS",
          .decode.addr = A_RPUM1_INTLPD_READ_QOS,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_WRITE_QOS",
          .decode.addr = A_RPUM1_INTLPD_WRITE_QOS,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_FN_MOD", .decode.addr = A_RPUM1_INTLPD_FN_MOD,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_QOS_CTNL",
          .decode.addr = A_RPUM1_INTLPD_QOS_CTNL,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_MAX_OT", .decode.addr = A_RPUM1_INTLPD_MAX_OT,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_MAX_COMB_OT",
          .decode.addr = A_RPUM1_INTLPD_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AW_P", .decode.addr = A_RPUM1_INTLPD_AW_P,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AW_B", .decode.addr = A_RPUM1_INTLPD_AW_B,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AW_R", .decode.addr = A_RPUM1_INTLPD_AW_R,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AR_P", .decode.addr = A_RPUM1_INTLPD_AR_P,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AR_B", .decode.addr = A_RPUM1_INTLPD_AR_B,
          .reset = 0,
    },{   .name = "RPUM1_INTLPD_AR_R", .decode.addr = A_RPUM1_INTLPD_AR_R,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_FN_MOD2",
          .decode.addr = A_ADMAM_INTLPD_IB_FN_MOD2,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_FN_MOD",
          .decode.addr = A_ADMAM_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_ADMAM_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_MAX_OT",
          .decode.addr = A_ADMAM_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_ADMAM_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AW_P", .decode.addr = A_ADMAM_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AW_B", .decode.addr = A_ADMAM_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AW_R", .decode.addr = A_ADMAM_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AR_P", .decode.addr = A_ADMAM_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AR_B", .decode.addr = A_ADMAM_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "ADMAM_INTLPD_IB_AR_R", .decode.addr = A_ADMAM_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_FN_MOD",
          .decode.addr = A_AFIFM6M_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_AFIFM6M_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_MAX_OT",
          .decode.addr = A_AFIFM6M_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_AFIFM6M_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AW_P",
          .decode.addr = A_AFIFM6M_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AW_B",
          .decode.addr = A_AFIFM6M_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AW_R",
          .decode.addr = A_AFIFM6M_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AR_P",
          .decode.addr = A_AFIFM6M_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AR_B",
          .decode.addr = A_AFIFM6M_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "AFIFM6M_INTLPD_IB_AR_R",
          .decode.addr = A_AFIFM6M_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_FN_MOD2",
          .decode.addr = A_DAP_INTLPD_IB_FN_MOD2,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_READ_QOS",
          .decode.addr = A_DAP_INTLPD_IB_READ_QOS,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_WRITE_QOS",
          .decode.addr = A_DAP_INTLPD_IB_WRITE_QOS,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_FN_MOD", .decode.addr = A_DAP_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_DAP_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_MAX_OT", .decode.addr = A_DAP_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_DAP_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AW_P", .decode.addr = A_DAP_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AW_B", .decode.addr = A_DAP_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AW_R", .decode.addr = A_DAP_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AR_P", .decode.addr = A_DAP_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AR_B", .decode.addr = A_DAP_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "DAP_INTLPD_IB_AR_R", .decode.addr = A_DAP_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_READ_QOS",
          .decode.addr = A_USB0M_INTLPD_IB_READ_QOS,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_WRITE_QOS",
          .decode.addr = A_USB0M_INTLPD_IB_WRITE_QOS,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_FN_MOD",
          .decode.addr = A_USB0M_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_USB0M_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_MAX_OT",
          .decode.addr = A_USB0M_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_USB0M_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AW_P", .decode.addr = A_USB0M_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AW_B", .decode.addr = A_USB0M_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AW_R", .decode.addr = A_USB0M_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AR_P", .decode.addr = A_USB0M_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AR_B", .decode.addr = A_USB0M_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "USB0M_INTLPD_IB_AR_R", .decode.addr = A_USB0M_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_READ_QOS",
          .decode.addr = A_USB1M_INTLPD_IB_READ_QOS,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_WRITE_QOS",
          .decode.addr = A_USB1M_INTLPD_IB_WRITE_QOS,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_FN_MOD",
          .decode.addr = A_USB1M_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_USB1M_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_MAX_OT",
          .decode.addr = A_USB1M_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_USB1M_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AW_P", .decode.addr = A_USB1M_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AW_B", .decode.addr = A_USB1M_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AW_R", .decode.addr = A_USB1M_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AR_P", .decode.addr = A_USB1M_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AR_B", .decode.addr = A_USB1M_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "USB1M_INTLPD_IB_AR_R", .decode.addr = A_USB1M_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_FN_MOD",
          .decode.addr = A_INTIOU_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_INTIOU_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_MAX_OT",
          .decode.addr = A_INTIOU_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_INTIOU_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AW_P",
          .decode.addr = A_INTIOU_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AW_B",
          .decode.addr = A_INTIOU_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AW_R",
          .decode.addr = A_INTIOU_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AR_P",
          .decode.addr = A_INTIOU_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AR_B",
          .decode.addr = A_INTIOU_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "INTIOU_INTLPD_IB_AR_R",
          .decode.addr = A_INTIOU_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_FN_MOD",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_FN_MOD,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_QOS_CNTL",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_MAX_OT",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_MAX_OT,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_MAX_COMB_OT",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AW_P",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_AW_P,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AW_B",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_AW_B,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AW_R",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_AW_R,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AR_P",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_AR_P,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AR_B",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_AR_B,
          .reset = 0,
    },{   .name = "INTCSUPMU_INTLPD_IB_AR_R",
          .decode.addr = A_INTCSUPMU_INTLPD_IB_AR_R,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_FN_MOD",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_FN_MOD,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_QOS_CNTL",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_MAX_OT",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_MAX_OT,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_MAX_COMB_OT",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AW_P",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_AW_P,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AW_B",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_AW_B,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AW_R",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_AW_R,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AR_P",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_AR_P,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AR_B",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_AR_B,
          .reset = 0,
    },{   .name = "INTLPDINBOUND_INTLPDMAIN_AR_R",
          .decode.addr = A_INTLPDINBOUND_INTLPDMAIN_AR_R,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_FN_MOD",
          .decode.addr = A_INTFPD_INTLPDOCM_FN_MOD,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_QOS_CNTL",
          .decode.addr = A_INTFPD_INTLPDOCM_QOS_CNTL,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_MAX_OT",
          .decode.addr = A_INTFPD_INTLPDOCM_MAX_OT,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_MAX_COMB_OT",
          .decode.addr = A_INTFPD_INTLPDOCM_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AW_P",
          .decode.addr = A_INTFPD_INTLPDOCM_AW_P,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AW_B",
          .decode.addr = A_INTFPD_INTLPDOCM_AW_B,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AW_R",
          .decode.addr = A_INTFPD_INTLPDOCM_AW_R,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AR_P",
          .decode.addr = A_INTFPD_INTLPDOCM_AR_P,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AR_B",
          .decode.addr = A_INTFPD_INTLPDOCM_AR_B,
          .reset = 0,
    },{   .name = "INTFPD_INTLPDOCM_AR_R",
          .decode.addr = A_INTFPD_INTLPDOCM_AR_R,
          .reset = 0,
    },{   .name = "IB9_FN_MOD_ISS_BM", .decode.addr = A_IB9_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB9_FN_MOD", .decode.addr = A_IB9_FN_MOD,
          .reset = 0,
    },{   .name = "IB5_FN_MOD_ISS_BM", .decode.addr = A_IB5_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB5_FN_MOD2", .decode.addr = A_IB5_FN_MOD2,
          .reset = 0,
    },{   .name = "IB5_FN_MOD", .decode.addr = A_IB5_FN_MOD,
          .reset = 0,
    },{   .name = "IB5_QOS_CNTL", .decode.addr = A_IB5_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB5_MAX_OT", .decode.addr = A_IB5_MAX_OT,
          .reset = 0,
    },{   .name = "IB5_MAX_COMB_OT", .decode.addr = A_IB5_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB5_AW_P", .decode.addr = A_IB5_AW_P,
          .reset = 0,
    },{   .name = "IB5_AW_B", .decode.addr = A_IB5_AW_B,
          .reset = 0,
    },{   .name = "IB5_AW_R", .decode.addr = A_IB5_AW_R,
          .reset = 0,
    },{   .name = "IB5_AR_P", .decode.addr = A_IB5_AR_P,
          .reset = 0,
    },{   .name = "IB5_AR_B", .decode.addr = A_IB5_AR_B,
          .reset = 0,
    },{   .name = "IB5_AR_R", .decode.addr = A_IB5_AR_R,
          .reset = 0,
    },{   .name = "IB6_FN_MOD_ISS_BM", .decode.addr = A_IB6_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB6_FN_MOD2", .decode.addr = A_IB6_FN_MOD2,
          .reset = 0,
    },{   .name = "IB6_FN_MOD", .decode.addr = A_IB6_FN_MOD,
          .reset = 0,
    },{   .name = "IB6_QOS_CNTL", .decode.addr = A_IB6_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB6_MAX_OT", .decode.addr = A_IB6_MAX_OT,
          .reset = 0,
    },{   .name = "IB6_MAX_COMB_OT", .decode.addr = A_IB6_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB6_AW_P", .decode.addr = A_IB6_AW_P,
          .reset = 0,
    },{   .name = "IB6_AW_B", .decode.addr = A_IB6_AW_B,
          .reset = 0,
    },{   .name = "IB6_AW_R", .decode.addr = A_IB6_AW_R,
          .reset = 0,
    },{   .name = "IB6_AR_P", .decode.addr = A_IB6_AR_P,
          .reset = 0,
    },{   .name = "IB6_AR_B", .decode.addr = A_IB6_AR_B,
          .reset = 0,
    },{   .name = "IB6_AR_R", .decode.addr = A_IB6_AR_R,
          .reset = 0,
    },{   .name = "IB8_FN_MOD_ISS_BM", .decode.addr = A_IB8_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB8_FN_MOD2", .decode.addr = A_IB8_FN_MOD2,
          .reset = 0,
    },{   .name = "IB8_FN_MOD", .decode.addr = A_IB8_FN_MOD,
          .reset = 0,
    },{   .name = "IB8_QOS_CNTL", .decode.addr = A_IB8_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB8_MAX_OT", .decode.addr = A_IB8_MAX_OT,
          .reset = 0,
    },{   .name = "IB8_MAX_COMB_OT", .decode.addr = A_IB8_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB8_AW_P", .decode.addr = A_IB8_AW_P,
          .reset = 0,
    },{   .name = "IB8_AW_B", .decode.addr = A_IB8_AW_B,
          .reset = 0,
    },{   .name = "IB8_AW_R", .decode.addr = A_IB8_AW_R,
          .reset = 0,
    },{   .name = "IB8_AR_P", .decode.addr = A_IB8_AR_P,
          .reset = 0,
    },{   .name = "IB8_AR_B", .decode.addr = A_IB8_AR_B,
          .reset = 0,
    },{   .name = "IB8_AR_R", .decode.addr = A_IB8_AR_R,
          .reset = 0,
    },{   .name = "IB0_FN_MOD_ISS_BM", .decode.addr = A_IB0_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB0_FN_MOD2", .decode.addr = A_IB0_FN_MOD2,
          .reset = 0,
    },{   .name = "IB0_FN_MOD", .decode.addr = A_IB0_FN_MOD,
          .reset = 0,
    },{   .name = "IB0_QOS_CNTL", .decode.addr = A_IB0_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB0_MAX_OT", .decode.addr = A_IB0_MAX_OT,
          .reset = 0,
    },{   .name = "IB0_MAX_COMB_OT", .decode.addr = A_IB0_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB0_AW_P", .decode.addr = A_IB0_AW_P,
          .reset = 0,
    },{   .name = "IB0_AW_B", .decode.addr = A_IB0_AW_B,
          .reset = 0,
    },{   .name = "IB0_AW_R", .decode.addr = A_IB0_AW_R,
          .reset = 0,
    },{   .name = "IB0_AR_P", .decode.addr = A_IB0_AR_P,
          .reset = 0,
    },{   .name = "IB0_AR_B", .decode.addr = A_IB0_AR_B,
          .reset = 0,
    },{   .name = "IB0_AR_R", .decode.addr = A_IB0_AR_R,
          .reset = 0,
    },{   .name = "IB11_FN_MOD_ISS_BM", .decode.addr = A_IB11_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB11_FN_MOD2", .decode.addr = A_IB11_FN_MOD2,
          .reset = 0,
    },{   .name = "IB11_FN_MOD", .decode.addr = A_IB11_FN_MOD,
          .reset = 0,
    },{   .name = "IB11_QOS_CNTL", .decode.addr = A_IB11_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB11_MAX_OT", .decode.addr = A_IB11_MAX_OT,
          .reset = 0,
    },{   .name = "IB11_MAX_COMB_OT", .decode.addr = A_IB11_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB11_AW_P", .decode.addr = A_IB11_AW_P,
          .reset = 0,
    },{   .name = "IB11_AW_B", .decode.addr = A_IB11_AW_B,
          .reset = 0,
    },{   .name = "IB11_AW_R", .decode.addr = A_IB11_AW_R,
          .reset = 0,
    },{   .name = "IB11_AR_P", .decode.addr = A_IB11_AR_P,
          .reset = 0,
    },{   .name = "IB11_AR_B", .decode.addr = A_IB11_AR_B,
          .reset = 0,
    },{   .name = "IB11_AR_R", .decode.addr = A_IB11_AR_R,
          .reset = 0,
    },{   .name = "IB12_FN_MOD_ISS_BM", .decode.addr = A_IB12_FN_MOD_ISS_BM,
          .reset = 0,
    },{   .name = "IB12_FN_MOD2", .decode.addr = A_IB12_FN_MOD2,
          .reset = 0,
    },{   .name = "IB12_FN_MOD", .decode.addr = A_IB12_FN_MOD,
          .reset = 0,
    },{   .name = "IB12_QOS_CNTL", .decode.addr = A_IB12_QOS_CNTL,
          .reset = 0,
    },{   .name = "IB12_MAX_OT", .decode.addr = A_IB12_MAX_OT,
          .reset = 0,
    },{   .name = "IB12_MAX_COMB_OT", .decode.addr = A_IB12_MAX_COMB_OT,
          .reset = 0,
    },{   .name = "IB12_AW_P", .decode.addr = A_IB12_AW_P,
          .reset = 0,
    },{   .name = "IB12_AW_B", .decode.addr = A_IB12_AW_B,
          .reset = 0,
    },{   .name = "IB12_AW_R", .decode.addr = A_IB12_AW_R,
          .reset = 0,
    },{   .name = "IB12_AR_P", .decode.addr = A_IB12_AR_P,
          .reset = 0,
    },{   .name = "IB12_AR_B", .decode.addr = A_IB12_AR_B,
          .reset = 0,
    },{   .name = "IB12_AR_R", .decode.addr = A_IB12_AR_R,
          .reset = 0,
    },
};

static void lpd_gpv_reset(DeviceState *dev)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        dep_register_reset(&s->regs_info[i]);
    }
}

static uint64_t lpd_gpv_read(void *opaque, hwaddr addr, unsigned size)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(opaque);
    DepRegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: read from %" HWADDR_PRIx "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr);
        return 0;
    }
    return dep_register_read(r);
}

static void lpd_gpv_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(opaque);
    DepRegisterInfo *r = &s->regs_info[addr / 4];

    if (!r->data) {
        qemu_log("%s: Decode error: write to %" HWADDR_PRIx "=%" PRIx64 "\n",
                 object_get_canonical_path(OBJECT(s)),
                 addr, value);
        return;
    }
    dep_register_write(r, value, ~0);
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

static void lpd_gpv_realize(DeviceState *dev, Error **errp)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(dev);
    const char *prefix = object_get_canonical_path(OBJECT(dev));
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(lpd_gpv_regs_info); ++i) {
        DepRegisterInfo *r = &s->regs_info[lpd_gpv_regs_info[i].decode.addr/4];

        *r = (DepRegisterInfo) {
            .data = (uint8_t *)&s->regs[
                    lpd_gpv_regs_info[i].decode.addr/4],
            .data_size = sizeof(uint32_t),
            .access = &lpd_gpv_regs_info[i],
            .debug = XLNX_LPD_GPV_ERR_DEBUG,
            .prefix = prefix,
            .opaque = s,
        };
    }
}

static void lpd_gpv_init(Object *obj)
{
    XlnxLPDGPVState *s = XLNX_LPD_GPV(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &lpd_gpv_ops, s,
                          TYPE_XLNX_LPD_GPV, R_MAX * 4);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_lpd_gpv = {
    .name = TYPE_XLNX_LPD_GPV,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxLPDGPVState, R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void lpd_gpv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = lpd_gpv_reset;
    dc->realize = lpd_gpv_realize;
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
