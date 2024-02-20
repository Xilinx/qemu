/*
 * UFS controller
 * Based on JESD223
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/register.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/block/ufshc-if.h"
#include "ufs-utp.h"
#include "hw/irq.h"
#include "trace.h"
#include "sysemu/dma.h"
#include "hw/sysbus.h"
#include "hw/block/ufs-dev.h"

#ifndef UFSHC_ERR_DEBUG
#define UFSHC_ERR_DEBUG 0
#endif

#define TYPE_UFSHC "ufshc"
#define UFSHC(obj) \
        OBJECT_CHECK(UFSHCState, (obj), TYPE_UFSHC)
#define TYPE_SYSBUS_UFSHC "ufshc-sysbus"
#define UFSHC_SYSBUS(obj) \
        OBJECT_CHECK(UFSHCSysbus, (obj), TYPE_SYSBUS_UFSHC)

#define R_HOST_CAP_REG_OFFSET 0x0
#define R_HOST_CAP_REG_SIZE (0x20 / 4)
#define R_OPR_RUN_REG_OFFSET 0x20
#define R_OPR_RUN_REG_SIZE  ((0x50 - 0x20) / 4)
#define R_UTP_TX_REG_OFFSET 0x50
#define R_UTP_TX_REG_SIZE   ((0x70 - 0x50) / 4)
#define R_UTP_TMNG_REG_OFFSET 0x70
#define R_UTP_TMNG_REG_SIZE ((0x90 - 0x70) / 4)
#define R_UIC_CMD_REG_OFFSET 0x90
#define R_UIC_CMD_REG_SIZE  ((0xF0 - 0x90) / 4)

#define R_MAX (0xF0 / 4)

#define MAX_TR 32
#define MAX_TMR 8
#define MIN_LINK_STARTUP_COUNT 5
#define UIC_GEN_ERR_CODE_SUCCESS 0
#define UIC_GEN_ERR_CODE_FAILURE 1

/*
 * Host Controller Capabilities Registers
 */
REG32(CAP, 0x0)
    FIELD(CAP, NUTRS, 0, 5)
    FIELD(CAP, NUTMRS, 16, 3)
    FIELD(CAP, 64AS, 24, 1)
    FIELD(CAP, OODDS, 25, 1)
    FIELD(CAP, UICDMETMS, 26, 1)
REG32(VER, 0x8)
    FIELD(VER, MNR, 0, 16)
    FIELD(VER, MJR, 16, 16)
REG32(HCDDID, 0x10)
    FIELD(HCDDID, DC, 0, 16)
    FIELD(HCDDID, HCDID, 24, 8)
REG32(HCPMID, 0x14)
    FIELD(HCPMID, MID, 0, 16)
    FIELD(HCPMID, PID, 16, 16)

/*
 * Operation and Runtime Registers
 */
REG32(IS, 0x20)
    FIELD(IS, SBFES, 17, 1)
    FIELD(IS, HCFES, 16, 1)
    FIELD(IS, DFES, 11, 1)
    FIELD(IS, UCCS, 10, 1)
    FIELD(IS, UTMRCS, 9, 1)
    FIELD(IS, ULSS, 8, 1)
    FIELD(IS, ULLS, 7, 1)
    FIELD(IS, UHES, 6, 1)
    FIELD(IS, UHXS, 5, 1)
    FIELD(IS, UPMS, 4, 1)
    FIELD(IS, UTMS, 3, 1)
    FIELD(IS, UE, 2, 1)
    FIELD(IS, UDEPRI, 1, 1)
    FIELD(IS, UTRCS, 0, 1)
REG32(IE, 0x24)
    FIELD(IE, SBFES, 17, 1)
    FIELD(IE, HCFES, 16, 1)
    FIELD(IE, DFES, 11, 1)
    FIELD(IE, UCCS, 10, 1)
    FIELD(IE, UTMRCS, 9, 1)
    FIELD(IE, ULSS, 8, 1)
    FIELD(IE, ULLS, 7, 1)
    FIELD(IE, UHES, 6, 1)
    FIELD(IE, UHXS, 5, 1)
    FIELD(IE, UPMS, 4, 1)
    FIELD(IE, UTMS, 3, 1)
    FIELD(IE, UE, 2, 1)
    FIELD(IE, UDEPRI, 1, 1)
    FIELD(IE, UTRCS, 0, 1)
REG32(HCS, 0x30)
    FIELD(HCS, TTAGUTPE, 16, 8)
    FIELD(HCS, UTPEC, 12, 4)
    FIELD(HCS, CCS, 11, 1)
    FIELD(HCS, UPMCRS, 8, 3)
    FIELD(HCS, DEI, 5, 1)
    FIELD(HCS, HEI, 4, 1)
    FIELD(HCS, UCRDY, 3, 1)
    FIELD(HCS, UTMRLRDY, 2, 1)
    FIELD(HCS, UTRLRDY, 1, 1)
    FIELD(HCS, DP, 0, 1)
REG32(HCE, 0x34)
    FIELD(HCE, HCE, 0, 1)
REG32(UECPA, 0x38)
    FIELD(UECPA, EC, 0, 5)
    FIELD(UECPA, ERR, 31, 1)
REG32(UECDL, 0x3c)
    FIELD(UECDL, EC, 0, 15)
    FIELD(UECDL, ERR, 31, 1)
REG32(UECN, 0x40)
    FIELD(UECN, EC, 0, 3)
    FIELD(UECN, ERR, 31, 1)
REG32(UECT, 0x44)
    FIELD(UECT, EC, 0, 7)
    FIELD(UECT, ERR, 31, 1)
