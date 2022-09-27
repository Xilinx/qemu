/*
 * ITS emulation for a GICv3-based system
 *
 * Copyright Linaro.org 2021
 *
 * Authors:
 *  Shashi Mallela <shashi.mallela@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "gicv3_internal.h"
#include "qom/object.h"
#include "qapi/error.h"

typedef struct GICv3ITSClass GICv3ITSClass;
/* This is reusing the GICv3ITSState typedef from ARM_GICV3_ITS_COMMON */
DECLARE_OBJ_CHECKERS(GICv3ITSState, GICv3ITSClass,
                     ARM_GICV3_ITS, TYPE_ARM_GICV3_ITS)

struct GICv3ITSClass {
    GICv3ITSCommonClass parent_class;
    void (*parent_reset)(DeviceState *dev);
};

/*
 * This is an internal enum used to distinguish between LPI triggered
 * via command queue and LPI triggered via gits_translater write.
 */
typedef enum ItsCmdType {
    NONE = 0, /* internal indication for GITS_TRANSLATER write */
    CLEAR = 1,
    DISCARD = 2,
    INTERRUPT = 3,
} ItsCmdType;

typedef struct {
    uint32_t iteh;
    uint64_t itel;
} IteEntry;

/*
 * The ITS spec permits a range of CONSTRAINED UNPREDICTABLE options
 * if a command parameter is not correct. These include both "stall
 * processing of the command queue" and "ignore this command, and
 * keep processing the queue". In our implementation we choose that
 * memory transaction errors reading the command packet provoke a
 * stall, but errors in parameters cause us to ignore the command
 * and continue processing.
 * The process_* functions which handle individual ITS commands all
 * return an ItsCmdResult which tells process_cmdq() whether it should
 * stall or keep going.
 */
typedef enum ItsCmdResult {
    CMD_STALL = 0,
    CMD_CONTINUE = 1,
} ItsCmdResult;

static uint64_t baser_base_addr(uint64_t value, uint32_t page_sz)
{
    uint64_t result = 0;

    switch (page_sz) {
    case GITS_PAGE_SIZE_4K:
    case GITS_PAGE_SIZE_16K:
        result = FIELD_EX64(value, GITS_BASER, PHYADDR) << 12;
        break;

    case GITS_PAGE_SIZE_64K:
        result = FIELD_EX64(value, GITS_BASER, PHYADDRL_64K) << 16;
        result |= FIELD_EX64(value, GITS_BASER, PHYADDRH_64K) << 48;
        break;

    default:
        break;
    }
    return result;
}

static uint64_t table_entry_addr(GICv3ITSState *s, TableDesc *td,
                                 uint32_t idx, MemTxResult *res)
{
    /*
     * Given a TableDesc describing one of the ITS in-guest-memory
     * tables and an index into it, return the guest address
     * corresponding to that table entry.
     * If there was a memory error reading the L1 table of an
     * indirect table, *res is set accordingly, and we return -1.
     * If the L1 table entry is marked not valid, we return -1 with
     * *res set to MEMTX_OK.
     *
     * The specification defines the format of level 1 entries of a
     * 2-level table, but the format of level 2 entries and the format
     * of flat-mapped tables is IMPDEF.
     */
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t l2idx;
    uint64_t l2;
    uint32_t num_l2_entries;

    *res = MEMTX_OK;

    if (!td->indirect) {
        /* Single level table */
        return td->base_addr + idx * td->entry_sz;
    }

    /* Two level table */
    l2idx = idx / (td->page_sz / L1TABLE_ENTRY_SIZE);

    l2 = address_space_ldq_le(as,
                              td->base_addr + (l2idx * L1TABLE_ENTRY_SIZE),
                              MEMTXATTRS_UNSPECIFIED, res);
    if (*res != MEMTX_OK) {
        return -1;
    }
    if (!(l2 & L2_TABLE_VALID_MASK)) {
        return -1;
    }

    num_l2_entries = td->page_sz / td->entry_sz;
    return (l2 & ((1ULL << 51) - 1)) + (idx % num_l2_entries) * td->entry_sz;
}

static bool get_cte(GICv3ITSState *s, uint16_t icid, uint64_t *cte,
                    MemTxResult *res)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t entry_addr = table_entry_addr(s, &s->ct, icid, res);

    if (entry_addr == -1) {
        return false; /* not valid */
    }

    *cte = address_space_ldq_le(as, entry_addr, MEMTXATTRS_UNSPECIFIED, res);
    return FIELD_EX64(*cte, CTE, VALID);
}