REG32(UECDME, 0x48)
    FIELD(UECDME, EC, 0, 1)
    FIELD(UECDME, ERR, 31, 1)
REG32(UTRIACR, 0x4c)
    FIELD(UTRIACR, IAEN, 31, 1)
    FIELD(UTRIACR, IAPWEN, 24, 1)
    FIELD(UTRIACR, IASB, 20, 1)
    FIELD(UTRIACR, CTR, 16, 1)
    FIELD(UTRIACR, IACTH, 8, 5)
    FIELD(UTRIACR, IATOVAL, 0, 8)
/*
 * UTP Transfer Request Registers
 */
REG32(UTRLBA, 0x50)
    FIELD(UTRLBA, UTRLBA, 10, 22)
REG32(UTRLBAU, 0x54)
    FIELD(UTRLBAU, UTRLBAU, 0, 32)
REG32(UTRLDBR, 0x58)
    FIELD(UTRLDBR, UTRLDBR, 0, 32)
REG32(UTRLCLR, 0x5c)
    FIELD(UTRLCLR, UTRLCLR, 0, 32)
REG32(UTRLRSR, 0x60)
    FIELD(UTRLRSR, UTRLRSR, 0, 1)
/*
 * UTP Task Management Registers
 */
REG32(UTMRLBA, 0x70)
    FIELD(UTMRLBA, UTMRLBA, 10, 22)
REG32(UTMRLBAU, 0x74)
    FIELD(UTMRLBAU, UTMRLBAU, 0, 32)
REG32(UTMRLDBR, 0x78)
    FIELD(UTMRLDBR, UTMRLDBR, 0, 32)
REG32(UTMRLCLR, 0x7c)
    FIELD(UTMRLCLR, UTMRLCLR, 0, 32)
REG32(UTMRLRSR, 0x80)
    FIELD(UTMRLRSR, UTMRLRSR, 0, 1)
/*
 * UIC Command Registers
 */
REG32(UICCMD, 0x90)
    FIELD(UICCMD, CMDOP, 0, 8)
REG32(UICCMDARG1, 0x94)
    FIELD(UICCMDARG1, ARG1, 0, 32)
REG32(UICCMDARG2, 0x98)
    FIELD(UICCMDARG2, ARG2, 0, 32)
REG32(UICCMDARG3, 0x9c)
    FIELD(UCMDARG3, ARG3, 0, 32)

typedef struct utp_record {
    /*
     * Parameters recoreded from
     * request upiu
     *
     *  -Transfer type
     *  -Lun
     *  -Task tag
     */
    uint8_t tt;
    uint8_t lun;
    uint8_t task_tag;
    /*
     * Parameters recorded from
     * response upiu's for further
     * processing the transfer requests.
     *
     *  -Data segment lengh
     *  -Data Buffer offset
     *  -Data Transfer count
     *  -EHS Length
     *  -Transfer type
     */
    uint16_t dsl;
    uint32_t dbo;
    uint32_t dtc;
    uint8_t ehs_len;
    uint8_t resp_tt;
} utp_record;

typedef struct tr_info {
    utp_tr_desc desc;
    utp_record rec;
    ufs_prdt *prdt;
    QEMUSGList sgl;
} tr_info;

typedef struct tmr_info {
    utp_tmr_desc desc;
    utp_record rec;
} tmr_info;

typedef struct UFSHCState {
    DeviceState parent;

    MemoryRegion iomem;

    uint8_t num_tr_slots;
    uint8_t num_tmr_slots;
    bool oods;
    uint32_t ufshci_ver;
    uint8_t hcdid;
    uint16_t dc;
    uint16_t mid;
    uint16_t pid;
    uint8_t nLinkStartUp;
    qemu_irq irq;
    /*
     * Transfere Request list
     */
    tr_info tr_list[MAX_TR];
    /*
     * Task Management Request list
     */
    tmr_info tmr_list[MAX_TMR];

    ufshcIF *ufs_target;
    ufshcIF *unipro;

    AddressSpace *dma_as;
    /*
     * Registers
     * Host Controller Capabilities Registers
     * Operation and Runtime Registers
     * UTP Transfer Request Registers
     * UTP Task Management Registers
     * UIC Command Registers
     */
    uint32_t regs[R_MAX];
    RegisterInfo regs_info[R_MAX];
} UFSHCState;

typedef struct UFSHCSysbus {
    SysBusDevice parent;

    UFSHCState ufshc;
    UFSDev *ufsdev;
    ufsBus *bus;
    MemoryRegion *dma_mr;
    qemu_irq *irq;
} UFSHCSysbus;

static inline bool ufshc_is_enable(UFSHCState *s)
{
    return !!ARRAY_FIELD_EX32(s->regs, HCE, HCE);
}

static void ufshc_irq_update(UFSHCState *s)
{
    /*
     * TODO:
     * Add Interrupt Aggregation logic
     */
    qemu_set_irq(s->irq, s->regs[R_IS] & s->regs[R_IE]);
}

static void ufshc_init(UFSHCState *s)
{
    bool t_present = !!(s->ufs_target);

    /*
     * Reset the controller
     *
     * DME_RESET.req
     * DME_RESET.cnf_L
     * DME_ENABLE.req
     * DME_ENABLE.cnf_L
     * Set HCE &
     * UIC Ready
     */
    ufshci_dme_cmd(s->unipro, DME_RESET, 0, 0, NULL);
    ARRAY_FIELD_DP32(s->regs, HCE, HCE, 1);
    ARRAY_FIELD_DP32(s->regs, HCS, UCRDY, 1);
    /*
     * TR/TMR list ready
     */
    ARRAY_FIELD_DP32(s->regs, HCS, UTRLRDY, t_present);
    ARRAY_FIELD_DP32(s->regs, HCS, UTMRLRDY, t_present);
    ARRAY_FIELD_DP32(s->regs, HCS, CCS, !t_present);
}