static bool update_ite(GICv3ITSState *s, uint32_t eventid, uint64_t dte,
                       IteEntry ite)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t itt_addr;
    MemTxResult res = MEMTX_OK;

    itt_addr = FIELD_EX64(dte, DTE, ITTADDR);
    itt_addr <<= ITTADDR_SHIFT; /* 256 byte aligned */

    address_space_stq_le(as, itt_addr + (eventid * (sizeof(uint64_t) +
                         sizeof(uint32_t))), ite.itel, MEMTXATTRS_UNSPECIFIED,
                         &res);

    if (res == MEMTX_OK) {
        address_space_stl_le(as, itt_addr + (eventid * (sizeof(uint64_t) +
                             sizeof(uint32_t))) + sizeof(uint32_t), ite.iteh,
                             MEMTXATTRS_UNSPECIFIED, &res);
    }
    if (res != MEMTX_OK) {
        return false;
    } else {
        return true;
    }
}

static bool get_ite(GICv3ITSState *s, uint32_t eventid, uint64_t dte,
                    uint16_t *icid, uint32_t *pIntid, MemTxResult *res)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t itt_addr;
    bool status = false;
    IteEntry ite = {};

    itt_addr = FIELD_EX64(dte, DTE, ITTADDR);
    itt_addr <<= ITTADDR_SHIFT; /* 256 byte aligned */

    ite.itel = address_space_ldq_le(as, itt_addr +
                                    (eventid * (sizeof(uint64_t) +
                                    sizeof(uint32_t))), MEMTXATTRS_UNSPECIFIED,
                                    res);

    if (*res == MEMTX_OK) {
        ite.iteh = address_space_ldl_le(as, itt_addr +
                                        (eventid * (sizeof(uint64_t) +
                                        sizeof(uint32_t))) + sizeof(uint32_t),
                                        MEMTXATTRS_UNSPECIFIED, res);

        if (*res == MEMTX_OK) {
            if (FIELD_EX64(ite.itel, ITE_L, VALID)) {
                int inttype = FIELD_EX64(ite.itel, ITE_L, INTTYPE);
                if (inttype == ITE_INTTYPE_PHYSICAL) {
                    *pIntid = FIELD_EX64(ite.itel, ITE_L, INTID);
                    *icid = FIELD_EX32(ite.iteh, ITE_H, ICID);
                    status = true;
                }
            }
        }
    }
    return status;
}

static uint64_t get_dte(GICv3ITSState *s, uint32_t devid, MemTxResult *res)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t entry_addr = table_entry_addr(s, &s->dt, devid, res);

    if (entry_addr == -1) {
        return 0; /* a DTE entry with the Valid bit clear */
    }
    return address_space_ldq_le(as, entry_addr, MEMTXATTRS_UNSPECIFIED, res);
}

/*
 * This function handles the processing of following commands based on
 * the ItsCmdType parameter passed:-
 * 1. triggering of lpi interrupt translation via ITS INT command
 * 2. triggering of lpi interrupt translation via gits_translater register
 * 3. handling of ITS CLEAR command
 * 4. handling of ITS DISCARD command
 */
static ItsCmdResult process_its_cmd(GICv3ITSState *s, uint64_t value,
                                    uint32_t offset, ItsCmdType cmd)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t devid, eventid;
    MemTxResult res = MEMTX_OK;
    bool dte_valid;
    uint64_t dte = 0;
    uint64_t num_eventids;
    uint16_t icid = 0;
    uint32_t pIntid = 0;
    bool ite_valid = false;
    uint64_t cte = 0;
    bool cte_valid = false;
    uint64_t rdbase;

    if (cmd == NONE) {
        devid = offset;
    } else {
        devid = ((value & DEVID_MASK) >> DEVID_SHIFT);

        offset += NUM_BYTES_IN_DW;
        value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                     MEMTXATTRS_UNSPECIFIED, &res);
    }

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    eventid = (value & EVENTID_MASK);

    if (devid >= s->dt.num_ids) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes: devid %d>=%d",
                      __func__, devid, s->dt.num_ids);
        return CMD_CONTINUE;
    }

    dte = get_dte(s, devid, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }
    dte_valid = FIELD_EX64(dte, DTE, VALID);

    if (!dte_valid) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes: "
                      "invalid dte: %"PRIx64" for %d\n",
                      __func__, dte, devid);
        return CMD_CONTINUE;
    }

    num_eventids = 1ULL << (FIELD_EX64(dte, DTE, SIZE) + 1);

    if (eventid >= num_eventids) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes: eventid %d >= %"
                      PRId64 "\n",
                      __func__, eventid, num_eventids);
        return CMD_CONTINUE;
    }

    ite_valid = get_ite(s, eventid, dte, &icid, &pIntid, &res);
    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    if (!ite_valid) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes: invalid ITE\n",
                      __func__);
        return CMD_CONTINUE;
    }

    if (icid >= s->ct.num_ids) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid ICID 0x%x in ITE (table corrupted?)\n",
                      __func__, icid);
        return CMD_CONTINUE;
    }

    cte_valid = get_cte(s, icid, &cte, &res);
    if (res != MEMTX_OK) {
        return CMD_STALL;
    }
    if (!cte_valid) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes: "
                      "invalid cte: %"PRIx64"\n",
                      __func__, cte);
        return CMD_CONTINUE;
    }

    /*
     * Current implementation only supports rdbase == procnum
     * Hence rdbase physical address is ignored
     */
    rdbase = FIELD_EX64(cte, CTE, RDBASE);

    if (rdbase >= s->gicv3->num_cpu) {
        return CMD_CONTINUE;
    }

    if ((cmd == CLEAR) || (cmd == DISCARD)) {
        gicv3_redist_process_lpi(&s->gicv3->cpu[rdbase], pIntid, 0);
    } else {
        gicv3_redist_process_lpi(&s->gicv3->cpu[rdbase], pIntid, 1);
    }

    if (cmd == DISCARD) {
        IteEntry ite = {};
        /* remove mapping from interrupt translation table */
        return update_ite(s, eventid, dte, ite) ? CMD_CONTINUE : CMD_STALL;
    }
    return CMD_CONTINUE;
}