static void hce_post_write(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);

    if (s->regs[R_HCE]) {
        resettable_reset(OBJECT(s), RESET_TYPE_COLD);
        ufshc_init(s);
    }
    ufshc_irq_update(s);
}

static void uiccmd_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);
    CfgResultCode status = DME_FAILURE;
    bool t_present = !!(s->ufs_target);


    if (val == 0) {
        return;
    }

    status = ufshci_dme_cmd(s->unipro, 0xFF & val,
        extract32(s->regs[R_UICCMDARG1], 16, 16),
        extract32(s->regs[R_UICCMDARG1], 0, 16),
        &s->regs[R_UICCMDARG3]);

    switch ((uint8_t) val) {
    case DME_POWERON:
    case DME_POWEROFF:
        if (status == DME_SUCCESS) {
            ARRAY_FIELD_DP32(s->regs, IS, UPMS, 1);
        }
        break;
    case DME_RESET:
        break;
    case DME_ENDPOINTRESET:
        ARRAY_FIELD_DP32(s->regs, IS, UDEPRI, 1);
        break;
    case DME_LINKSTARTUP:
        if (status == DME_SUCCESS) {
            ARRAY_FIELD_DP32(s->regs, HCS, DP, t_present);
        }
        break;
    };

    ARRAY_FIELD_DP32(s->regs, IS, UCCS, 1);
    ARRAY_FIELD_DP32(s->regs, UICCMDARG2, ARG2,(uint8_t) status);
    ufshc_irq_update(s);
}

static void utriacr_postw(RegisterInfo *reg, uint64_t val)
{
    /*
     * Update Timer/Counter on enabling IAPWEN
     * Implement Timer/Counter for interrupts
     */
    qemu_log_mask(LOG_UNIMP, "Interrupt aggregator not supported!\n");
}

static void utrlclr_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);

    /*
     * Clear the list
     */
    if (s->regs[R_UTRLRSR]) {
        s->regs[R_UTRLDBR] &= s->regs[R_UTRLCLR];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to UTRLCLR, while UTRLRSR is not set");
    }
}

static hwaddr utp_tr_get_ucd_base(utp_tr_desc *desc)
{
    return desc->ucdba | (hwaddr) desc->ucdbau << 32;
}

static uint32_t sizeof_upiu(uint8_t tt)
{
    uint32_t size = 0;

    switch (tt & 0x3F) {
    case TRNS_NOP_OUT:
    case TRNS_NOP_IN:
        size = sizeof(upiu_nop);
        break;
    case TRNS_COMMAND:
        size = sizeof(upiu_cmd);
        break;
    case TRNS_RESPONSE:
        size = sizeof(upiu_resp);
        break;
    case TRNS_DATA_OUT:
    case TRNS_DATA_IN:
    case TRNS_RDY_TO_TRANSFER:
        size = sizeof(upiu_data);
        break;
    case TRNS_TASK_MNG_REQ:
    case TRNS_TASK_MNG_RESP:
        size = sizeof(upiu_task_mng_req);
        break;
    case TRNS_QUERY_REQ:
    case TRNS_QUERY_RESP:
        size = sizeof(upiu_query);
        break;
    case TRNS_REJECT:
        size = sizeof(upiu_reject);
        break;
    };
    return size;
}

static void utp_upiu_ex(UFSHCState *s, upiu_pkt *pkt, hwaddr upiu_base)
{
    uint8_t tt;
    /*
     * Read UPIU Header
     */
    address_space_rw(s->dma_as,
                    upiu_base,
                    MEMTXATTRS_UNSPECIFIED,
                    pkt,
                    sizeof(upiu_header), false);
    tt = UPIU_TT(pkt);
    /*
     * Read rest of the upiu packet
     */
    address_space_rw(s->dma_as,
                    upiu_base + sizeof(upiu_header),
                    MEMTXATTRS_UNSPECIFIED,
                    (uint8_t *)pkt + sizeof(upiu_header),
                    (sizeof_upiu(tt) -
                     sizeof(upiu_header)), false);
}

static void utp_record_req_upiu_param(upiu_pkt *pkt, utp_record *r)
{
    r->tt = UPIU_TT(pkt);
    r->lun = UPIU_LUN(pkt);
    r->task_tag = UPIU_TAG(pkt);
}

static void utp_upiu_data_ex(UFSHCState *s, upiu_pkt *pkt, hwaddr upiu_base, void *data)
{
    uint16_t len;
    uint8_t tt;

    tt = UPIU_TT(pkt);
    len = UPIU_DSL(pkt);

    address_space_rw(s->dma_as,
                    upiu_base + sizeof_upiu(tt),
                    MEMTXATTRS_UNSPECIFIED,
                    data,
                    len, false);
}

static void ufs_prepare_sg_list(UFSHCState *s, tr_info *tr)
{
    int i;
    dma_addr_t addr;
    dma_addr_t size;

    qemu_sglist_init(&tr->sgl, DEVICE(s), (int) tr->desc.prdtl,
                     s->dma_as);
    trace_ufshc_sgl_list("SGL list:");
    for(i = 0; i < tr->desc.prdtl; i++) {
        addr = (dma_addr_t) tr->prdt[i].addrl | ((dma_addr_t) tr->prdt[i].addrh << 32);
        size = ((tr->prdt[i].size & R_PRDT_DW3_DBC_MASK) | 0x3) + 1;
        trace_ufshc_sgl_list2((uint64_t) addr, (uint64_t) size);
        qemu_sglist_add(&tr->sgl, addr, size);
    }
}

/*
 * Extract the PRDT table
 */
static void utp_tr_prdt_ex(UFSHCState *s, tr_info *tr)
{
    uint16_t prdtl = tr->desc.prdtl;
    uint64_t ucd_base = utp_tr_get_ucd_base(&tr->desc);
    uint16_t prdto = tr->desc.prdto << 2;

    if (prdtl) {
        tr->prdt = g_malloc0(sizeof(ufs_prdt) * prdtl);

        address_space_rw(s->dma_as,
                        ucd_base + prdto,
                        MEMTXATTRS_UNSPECIFIED,
                        tr->prdt, sizeof(ufs_prdt) * prdtl,
                        false);
        /*
         *  Prepare QEMU SG list
         */
        ufs_prepare_sg_list(s, tr);
    }

 }

static void start_tr_processing(UFSHCState *s)
{
    unsigned int i;
    hwaddr ucd_base;
    upiu_pkt tr_upiu;
    /* Data segment length */
    uint16_t dsl;
    void *ds;

    /*
     * Start Processing list
     */
    for (i = 0; i < s->num_tr_slots; i++) {
        if (extract32(s->regs[R_UTRLDBR], i, 1)) {
            /*
             * Clear the OCS
             */
            ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[i].desc, UTP_DW2, OCS,
                              UTP_OCS_SUCCESS);
            memset(&tr_upiu, 0, sizeof(upiu_pkt));
            /*
             * Read UPIU
             */
            ucd_base = le32_to_cpu(s->tr_list[i].desc.ucdba) |
                       ((uint64_t)le32_to_cpu(s->tr_list[i].desc.ucdbau) << 32);
            utp_upiu_ex(s, &tr_upiu, ucd_base);
            trace_ufshc_tr_send(UPIU_TT(&tr_upiu), UPIU_TAG(&tr_upiu), i);
            /*
             * Record the UPIU params to relate the upiu's from target.
             */
            utp_record_req_upiu_param(&tr_upiu, &s->tr_list[i].rec);
            utp_tr_prdt_ex(s, &s->tr_list[i]);
            /*
             * Send UPIU to target
             */
            ufshci_send_upiu(s->ufs_target, &tr_upiu);
            /*
             * Send Data segment
             */
            dsl = UPIU_DSL(&tr_upiu);
            if (dsl != 0) {
                ds = g_malloc0(dsl);
                utp_upiu_data_ex(s, &tr_upiu, ucd_base, ds);
                ufshci_send_data(s->ufs_target, ds, dsl, UPIU_TAG(&tr_upiu));
                g_free(ds);
            }
        }
    }
}

static void utrlrsr_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);

    if (val && !ufshc_is_enable(s)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Transfer request is started while HC is disabled");
            return;
    }

    if (val) {
        start_tr_processing(s);
    }
}

static void utrldbr_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);
    unsigned int i;
    hwaddr listBase;
    /*
     * Reload desc upon setting the bit and
     * clear the bits on reload.
     */

    listBase = s->regs[R_UTRLBA] | (((uint64_t)s->regs[R_UTRLBAU]) << 32);
    for (i = 0; i < s->num_tr_slots; i++) {
        if (extract32((uint32_t) val, i, 1)) {
            address_space_rw(s->dma_as,
                            listBase + (sizeof(utp_tr_desc) * i),
                            MEMTXATTRS_UNSPECIFIED,
                            &s->tr_list[i].desc,
                            sizeof(utp_tr_desc), false);
        }
    }
    if (ARRAY_FIELD_EX32(s->regs, UTRLRSR, UTRLRSR)) {
        start_tr_processing(s);
    }
}

static void utmrldbr_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);
    unsigned int i;
    hwaddr listBase;

    /*
     * Reload desc upon setting the bit and
     * clear the bits on reload.
     */
    listBase = s->regs[R_UTMRLBA] | (((uint64_t)s->regs[R_UTMRLBAU]) << 32);
    for (i = 0; i < s->num_tmr_slots; i++) {
        if (extract32((uint32_t) val, i, 1)) {
            address_space_rw(s->dma_as,
                            listBase + (sizeof(utp_tmr_desc) * i),
                            MEMTXATTRS_UNSPECIFIED,
                            &s->tmr_list[i].desc,
                            sizeof(utp_tmr_desc), false);
        }
    }
}

static void utmrlclr_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);
    /*
     * Clear the list
     */
    if (s->regs[R_UTMRLRSR]) {
        s->regs[R_UTMRLDBR] &= s->regs[R_UTMRLCLR];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Write to UTMRLCLR, while UTMRLRSR is not set");
    }
}

static void utmrlrsr_postw(RegisterInfo *reg, uint64_t val)
{
    UFSHCState *s = UFSHC(reg->opaque);
    upiu_pkt tmr_upiu;
    unsigned int i;

    if (val && ufshc_is_enable(s)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Transfer Management request is started while"
                          " HC is disabled");
            return;
    }

    if (!val) {
        return;
    }
    /*
     * Start Processing list
     */
    for (i = 0; i < s->num_tmr_slots; i++) {
        if (extract32(s->regs[R_UTMRLDBR], i, 1)) {
            memset(&tmr_upiu, 0, sizeof(upiu_pkt));
            /*
             * Send UPIU to target
             */
            trace_ufshc_tmr_send(UPIU_TT(&s->tmr_list[i].desc.req),
                                      UPIU_TAG(&s->tmr_list[i].desc.req), i);
            tmr_upiu.task_mng_req = s->tmr_list[i].desc.req;
            /*
             * Record the UPIU params to relate the upiu's from target.
             */
            utp_record_req_upiu_param(&tmr_upiu, &s->tmr_list[i].rec);

            ufshci_send_upiu(s->ufs_target, &tmr_upiu);
        }
    }
}