static ItsCmdResult process_mapti(GICv3ITSState *s, uint64_t value,
                                  uint32_t offset, bool ignore_pInt)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t devid, eventid;
    uint32_t pIntid = 0;
    uint64_t num_eventids;
    uint32_t num_intids;
    bool dte_valid;
    MemTxResult res = MEMTX_OK;
    uint16_t icid = 0;
    uint64_t dte = 0;
    IteEntry ite = {};

    devid = ((value & DEVID_MASK) >> DEVID_SHIFT);
    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    eventid = (value & EVENTID_MASK);

    if (ignore_pInt) {
        pIntid = eventid;
    } else {
        pIntid = ((value & pINTID_MASK) >> pINTID_SHIFT);
    }

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    icid = value & ICID_MASK;

    if (devid >= s->dt.num_ids) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes: devid %d>=%d",
                      __func__, devid, s->dt.num_ids);
        return CMD_CONTINUE;
    }

    dte = get_dte(s, devid, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }
    dte_valid = FIELD_EX64(dte, DTE, VALID);
    num_eventids = 1ULL << (FIELD_EX64(dte, DTE, SIZE) + 1);
    num_intids = 1ULL << (GICD_TYPER_IDBITS + 1);

    if ((icid >= s->ct.num_ids)
            || !dte_valid || (eventid >= num_eventids) ||
            (((pIntid < GICV3_LPI_INTID_START) || (pIntid >= num_intids)) &&
             (pIntid != INTID_SPURIOUS))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid command attributes "
                      "icid %d or eventid %d or pIntid %d or"
                      "unmapped dte %d\n", __func__, icid, eventid,
                      pIntid, dte_valid);
        /*
         * in this implementation, in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
        return CMD_CONTINUE;
    }

    /* add ite entry to interrupt translation table */
    ite.itel = FIELD_DP64(ite.itel, ITE_L, VALID, dte_valid);
    ite.itel = FIELD_DP64(ite.itel, ITE_L, INTTYPE, ITE_INTTYPE_PHYSICAL);
    ite.itel = FIELD_DP64(ite.itel, ITE_L, INTID, pIntid);
    ite.itel = FIELD_DP64(ite.itel, ITE_L, DOORBELL, INTID_SPURIOUS);
    ite.iteh = FIELD_DP32(ite.iteh, ITE_H, ICID, icid);

    return update_ite(s, eventid, dte, ite) ? CMD_CONTINUE : CMD_STALL;
}

static bool update_cte(GICv3ITSState *s, uint16_t icid, bool valid,
                       uint64_t rdbase)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t entry_addr;
    uint64_t cte = 0;
    MemTxResult res = MEMTX_OK;

    if (!s->ct.valid) {
        return true;
    }

    if (valid) {
        /* add mapping entry to collection table */
        cte = FIELD_DP64(cte, CTE, VALID, 1);
        cte = FIELD_DP64(cte, CTE, RDBASE, rdbase);
    }

    entry_addr = table_entry_addr(s, &s->ct, icid, &res);
    if (res != MEMTX_OK) {
        /* memory access error: stall */
        return false;
    }
    if (entry_addr == -1) {
        /* No L2 table for this index: discard write and continue */
        return true;
    }

    address_space_stq_le(as, entry_addr, cte, MEMTXATTRS_UNSPECIFIED, &res);
    return res == MEMTX_OK;
}