static const RegisterAccessInfo ufshc_reg_info[] = {
    {    .name = "CAP", .addr = A_CAP,
         .ro = 0xffffffff,
    },{  .name = "VER", .addr = A_VER,
         .ro = 0xffffffff,
    },{  .name = "HCDDID", .addr = A_HCDDID,
         .ro = 0xffffffff,
    },{  .name = "HCPMID", .addr = A_HCPMID,
         .ro = 0xffffffff,
    },{  .name = "IS", .addr = A_IS,
         .w1c = 0x30fff,
         .ro = 0xffcf000,
    },{  .name = "IE", .addr = A_IE,
         .ro = 0xffcf000,
    },{  .name = "HCS", .addr = A_HCS,
         .ro = 0xfffff8cf,
         .w1c = 0x30,
    },{  .name = "HCE", .addr = A_HCE,
         .ro = 0xfffffffe,
         .post_write = hce_post_write,
    },{  .name = "UECPA", .addr = A_UECPA,
         .ro = 0xffffffff,
    },{  .name = "UECDL", .addr = A_UECDL,
         .ro = 0xffffffff,
    },{  .name = "UECN", .addr = A_UECN,
         .ro = 0xffffffff,
    },{  .name = "UECT", .addr = A_UECT,
         .ro = 0xffffffff,
    },{  .name = "UECDME", .addr = A_UECDME,
         .ro = 0xffffffff,
    },{  .name = "UTRIACR", .addr = A_UTRIACR,
         .rsvd = 0x7eeee000,
         .post_write = utriacr_postw,
    },{  .name = "UTRLBA", .addr = A_UTRLBA,
         .rsvd = 0x3ff,
    },{  .name = "UTRLBAU", .addr = A_UTRLBAU,
    },{  .name = "UTRLDBR", .addr = A_UTRLDBR,
         .post_write = utrldbr_postw,
    },{  .name = "UTRLCLR", .addr = A_UTRLCLR,
         .post_write = utrlclr_postw,
    },{  .name = "UTRLRSR", .addr = A_UTRLRSR,
         .rsvd = 0xfffffffe,
         .post_write = utrlrsr_postw,
    },{  .name = "UTMRLBA", .addr = A_UTMRLBA,
         .rsvd = 0x3ff,
    },{  .name = "UTMRLBAU", .addr = A_UTMRLBAU,
    },{  .name = "UTMRLDBR", .addr = A_UTMRLDBR,
         .post_write = utmrldbr_postw,
    },{  .name = "UTMRLCLR", .addr = A_UTMRLCLR,
         .post_write = utmrlclr_postw,
    },{  .name = "UTMRLRSR", .addr = A_UTMRLRSR,
         .rsvd = 0xfffffffe,
         .post_write = utmrlrsr_postw,
    },{  .name = "UICCMD", .addr = A_UICCMD,
         .ro = 0xffffff00,
         .post_write = uiccmd_postw,
    },{  .name = "UICCMDARG1", .addr = A_UICCMDARG1,
    },{  .name = "UICCMDARG2", .addr = A_UICCMDARG2,
    },{  .name = "UICCMDARG3", .addr = A_UICCMDARG3,
    }
};

/*
 * Copy the UPIU Response header
 */
static bool utp_tr_copy_resp(UFSHCState *s, upiu_pkt *resp, uint8_t slot)
{
    uint8_t tt;
    uint32_t rSize;
    hwaddr ucd_base;
    uint32_t ruo;
    uint32_t rul;

    if (slot >= s->num_tr_slots) {
        return false;
    }
    tt = UPIU_TT(resp);
    rSize = sizeof_upiu(tt);
    ucd_base = utp_tr_get_ucd_base(&s->tr_list[slot].desc);
    ruo = s->tr_list[slot].desc.ruo << 2;
    rul = s->tr_list[slot].desc.rul << 2;

    /*
     * Check for Response size issues
     */
    if (rul < rSize) {
        qemu_log_mask(LOG_GUEST_ERROR,
           "Expected Response size %d but received %d\n", rul, rSize);
        ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[slot].desc, UTP_DW2,
                          OCS, UTP_OCS_MISMATCH_RESPONSE_UPIU_SIZE);
    }
    address_space_rw(s->dma_as,
                    ucd_base + ruo,
                    MEMTXATTRS_UNSPECIFIED,
                    resp,
                    rSize, true);
    return true;
}

/*
 * Copy the response data
 */
static void utp_tr_copy_resp_data(UFSHCState *s, void *data, uint16_t len,
                                   uint8_t slot)
{
    uint8_t tt = s->tr_list[slot].rec.resp_tt;
    uint32_t rSize = sizeof_upiu(tt);
    hwaddr ucd_base = utp_tr_get_ucd_base(&s->tr_list[slot].desc);
    uint32_t ruo = s->tr_list[slot].desc.ruo << 2;
    uint32_t rul = s->tr_list[slot].desc.rul << 2;

    /*
     * Check for Response size issues
     */
    if (s->tr_list[slot].rec.dsl < len) {
        ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[slot].desc,
                         UTP_DW2, OCS, UTP_OCS_MISMATCH_DATA_BUFFER_SIZE);
    }
    if (rul < rSize + len) {
        ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[slot].desc,
                         UTP_DW2, OCS, UTP_OCS_MISMATCH_RESPONSE_UPIU_SIZE);
    }
    address_space_rw(s->dma_as,
                    ucd_base + ruo + rSize,
                    MEMTXATTRS_UNSPECIFIED,
                    data,
                    len, true);
}

/*
 * Search for transfer request internal record.
 */
static uint8_t search_tr_list(UFSHCState *s, uint8_t tag)
{
    int i;
    for (i = 0; i < s->num_tr_slots; i++) {
        if ((s->tr_list[i].rec.task_tag == tag)) {
            break;
        }
    }
    return i;
}

/*
 * Search for transfer management request internal record
 */
static uint8_t search_tmr_list(UFSHCState *s, uint8_t tag)
{
    int i;
    for (i = 0; i < s->num_tmr_slots; i++) {
        if ((s->tmr_list[i].rec.task_tag == tag)) {
            break;
        }
    }
    return i;
}

/*
 * Record TR Response UPIU Packets
 */
static bool utp_record_tr_resp_upiu(UFSHCState *s, upiu_pkt *resp)
{
    uint8_t slot;
    utp_record *r;
    uint8_t tag = UPIU_TAG(resp);

    slot = search_tr_list(s, tag);
    if (slot == s->num_tr_slots) {
        return false;
    }

    r = &s->tr_list[slot].rec;
    r->dbo = UPIU_DBO(resp);
    r->dtc = UPIU_DTC(resp);
    r->dsl = UPIU_DSL(resp);
    r->ehs_len = UPIU_EHS_L(resp);
    r->resp_tt = UPIU_TT(resp);
    return true;
}

/*
 * utr complete
 * Copy the utp header into the TR slot
 * Clear the Door Bell register &
 * Free the prdt and list entry
 */
static void utr_complete(UFSHCState *s, uint8_t slot)
{
   hwaddr listBase = s->regs[R_UTRLBA] | (((uint64_t)s->regs[R_UTRLBAU]) << 32);

   address_space_rw(s->dma_as,
                    listBase + (sizeof(utp_tr_desc) * slot),
                    MEMTXATTRS_UNSPECIFIED,
                    &s->tr_list[slot].desc,
                    sizeof(utp_header), true);
   ARRAY_FIELD_DP32(s->regs, IS, UTRCS, 1);
   s->regs[R_UTRLDBR] &= ~(1 << slot);
   g_free(s->tr_list[slot].prdt);
   qemu_sglist_destroy(&s->tr_list[slot].sgl);
   memset(&s->tr_list[slot], 0, sizeof(tr_info));
}

/*
 * utmr complete
 * Copy the utp header into the TMR slot
 * Clear the Door Bell register &
 * Free the prdt and list entry
 */
static void utmr_complete(UFSHCState *s, uint8_t slot)
{
    hwaddr listBase = s->regs[R_UTMRLBA] |
                      (((uint64_t)s->regs[R_UTMRLBAU]) << 32);
    address_space_rw(s->dma_as,
                    listBase + (sizeof(utp_tmr_desc) * slot),
                    MEMTXATTRS_UNSPECIFIED,
                    &s->tmr_list[slot].desc,
                    sizeof(utp_header), true);
    ARRAY_FIELD_DP32(s->regs, IS, UTMRCS, 1);
    s->regs[R_UTMRLDBR] &= ~(1 << slot);
    memset(&s->tmr_list[slot], 0, sizeof(tmr_info));
}

/*
 * Process the query response
 * Record the response packet in tmr_list
 */
static bool ufs_query_resp_process(UFSHCState *s, upiu_pkt *resp)
{
    uint8_t slot;
    utp_record *r;
    uint8_t tag = UPIU_TAG(resp);

    slot = search_tr_list(s, tag);
    if (slot == s->num_tr_slots) {
        return false;
    }

    r = &s->tr_list[slot].rec;
    r->dsl = UPIU_DSL(resp);
    r->resp_tt = UPIU_TT(resp);
    utp_tr_copy_resp(s, resp, slot);

    if (!r->dsl) {
        utr_complete(s, slot);
    }
    return true;
}

/*
 * Process the Read to transfer response
 */
static bool ufs_rtt_process(UFSHCState *s, upiu_pkt *resp)
{
    uint8_t tag = UPIU_TAG(resp);
    uint8_t slot = search_tr_list(s, tag);

    if (slot == s->num_tr_slots) {
        return false;
    }

    /*
     * Send DATA_OUT base on PRDT and RTT
     */
    return true;
}

/*
 * Process the task management response
 */
static bool ufs_tmr_resp_process(UFSHCState *s, upiu_pkt *resp)
{
    uint8_t slot;
    uint8_t tag = UPIU_TAG(resp);
    hwaddr listbase = s->regs[R_UTMRLBA] |
                      (((uint64_t)s->regs[R_UTMRLBAU]) << 32);

    slot = search_tmr_list(s, tag);
    if (slot >= s->num_tmr_slots) {
        return false;
    }

    address_space_rw(s->dma_as,
               listbase + sizeof(utp_tmr_desc) * slot + UTPTMR_RESP_UPIU_OFFSET,
               MEMTXATTRS_UNSPECIFIED,
               resp,
               sizeof(upiu_task_mng_resp), true);
    return true;
}

/*
 * receive_upiu
 * Process thed received UPIU Response form UFS device
 */