static ItsCmdResult process_mapc(GICv3ITSState *s, uint32_t offset)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint16_t icid;
    uint64_t rdbase;
    bool valid;
    MemTxResult res = MEMTX_OK;
    uint64_t value;

    offset += NUM_BYTES_IN_DW;
    offset += NUM_BYTES_IN_DW;

    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    icid = value & ICID_MASK;

    rdbase = (value & R_MAPC_RDBASE_MASK) >> R_MAPC_RDBASE_SHIFT;
    rdbase &= RDBASE_PROCNUM_MASK;

    valid = (value & CMD_FIELD_VALID_MASK);

    if ((icid >= s->ct.num_ids) || (rdbase >= s->gicv3->num_cpu)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ITS MAPC: invalid collection table attributes "
                      "icid %d rdbase %" PRIu64 "\n",  icid, rdbase);
        /*
         * in this implementation, in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
        return CMD_CONTINUE;
    }

    return update_cte(s, icid, valid, rdbase) ? CMD_CONTINUE : CMD_STALL;
}

static bool update_dte(GICv3ITSState *s, uint32_t devid, bool valid,
                       uint8_t size, uint64_t itt_addr)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint64_t entry_addr;
    uint64_t dte = 0;
    MemTxResult res = MEMTX_OK;

    if (s->dt.valid) {
        if (valid) {
            /* add mapping entry to device table */
            dte = FIELD_DP64(dte, DTE, VALID, 1);
            dte = FIELD_DP64(dte, DTE, SIZE, size);
            dte = FIELD_DP64(dte, DTE, ITTADDR, itt_addr);
        }
    } else {
        return true;
    }

    entry_addr = table_entry_addr(s, &s->dt, devid, &res);
    if (res != MEMTX_OK) {
        /* memory access error: stall */
        return false;
    }
    if (entry_addr == -1) {
        /* No L2 table for this index: discard write and continue */
        return true;
    }
    address_space_stq_le(as, entry_addr, dte, MEMTXATTRS_UNSPECIFIED, &res);
    return res == MEMTX_OK;
}

static ItsCmdResult process_mapd(GICv3ITSState *s, uint64_t value,
                                 uint32_t offset)
{
    AddressSpace *as = &s->gicv3->dma_as;
    uint32_t devid;
    uint8_t size;
    uint64_t itt_addr;
    bool valid;
    MemTxResult res = MEMTX_OK;

    devid = ((value & DEVID_MASK) >> DEVID_SHIFT);

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    size = (value & SIZE_MASK);

    offset += NUM_BYTES_IN_DW;
    value = address_space_ldq_le(as, s->cq.base_addr + offset,
                                 MEMTXATTRS_UNSPECIFIED, &res);

    if (res != MEMTX_OK) {
        return CMD_STALL;
    }

    itt_addr = (value & ITTADDR_MASK) >> ITTADDR_SHIFT;

    valid = (value & CMD_FIELD_VALID_MASK);

    if ((devid >= s->dt.num_ids) ||
        (size > FIELD_EX64(s->typer, GITS_TYPER, IDBITS))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ITS MAPD: invalid device table attributes "
                      "devid %d or size %d\n", devid, size);
        /*
         * in this implementation, in case of error
         * we ignore this command and move onto the next
         * command in the queue
         */
        return CMD_CONTINUE;
    }

    return update_dte(s, devid, valid, size, itt_addr) ? CMD_CONTINUE : CMD_STALL;
}

/*
 * Current implementation blocks until all
 * commands are processed
 */
static void process_cmdq(GICv3ITSState *s)
{
    uint32_t wr_offset = 0;
    uint32_t rd_offset = 0;
    uint32_t cq_offset = 0;
    uint64_t data;
    AddressSpace *as = &s->gicv3->dma_as;
    MemTxResult res = MEMTX_OK;
    uint8_t cmd;
    int i;

    if (!(s->ctlr & R_GITS_CTLR_ENABLED_MASK)) {
        return;
    }

    wr_offset = FIELD_EX64(s->cwriter, GITS_CWRITER, OFFSET);

    if (wr_offset >= s->cq.num_entries) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write offset "
                      "%d\n", __func__, wr_offset);
        return;
    }

    rd_offset = FIELD_EX64(s->creadr, GITS_CREADR, OFFSET);

    if (rd_offset >= s->cq.num_entries) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read offset "
                      "%d\n", __func__, rd_offset);
        return;
    }

    while (wr_offset != rd_offset) {
        ItsCmdResult result = CMD_CONTINUE;

        cq_offset = (rd_offset * GITS_CMDQ_ENTRY_SIZE);
        data = address_space_ldq_le(as, s->cq.base_addr + cq_offset,
                                    MEMTXATTRS_UNSPECIFIED, &res);
        if (res != MEMTX_OK) {
            s->creadr = FIELD_DP64(s->creadr, GITS_CREADR, STALLED, 1);
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: could not read command at 0x%" PRIx64 "\n",
                          __func__, s->cq.base_addr + cq_offset);
            break;
        }

        cmd = (data & CMD_MASK);

        switch (cmd) {
        case GITS_CMD_INT:
            result = process_its_cmd(s, data, cq_offset, INTERRUPT);
            break;
        case GITS_CMD_CLEAR:
            result = process_its_cmd(s, data, cq_offset, CLEAR);
            break;
        case GITS_CMD_SYNC:
            /*
             * Current implementation makes a blocking synchronous call
             * for every command issued earlier, hence the internal state
             * is already consistent by the time SYNC command is executed.
             * Hence no further processing is required for SYNC command.
             */
            break;
        case GITS_CMD_MAPD:
            result = process_mapd(s, data, cq_offset);
            break;
        case GITS_CMD_MAPC:
            result = process_mapc(s, cq_offset);
            break;
        case GITS_CMD_MAPTI:
            result = process_mapti(s, data, cq_offset, false);
            break;
        case GITS_CMD_MAPI:
            result = process_mapti(s, data, cq_offset, true);
            break;
        case GITS_CMD_DISCARD:
            result = process_its_cmd(s, data, cq_offset, DISCARD);
            break;
        case GITS_CMD_INV:
        case GITS_CMD_INVALL:
            /*
             * Current implementation doesn't cache any ITS tables,
             * but the calculated lpi priority information. We only
             * need to trigger lpi priority re-calculation to be in
             * sync with LPI config table or pending table changes.
             */
            for (i = 0; i < s->gicv3->num_cpu; i++) {
                gicv3_redist_update_lpi(&s->gicv3->cpu[i]);
            }
            break;
        default:
            break;
        }
        if (result == CMD_CONTINUE) {
            rd_offset++;
            rd_offset %= s->cq.num_entries;
            s->creadr = FIELD_DP64(s->creadr, GITS_CREADR, OFFSET, rd_offset);
        } else {
            /* CMD_STALL */
            s->creadr = FIELD_DP64(s->creadr, GITS_CREADR, STALLED, 1);
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: 0x%x cmd processing failed, stalling\n",
                          __func__, cmd);
            break;
        }
    }
}

/*
 * This function extracts the ITS Device and Collection table specific
 * parameters (like base_addr, size etc) from GITS_BASER register.
 * It is called during ITS enable and also during post_load migration
 */
static void extract_table_params(GICv3ITSState *s)
{
    uint16_t num_pages = 0;
    uint8_t  page_sz_type;
    uint8_t type;
    uint32_t page_sz = 0;
    uint64_t value;

    for (int i = 0; i < 8; i++) {
        TableDesc *td;
        int idbits;

        value = s->baser[i];

        if (!value) {
            continue;
        }

        page_sz_type = FIELD_EX64(value, GITS_BASER, PAGESIZE);

        switch (page_sz_type) {
        case 0:
            page_sz = GITS_PAGE_SIZE_4K;
            break;

        case 1:
            page_sz = GITS_PAGE_SIZE_16K;
            break;

        case 2:
        case 3:
            page_sz = GITS_PAGE_SIZE_64K;
            break;

        default:
            g_assert_not_reached();
        }

        num_pages = FIELD_EX64(value, GITS_BASER, SIZE) + 1;

        type = FIELD_EX64(value, GITS_BASER, TYPE);

        switch (type) {
        case GITS_BASER_TYPE_DEVICE:
            td = &s->dt;
            idbits = FIELD_EX64(s->typer, GITS_TYPER, DEVBITS) + 1;
            break;
        case GITS_BASER_TYPE_COLLECTION:
            td = &s->ct;
            if (FIELD_EX64(s->typer, GITS_TYPER, CIL)) {
                idbits = FIELD_EX64(s->typer, GITS_TYPER, CIDBITS) + 1;
            } else {
                /* 16-bit CollectionId supported when CIL == 0 */
                idbits = 16;
            }
            break;
        default:
            /*
             * GITS_BASER<n>.TYPE is read-only, so GITS_BASER_RO_MASK
             * ensures we will only see type values corresponding to
             * the values set up in gicv3_its_reset().
             */
            g_assert_not_reached();
        }

        memset(td, 0, sizeof(*td));
        td->valid = FIELD_EX64(value, GITS_BASER, VALID);
        /*
         * If GITS_BASER<n>.Valid is 0 for any <n> then we will not process
         * interrupts. (GITS_TYPER.HCC is 0 for this implementation, so we
         * do not have a special case where the GITS_BASER<n>.Valid bit is 0
         * for the register corresponding to the Collection table but we
         * still have to process interrupts using non-memory-backed
         * Collection table entries.)
         */
        if (!td->valid) {
            continue;
        }
        td->page_sz = page_sz;
        td->indirect = FIELD_EX64(value, GITS_BASER, INDIRECT);
        td->entry_sz = FIELD_EX64(value, GITS_BASER, ENTRYSIZE) + 1;
        td->base_addr = baser_base_addr(value, page_sz);
        if (!td->indirect) {
            td->num_entries = (num_pages * page_sz) / td->entry_sz;
        } else {
            td->num_entries = (((num_pages * page_sz) /
                                  L1TABLE_ENTRY_SIZE) *
                                 (page_sz / td->entry_sz));
        }
        td->num_ids = 1ULL << idbits;
    }
}