static void ufshc_receive_upiu(ufshcIF *ifs, upiu_pkt *pkt)
{
    UFSHCState *s = UFSHC(ifs);
    uint8_t tag = UPIU_TAG(pkt);
    uint8_t tt = UPIU_TT(pkt);
    uint8_t slot = search_tr_list(s, tag);

    /*
     * Receive response/DATA_IN/RTT/NOP_IN/REJECT UPIU's
     */
    trace_ufshc_tr_recv(UPIU_TT(pkt), UPIU_TAG(pkt));
    switch (tt) {
    case TRNS_DATA_IN:
        /*
         * Copy the data as per PRDT and Data-buffer-offset
         */
        if (!utp_record_tr_resp_upiu(s, pkt)) {
            /*
             * Error
             */
         }
        break;
    case TRNS_RDY_TO_TRANSFER:
        /*
         * Start sending DATA_OUT packets
         */
        if (!ufs_rtt_process(s, pkt)) {
            /*
             * Error
             */
         }
        break;
    case TRNS_TASK_MNG_RESP:
        ufs_tmr_resp_process(s, pkt);
        utmr_complete(s, slot);
        break;
    case TRNS_QUERY_RESP:
        ufs_query_resp_process(s, pkt);
        break;
    case TRNS_RESPONSE:
        /*
         * Copy the response
         */
        utp_record_tr_resp_upiu(s, pkt);
        utp_tr_copy_resp(s, pkt, slot);
        break;
    case TRNS_NOP_IN:
       /*
        * Copy the response
        * fall through
        */
    case TRNS_REJECT:
       /*
        * Copy the response
        */
       utp_tr_copy_resp(s, pkt, slot);
       utr_complete(s, slot);
       break;
    default:
       /*
        * Error No suppport added for this command.
        */
       qemu_log("No suppport added for this command %x\n", tt);
    };
    ufshc_irq_update(s);
}

/*
 * Handle DATA_IN UPIU
 */
static void utp_data_in(UFSHCState *s, uint8_t slot, void *data, uint16_t len)
{
    uint32_t size;
    uint16_t prdtl = s->tr_list[slot].desc.prdtl;
    /*
     * cp: Num copied bytes
     * cp_len: num bytes can be copied accord to prdt entry
     */
    uint16_t cp = 0, i, cp_len;
    ufs_prdt *prdt = s->tr_list[slot].prdt;
    uint32_t dbo =  s->tr_list[slot].rec.dbo;
    /*
     * offset: offset w.r.t combined prdt entries
     */
    uint32_t offset = 0;

    if (prdtl && prdt) {
        for (i = 0; cp < len && i < prdtl; i++) {
            /*
             * Check for Data Byte Count DWORD granularity
             */
            if (!(prdt[i].size & 0x3)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "PRDT Entry %d DBC should follow DWORD granularity", i);
                ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[slot].desc,
                         UTP_DW2, OCS, UTP_OCS_INVALID_PRDT_ATTRIBUTES);
            }
            size = (prdt[i].size & R_PRDT_DW3_DBC_MASK) | 0x3;
            if (!(dbo + cp < size + offset)) {
                offset += size ;
                continue;
            } else {
                cp_len = len - cp <= size + 1 ? len - cp : size + 1;
                /*
                 * Check for Data buffer DWORD granularity
                 */
                if (prdt[i].addrl & 0x3) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                     "PRDT Entry %d Buffer should be DWORD aligned", i);
                    ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[slot].desc,
                         UTP_DW2, OCS, UTP_OCS_INVALID_PRDT_ATTRIBUTES);
                }
                address_space_rw(s->dma_as,
                                 prdt[i].addrl | ((hwaddr) prdt[i].addrh << 32),
                                 MEMTXATTRS_UNSPECIFIED, (uint8_t *)data + cp,
                                 cp_len,
                                 true);
                cp += cp_len;
                offset += cp_len;
            }
            if (cp < len) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "PRDT Buffer is insufficient for data received");
                ARRAY_FIELD_DP32((uint32_t *)&s->tr_list[slot].desc,
                        UTP_DW2, OCS, UTP_OCS_FATAL_ERROR);
            }
        }
    }
}

/*
 * receive_data
 * Handle the data, which is part of UPIU response
 */
static void ufshc_receive_data(ufshcIF *ifs, void *data, uint16_t len,
                              uint8_t task_tag)
{
    UFSHCState *s = UFSHC(ifs);
    uint8_t slot = search_tr_list(s, task_tag);

    /*
     * Receive data segement
     * Match the task tag with tr_list.
     */
    switch (s->tr_list[slot].rec.resp_tt) {
    case TRNS_DATA_IN:
        utp_data_in(s, slot, data, len);
        break;
    case TRNS_QUERY_RESP:
    case TRNS_RESPONSE:
        utp_tr_copy_resp_data(s, data, len, slot);
        utr_complete(s, slot);
        break;
    default:
        qemu_log("Invalid data segment\n");
    };
}

static QEMUSGList *ufshc_get_sgl(ufshcIF *ifs, uint8_t task_tag)
{
    UFSHCState *s = UFSHC(ifs);
    uint8_t slot = search_tr_list(s, task_tag);

    if (slot == s->num_tr_slots ||
        (s->tr_list[slot].desc.prdtl == 0)) {
        return NULL;
    }

    return &s->tr_list[slot].sgl;
}

static void ufshc_set_upmcrs(ufshcIF *ifs, upmcrs status)
{
    UFSHCState *s = UFSHC(ifs);

    ARRAY_FIELD_DP32(s->regs, HCS, UPMCRS, status);
}