static void extract_cmdq_params(GICv3ITSState *s)
{
    uint16_t num_pages = 0;
    uint64_t value = s->cbaser;

    num_pages = FIELD_EX64(value, GITS_CBASER, SIZE) + 1;

    memset(&s->cq, 0 , sizeof(s->cq));
    s->cq.valid = FIELD_EX64(value, GITS_CBASER, VALID);

    if (s->cq.valid) {
        s->cq.num_entries = (num_pages * GITS_PAGE_SIZE_4K) /
                             GITS_CMDQ_ENTRY_SIZE;
        s->cq.base_addr = FIELD_EX64(value, GITS_CBASER, PHYADDR);
        s->cq.base_addr <<= R_GITS_CBASER_PHYADDR_SHIFT;
    }
}

static MemTxResult gicv3_its_translation_write(void *opaque, hwaddr offset,
                                               uint64_t data, unsigned size,
                                               MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    bool result = true;
    uint32_t devid = 0;

    switch (offset) {
    case GITS_TRANSLATER:
        if (s->ctlr & R_GITS_CTLR_ENABLED_MASK) {
            devid = attrs.requester_id;
            result = process_its_cmd(s, data, devid, NONE);
        }
        break;
    default:
        break;
    }

    if (result) {
        return MEMTX_OK;
    } else {
        return MEMTX_ERROR;
    }
}

static bool its_writel(GICv3ITSState *s, hwaddr offset,
                              uint64_t value, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_CTLR:
        if (value & R_GITS_CTLR_ENABLED_MASK) {
            s->ctlr |= R_GITS_CTLR_ENABLED_MASK;
            extract_table_params(s);
            extract_cmdq_params(s);
            s->creadr = 0;
            process_cmdq(s);
        } else {
            s->ctlr &= ~R_GITS_CTLR_ENABLED_MASK;
        }
        break;
    case GITS_CBASER:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & R_GITS_CTLR_ENABLED_MASK)) {
            s->cbaser = deposit64(s->cbaser, 0, 32, value);
            s->creadr = 0;
            s->cwriter = s->creadr;
        }
        break;
    case GITS_CBASER + 4:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & R_GITS_CTLR_ENABLED_MASK)) {
            s->cbaser = deposit64(s->cbaser, 32, 32, value);
            s->creadr = 0;
            s->cwriter = s->creadr;
        }
        break;
    case GITS_CWRITER:
        s->cwriter = deposit64(s->cwriter, 0, 32,
                               (value & ~R_GITS_CWRITER_RETRY_MASK));
        if (s->cwriter != s->creadr) {
            process_cmdq(s);
        }
        break;
    case GITS_CWRITER + 4:
        s->cwriter = deposit64(s->cwriter, 32, 32, value);
        break;
    case GITS_CREADR:
        if (s->gicv3->gicd_ctlr & GICD_CTLR_DS) {
            s->creadr = deposit64(s->creadr, 0, 32,
                                  (value & ~R_GITS_CREADR_STALLED_MASK));
        } else {
            /* RO register, ignore the write */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid guest write to RO register at offset "
                          TARGET_FMT_plx "\n", __func__, offset);
        }
        break;
    case GITS_CREADR + 4:
        if (s->gicv3->gicd_ctlr & GICD_CTLR_DS) {
            s->creadr = deposit64(s->creadr, 32, 32, value);
        } else {
            /* RO register, ignore the write */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid guest write to RO register at offset "
                          TARGET_FMT_plx "\n", __func__, offset);
        }
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        /*
         * IMPDEF choice:- GITS_BASERn register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & R_GITS_CTLR_ENABLED_MASK)) {
            index = (offset - GITS_BASER) / 8;

            if (offset & 7) {
                value <<= 32;
                value &= ~GITS_BASER_RO_MASK;
                s->baser[index] &= GITS_BASER_RO_MASK | MAKE_64BIT_MASK(0, 32);
                s->baser[index] |= value;
            } else {
                value &= ~GITS_BASER_RO_MASK;
                s->baser[index] &= GITS_BASER_RO_MASK | MAKE_64BIT_MASK(32, 32);
                s->baser[index] |= value;
            }
        }
        break;
    case GITS_IIDR:
    case GITS_IDREGS ... GITS_IDREGS + 0x2f:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static bool its_readl(GICv3ITSState *s, hwaddr offset,
                             uint64_t *data, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_CTLR:
        *data = s->ctlr;
        break;
    case GITS_IIDR:
        *data = gicv3_iidr();
        break;
    case GITS_IDREGS ... GITS_IDREGS + 0x2f:
        /* ID registers */
        *data = gicv3_idreg(offset - GITS_IDREGS);
        break;
    case GITS_TYPER:
        *data = extract64(s->typer, 0, 32);
        break;
    case GITS_TYPER + 4:
        *data = extract64(s->typer, 32, 32);
        break;
    case GITS_CBASER:
        *data = extract64(s->cbaser, 0, 32);
        break;
    case GITS_CBASER + 4:
        *data = extract64(s->cbaser, 32, 32);
        break;
    case GITS_CREADR:
        *data = extract64(s->creadr, 0, 32);
        break;
    case GITS_CREADR + 4:
        *data = extract64(s->creadr, 32, 32);
        break;
    case GITS_CWRITER:
        *data = extract64(s->cwriter, 0, 32);
        break;
    case GITS_CWRITER + 4:
        *data = extract64(s->cwriter, 32, 32);
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        index = (offset - GITS_BASER) / 8;
        if (offset & 7) {
            *data = extract64(s->baser[index], 32, 32);
        } else {
            *data = extract64(s->baser[index], 0, 32);
        }
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static bool its_writell(GICv3ITSState *s, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_BASER ... GITS_BASER + 0x3f:
        /*
         * IMPDEF choice:- GITS_BASERn register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & R_GITS_CTLR_ENABLED_MASK)) {
            index = (offset - GITS_BASER) / 8;
            s->baser[index] &= GITS_BASER_RO_MASK;
            s->baser[index] |= (value & ~GITS_BASER_RO_MASK);
        }
        break;
    case GITS_CBASER:
        /*
         * IMPDEF choice:- GITS_CBASER register becomes RO if ITS is
         *                 already enabled
         */
        if (!(s->ctlr & R_GITS_CTLR_ENABLED_MASK)) {
            s->cbaser = value;
            s->creadr = 0;
            s->cwriter = s->creadr;
        }
        break;
    case GITS_CWRITER:
        s->cwriter = value & ~R_GITS_CWRITER_RETRY_MASK;
        if (s->cwriter != s->creadr) {
            process_cmdq(s);
        }
        break;
    case GITS_CREADR:
        if (s->gicv3->gicd_ctlr & GICD_CTLR_DS) {
            s->creadr = value & ~R_GITS_CREADR_STALLED_MASK;
        } else {
            /* RO register, ignore the write */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid guest write to RO register at offset "
                          TARGET_FMT_plx "\n", __func__, offset);
        }
        break;
    case GITS_TYPER:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static bool its_readll(GICv3ITSState *s, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    bool result = true;
    int index;

    switch (offset) {
    case GITS_TYPER:
        *data = s->typer;
        break;
    case GITS_BASER ... GITS_BASER + 0x3f:
        index = (offset - GITS_BASER) / 8;
        *data = s->baser[index];
        break;
    case GITS_CBASER:
        *data = s->cbaser;
        break;
    case GITS_CREADR:
        *data = s->creadr;
        break;
    case GITS_CWRITER:
        *data = s->cwriter;
        break;
    default:
        result = false;
        break;
    }
    return result;
}