static void ufshc_reset_enter(Object *obj, ResetType type)
{
    UFSHCState *s = UFSHC(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    ARRAY_FIELD_DP32(s->regs, CAP, NUTRS, s->num_tr_slots - 1);
    ARRAY_FIELD_DP32(s->regs, CAP, NUTMRS, s->num_tmr_slots - 1);
    ARRAY_FIELD_DP32(s->regs, CAP, OODDS, s->oods);
    s->regs[R_VER] = s->ufshci_ver;
    ARRAY_FIELD_DP32(s->regs, HCDDID, DC, s->dc);
    ARRAY_FIELD_DP32(s->regs, HCDDID, HCDID, s->hcdid);
    ARRAY_FIELD_DP32(s->regs, HCPMID, MID, s->mid);
    ARRAY_FIELD_DP32(s->regs, HCPMID, PID, s->pid);
}

static void ufshc_sysbus_reset_enter(Object *obj, ResetType type)
{
    UFSHCSysbus *s = UFSHC_SYSBUS(obj);

    ufshc_reset_enter(OBJECT(&s->ufshc), RESET_TYPE_COLD);
}

static const MemoryRegionOps ufshc_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void ufshc_realize(DeviceState *dev, Error **errp)
{
    UFSHCState *s = UFSHC(dev);

    g_assert(s->num_tr_slots <= MAX_TR);
    g_assert(s->num_tmr_slots <= MAX_TMR);
}

static void ufshc_sysbus_realize(DeviceState *dev, Error **errp)
{
    UFSHCSysbus *s = UFSHC_SYSBUS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    object_property_set_link(OBJECT(s->ufsdev), "ufs-initiator",
                             OBJECT(&s->ufshc), NULL);
    qdev_set_parent_bus(DEVICE(s->ufsdev), BUS(s->bus), NULL);
    object_property_set_link(OBJECT(&s->ufshc), "ufs-target",
                             OBJECT(s->ufsdev), NULL);

    if (!qdev_realize(DEVICE(&s->ufshc), NULL, errp)) {
        return;
    }
    if (s->dma_mr) {
        s->ufshc.dma_as = g_malloc0(sizeof(AddressSpace));
        address_space_init(s->ufshc.dma_as, s->dma_mr, NULL);
    } else {
        s->ufshc.dma_as = &address_space_memory;
    }
    sysbus_init_mmio(sbd, &s->ufshc.iomem);
    sysbus_init_irq(sbd, &s->ufshc.irq);
}

static void ufshc_instance_init(Object *obj)
{
    UFSHCState *s = UFSHC(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, "ufshc-mem", R_MAX * 4);

    reg_array = register_init_block32(DEVICE(obj), ufshc_reg_info,
                                      ARRAY_SIZE(ufshc_reg_info),
                                      s->regs_info, s->regs,
                                      &ufshc_ops,
                                      UFSHC_ERR_DEBUG,
                                      R_MAX * 4);
    memory_region_add_subregion(&s->iomem, 0, &reg_array->mem);

    object_property_add_link(obj, "ufs-target", TYPE_UFSHC_IF,
                             (Object **)&s->ufs_target,
                              qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "unipro-mphy", TYPE_UFSHC_IF,
                             (Object **)&s->unipro,
                              qdev_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);

}

static void ufshc_sysbus_instance_init(Object *obj)
{
    UFSHCSysbus *s = UFSHC_SYSBUS(obj);

    object_initialize_child(obj, "ufshc-target", &s->ufshc, TYPE_UFSHC);
    object_property_add_link(obj, "ufs-target", TYPE_UFS_DEV,
                             (Object **)&s->ufsdev,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    qdev_alias_all_properties(DEVICE(&s->ufshc), obj);
    object_property_add_alias(obj, "unipro-mphy", OBJECT(&s->ufshc),
                                "unipro-mphy");
    s->bus = UFS_BUS(qbus_new(TYPE_UFS_BUS, DEVICE(obj), NULL));
}

static Property ufshc_props[] = {
    DEFINE_PROP_UINT8("num-tr-slots", UFSHCState, num_tr_slots, MAX_TR),
    DEFINE_PROP_UINT8("num-tmr-slots", UFSHCState, num_tmr_slots, MAX_TMR),
    DEFINE_PROP_BOOL("oods", UFSHCState, oods, false),
    DEFINE_PROP_UINT32("ufshci-version", UFSHCState, ufshci_ver, 0x300),
    DEFINE_PROP_UINT8("hcdid", UFSHCState, hcdid, 0),
    DEFINE_PROP_UINT16("dc", UFSHCState, dc, 0),
    DEFINE_PROP_UINT16("mid", UFSHCState, mid, 0),
    DEFINE_PROP_UINT16("pid", UFSHCState, pid, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ufshc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ufshcIFClass *uc = UFSHC_IF_CLASS(klass);

    dc->realize = ufshc_realize;
    device_class_set_props(dc, ufshc_props);
    rc->phases.enter = ufshc_reset_enter;
    uc->handle_upiu = ufshc_receive_upiu;
    uc->handle_data = ufshc_receive_data;
    uc->get_sgl = ufshc_get_sgl;
    uc->pwr_mode_status = ufshc_set_upmcrs;
}

static void ufshc_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = ufshc_sysbus_realize;
    rc->phases.enter = ufshc_sysbus_reset_enter;
}

static const TypeInfo ufshc_sysbus_info = {
    .name = TYPE_SYSBUS_UFSHC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UFSHCSysbus),
    .class_init = ufshc_sysbus_class_init,
    .instance_init = ufshc_sysbus_instance_init
};

static const TypeInfo ufshc_info = {
    .name          = TYPE_UFSHC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(UFSHCState),
    .class_init    = ufshc_class_init,
    .instance_init = ufshc_instance_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_UFSHC_IF },
        { },
    },
};

static void ufshc_types(void)
{
    type_register_static(&ufshc_info);
    type_register_static(&ufshc_sysbus_info);
}

type_init(ufshc_types)