static MemTxResult gicv3_its_read(void *opaque, hwaddr offset, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    bool result;

    switch (size) {
    case 4:
        result = its_readl(s, offset, data, attrs);
        break;
    case 8:
        result = its_readll(s, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use false returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        *data = 0;
    }
    return MEMTX_OK;
}

static MemTxResult gicv3_its_write(void *opaque, hwaddr offset, uint64_t data,
                                   unsigned size, MemTxAttrs attrs)
{
    GICv3ITSState *s = (GICv3ITSState *)opaque;
    bool result;

    switch (size) {
    case 4:
        result = its_writel(s, offset, data, attrs);
        break;
    case 8:
        result = its_writell(s, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write at offset " TARGET_FMT_plx
                      "size %u\n", __func__, offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI;
         * so use false returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
    }
    return MEMTX_OK;
}

static const MemoryRegionOps gicv3_its_control_ops = {
    .read_with_attrs = gicv3_its_read,
    .write_with_attrs = gicv3_its_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .impl.min_access_size = 4,
    .impl.max_access_size = 8,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps gicv3_its_translation_ops = {
    .write_with_attrs = gicv3_its_translation_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
    .impl.min_access_size = 2,
    .impl.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void gicv3_arm_its_realize(DeviceState *dev, Error **errp)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    int i;

    for (i = 0; i < s->gicv3->num_cpu; i++) {
        if (!(s->gicv3->cpu[i].gicr_typer & GICR_TYPER_PLPIS)) {
            error_setg(errp, "Physical LPI not supported by CPU %d", i);
            return;
        }
    }

    gicv3_its_init_mmio(s, &gicv3_its_control_ops, &gicv3_its_translation_ops);

    address_space_init(&s->gicv3->dma_as, s->gicv3->dma,
                       "gicv3-its-sysmem");

    /* set the ITS default features supported */
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, PHYSICAL, 1);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, ITT_ENTRY_SIZE,
                          ITS_ITT_ENTRY_SIZE - 1);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, IDBITS, ITS_IDBITS);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, DEVBITS, ITS_DEVBITS);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIL, 1);
    s->typer = FIELD_DP64(s->typer, GITS_TYPER, CIDBITS, ITS_CIDBITS);
}

static void gicv3_its_reset(DeviceState *dev)
{
    GICv3ITSState *s = ARM_GICV3_ITS_COMMON(dev);
    GICv3ITSClass *c = ARM_GICV3_ITS_GET_CLASS(s);

    c->parent_reset(dev);

    /* Quiescent bit reset to 1 */
    s->ctlr = FIELD_DP32(s->ctlr, GITS_CTLR, QUIESCENT, 1);

    /*
     * setting GITS_BASER0.Type = 0b001 (Device)
     *         GITS_BASER1.Type = 0b100 (Collection Table)
     *         GITS_BASER<n>.Type,where n = 3 to 7 are 0b00 (Unimplemented)
     *         GITS_BASER<0,1>.Page_Size = 64KB
     * and default translation table entry size to 16 bytes
     */
    s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, TYPE,
                             GITS_BASER_TYPE_DEVICE);
    s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, PAGESIZE,
                             GITS_BASER_PAGESIZE_64K);
    s->baser[0] = FIELD_DP64(s->baser[0], GITS_BASER, ENTRYSIZE,
                             GITS_DTE_SIZE - 1);

    s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, TYPE,
                             GITS_BASER_TYPE_COLLECTION);
    s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, PAGESIZE,
                             GITS_BASER_PAGESIZE_64K);
    s->baser[1] = FIELD_DP64(s->baser[1], GITS_BASER, ENTRYSIZE,
                             GITS_CTE_SIZE - 1);
}

static void gicv3_its_post_load(GICv3ITSState *s)
{
    if (s->ctlr & R_GITS_CTLR_ENABLED_MASK) {
        extract_table_params(s);
        extract_cmdq_params(s);
    }
}

static Property gicv3_its_props[] = {
    DEFINE_PROP_LINK("parent-gicv3", GICv3ITSState, gicv3, "arm-gicv3",
                     GICv3State *),
    DEFINE_PROP_END_OF_LIST(),
};

static void gicv3_its_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    GICv3ITSClass *ic = ARM_GICV3_ITS_CLASS(klass);
    GICv3ITSCommonClass *icc = ARM_GICV3_ITS_COMMON_CLASS(klass);

    dc->realize = gicv3_arm_its_realize;
    device_class_set_props(dc, gicv3_its_props);
    device_class_set_parent_reset(dc, gicv3_its_reset, &ic->parent_reset);
    icc->post_load = gicv3_its_post_load;
}

static const TypeInfo gicv3_its_info = {
    .name = TYPE_ARM_GICV3_ITS,
    .parent = TYPE_ARM_GICV3_ITS_COMMON,
    .instance_size = sizeof(GICv3ITSState),
    .class_init = gicv3_its_class_init,
    .class_size = sizeof(GICv3ITSClass),
};

static void gicv3_its_register_types(void)
{
    type_register_static(&gicv3_its_info);
}

type_init(gicv3_its_register_types)
