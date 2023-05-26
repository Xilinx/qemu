/*
 * QEMU model of the Xilinx PMX_EFUSE_CTRL
 *
 * Copyright (c) 2021 Xilinx Inc.
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "qemu/osdep.h"
#include "hw/nvram/xlnx-pmx-efuse.h"

#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/misc/xlnx-aes.h"

#ifndef XLNX_PMX_EFUSE_CTRL_ERR_DEBUG
#define XLNX_PMX_EFUSE_CTRL_ERR_DEBUG 0
#endif

REG32(WR_LOCK, 0x0)
    FIELD(WR_LOCK, LOCK, 0, 16)
REG32(CFG, 0x4)
    FIELD(CFG, SLVERR_ENABLE, 5, 1)
    FIELD(CFG, MARGIN_RD, 2, 1)
    FIELD(CFG, PGM_EN, 1, 1)
REG32(STATUS, 0x8)
    FIELD(STATUS, UDS_DICE_CRC_PASS, 13, 1)
    FIELD(STATUS, UDS_DICE_CRC_DONE, 12, 1)
    FIELD(STATUS, AES_USER_KEY_1_CRC_PASS, 11, 1)
    FIELD(STATUS, AES_USER_KEY_1_CRC_DONE, 10, 1)
    FIELD(STATUS, AES_USER_KEY_0_CRC_PASS, 9, 1)
    FIELD(STATUS, AES_USER_KEY_0_CRC_DONE, 8, 1)
    FIELD(STATUS, AES_CRC_PASS, 7, 1)
    FIELD(STATUS, AES_CRC_DONE, 6, 1)
    FIELD(STATUS, CACHE_DONE, 5, 1)
    FIELD(STATUS, CACHE_LOAD, 4, 1)
    FIELD(STATUS, EFUSE_2_TBIT, 2, 1)
    FIELD(STATUS, EFUSE_1_TBIT, 1, 1)
    FIELD(STATUS, EFUSE_0_TBIT, 0, 1)
REG32(EFUSE_PGM_ADDR, 0xc)
    FIELD(EFUSE_PGM_ADDR, PAGE, 13, 4)
    FIELD(EFUSE_PGM_ADDR, ROW, 5, 8)
    FIELD(EFUSE_PGM_ADDR, COLUMN, 0, 5)
REG32(EFUSE_RD_ADDR, 0x10)
    FIELD(EFUSE_RD_ADDR, PAGE, 13, 4)
    FIELD(EFUSE_RD_ADDR, ROW, 5, 8)
REG32(EFUSE_RD_DATA, 0x14)
REG32(TPGM, 0x18)
    FIELD(TPGM, VALUE, 0, 16)
REG32(TRD, 0x1c)
    FIELD(TRD, VALUE, 0, 8)
REG32(TSU_H_PS, 0x20)
    FIELD(TSU_H_PS, VALUE, 0, 8)
REG32(TSU_H_PS_CS, 0x24)
    FIELD(TSU_H_PS_CS, VALUE, 0, 8)
REG32(TRDM, 0x28)
    FIELD(TRDM, VALUE, 0, 8)
REG32(TSU_H_CS, 0x2c)
    FIELD(TSU_H_CS, VALUE, 0, 8)
REG32(EFUSE_ISR, 0x30)
    FIELD(EFUSE_ISR, APB_SLVERR, 31, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E2, 18, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E1, 17, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E04S, 16, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E03S, 15, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E02S, 14, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E01S, 13, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E00S, 12, 1)
    FIELD(EFUSE_ISR, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_ISR, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_ISR, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_ISR, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_ISR, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_ISR, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_ISR, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_ISR, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_ISR, RD_ERROR, 3, 1)
    FIELD(EFUSE_ISR, RD_DONE, 2, 1)
    FIELD(EFUSE_ISR, PGM_ERROR, 1, 1)
    FIELD(EFUSE_ISR, PGM_DONE, 0, 1)
REG32(EFUSE_IMR, 0x34)
    FIELD(EFUSE_IMR, APB_SLVERR, 31, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E2, 18, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E1, 17, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E04S, 16, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E03S, 15, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E02S, 14, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E01S, 13, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E00S, 12, 1)
    FIELD(EFUSE_IMR, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_IMR, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_IMR, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_IMR, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_IMR, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_IMR, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_IMR, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_IMR, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_IMR, RD_ERROR, 3, 1)
    FIELD(EFUSE_IMR, RD_DONE, 2, 1)
    FIELD(EFUSE_IMR, PGM_ERROR, 1, 1)
    FIELD(EFUSE_IMR, PGM_DONE, 0, 1)
REG32(EFUSE_IER, 0x38)
    FIELD(EFUSE_IER, APB_SLVERR, 31, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E2, 18, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E1, 17, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E04S, 16, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E03S, 15, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E02S, 14, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E01S, 13, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E00S, 12, 1)
    FIELD(EFUSE_IER, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_IER, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_IER, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_IER, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_IER, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_IER, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_IER, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_IER, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_IER, RD_ERROR, 3, 1)
    FIELD(EFUSE_IER, RD_DONE, 2, 1)
    FIELD(EFUSE_IER, PGM_ERROR, 1, 1)
    FIELD(EFUSE_IER, PGM_DONE, 0, 1)
REG32(EFUSE_IDR, 0x3c)
    FIELD(EFUSE_IDR, APB_SLVERR, 31, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E2, 18, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E1, 17, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E04S, 16, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E03S, 15, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E02S, 14, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E01S, 13, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E00S, 12, 1)
    FIELD(EFUSE_IDR, CACHE_PARITY_E0R, 11, 1)
    FIELD(EFUSE_IDR, CACHE_APB_SLVERR, 10, 1)
    FIELD(EFUSE_IDR, CACHE_REQ_ERROR, 9, 1)
    FIELD(EFUSE_IDR, MAIN_REQ_ERROR, 8, 1)
    FIELD(EFUSE_IDR, READ_ON_CACHE_LD, 7, 1)
    FIELD(EFUSE_IDR, CACHE_FSM_ERROR, 6, 1)
    FIELD(EFUSE_IDR, MAIN_FSM_ERROR, 5, 1)
    FIELD(EFUSE_IDR, CACHE_ERROR, 4, 1)
    FIELD(EFUSE_IDR, RD_ERROR, 3, 1)
    FIELD(EFUSE_IDR, RD_DONE, 2, 1)
    FIELD(EFUSE_IDR, PGM_ERROR, 1, 1)
    FIELD(EFUSE_IDR, PGM_DONE, 0, 1)
REG32(EFUSE_CACHE_LOAD, 0x40)
    FIELD(EFUSE_CACHE_LOAD, LOAD, 0, 1)
REG32(EFUSE_PGM_LOCK, 0x44)
    FIELD(EFUSE_PGM_LOCK, REVOCATION_ID_LOCK, 0, 1)
REG32(EFUSE_AES_CRC, 0x48)
REG32(EFUSE_AES_USR_KEY0_CRC, 0x4c)
REG32(EFUSE_AES_USR_KEY1_CRC, 0x50)
REG32(ANLG_OSC_SW_1LP, 0x60)
    FIELD(ANLG_OSC_SW_1LP, SELECT, 0, 1)
REG32(UDS_DICE_CRC, 0x70)

#define PMX_EFUSE_CTRL_R_MAX (R_UDS_DICE_CRC + 1)

#define EFUSE_ANCHOR_3_COL          (27)
#define EFUSE_ANCHOR_1_COL          (1)

#define R_WR_LOCK_UNLOCK_PASSCODE   (0xDF0D)

/*
 * The eFuse storage is organized as 2-dimensional <row, byte[3:0]>
 * matrix.
 *
 * With few exceptions, a logical data entity, e.g., an AES-key or
 * a 32-bit word readable through a 32-bit aligned cache address, is
 * organized along multiple rows of the same byte lane.
 *
 * Here, such a region of contiguous 1 or more rows of the same
 * byte lane is referred to as a tile.
 *
 * The coordinate of a tile is a pair <row, byte_lane> number, where
 * 'row' is the lowest row id (0-based), and 'byte_lane' is 1..4
 * (a value of '5' is used to indicate a strip tile, i.e., 1 row
 *  with all 4 bytes from the same row).
 *
 * Efuse access-control is byte-wise, and is implemented using a
 * 2-level table lookup:
 *
 * a) Level 1 uses efuse array's byte offset (the value specified
 *    in controller's EFUSE_PGM_ADDR or EFUSE_RD_ADDR register, and
 *    not the cache byte offset) to obtain the ID of its respective
 *    access-control checker (acc) function.
 *
 *    There are 2 level-1 tables:
 *    i)  One for read-only access, i.e., any bits within the byte cannot
 *        be changed via EFUSE_PGM_ADDR register.
 *
 *    ii) One for write-only access, i.e., any bits within the byte cannot
 *        be read via EFUSE_RD_ADDR register or its corresponding cache.
 *
 *    If level-1 lookup returns an ID of 0, the requested access is
 *    always granted.
 *
 * b) Level 2 uses acc ID to find the entry address of the acc function,
 *    which returns True if access is denied.
 */
typedef const struct XlnxPmxEfuseTile {
    uint16_t row:12;       /* 0-based index into fuse[] u32 array */
    uint16_t byte_lane:4;  /* 1-based byte-lane of u8 (5:u32) lsb */
} XlnxPmxEfuseTile;

typedef bool (*efuse_acv_t)(XlnxPmxEFuseCtrl *s);

#include "xlnx-pmx-efuse-tile.c.inc"

/* Bits readable as 32-bit words through the pmx-efuse-cache */
static XlnxPmxEfuseTile pmx_efuse_u32[] = {
    EFUSE_U32_TILES,
};

/*
 * Write-only u8 arrays.
 *
 * pmx-efuse-ctrl can, and only can, report their calculated CRC.
 */
static XlnxPmxEfuseTile pmx_efuse_u8_aes_key[] = {
    EFUSE_U8_TILES_AES_KEY
};

static XlnxPmxEfuseTile pmx_efuse_u8_user0_key[] = {
    EFUSE_U8_TILES_USER0_KEY
};

static XlnxPmxEfuseTile pmx_efuse_u8_user1_key[] = {
    EFUSE_U8_TILES_USER1_KEY
};

static XlnxPmxEfuseTile pmx_efuse_u8_uds[] = {
    EFUSE_U8_TILES_UDS
};

/* A table to determine if a given eFuse array's byte is write-only. */
static const uint8_t pmx_efuse_ac_wr_only[] = {
    EFUSE_ACL1_WR_ONLY
};

/* A table to determine if a given eFuse array's byte is read-only */
static const uint8_t pmx_efuse_ac_rd_only[] = {
    EFUSE_ACL1_RD_ONLY
};

/* A table to dispatch access control checker */
static efuse_acv_t const pmx_efuse_ac_verifier[] = {
    EFUSE_ACL2_FUNCS
};

static unsigned pmx_efuse_bits(XlnxEFuse *efuse)
{
    return efuse->efuse_nr * efuse->efuse_size;
}

static bool pmx_efuse_ac_locked(XlnxPmxEFuseCtrl *s, size_t baddr,
                                const uint8_t *ac_table, size_t ac_limit)
{
    efuse_acv_t verifier = NULL;
    uint8_t ac;

    /* Access request is granted if it is not under given access control */
    if (baddr >= ac_limit) {
        return false;
    }

    ac = ac_table[baddr];
    assert(ac < ARRAY_SIZE(pmx_efuse_ac_verifier));

    switch (ac) {
    case EFUSE_AC_NEVER:
        return false;
    case EFUSE_AC_ALWAYS:
        return true;
    default:
        verifier = pmx_efuse_ac_verifier[ac];
        return verifier && verifier(s);
    }
}

static uint8_t pmx_efuse_ac_rd_mask(XlnxPmxEFuseCtrl *s,
                                    size_t row, unsigned byte_idx)
{
    bool wr_only;

    wr_only = pmx_efuse_ac_locked(s, (row * 4 + byte_idx),
                                  pmx_efuse_ac_wr_only,
                                  sizeof(pmx_efuse_ac_wr_only));
    return wr_only ? 0 : 255;
}

static bool pmx_efuse_ac_writable(XlnxPmxEFuseCtrl *s, unsigned bit)
{
    bool rd_only;

    /* Global write-disable */
    if (!ARRAY_FIELD_EX32(s->regs, CFG, PGM_EN)) {
        return false;
    }

    /* Fine-grain write-access control */
    rd_only = pmx_efuse_ac_locked(s, (bit / 8),
                                  pmx_efuse_ac_rd_only,
                                  sizeof(pmx_efuse_ac_rd_only));
    return !rd_only;
}

static uint32_t pmx_efuse_tile_read_mask(XlnxPmxEfuseTile *tile,
                                         XlnxPmxEFuseCtrl *s)
{
    uint32_t mask = 0;
    unsigned bn, rn;

    bn = tile ? tile->byte_lane : 0;

    switch (bn) {
    case 1 ... 4:
        /* 4 rows in same byte lane */
        bn--;
        for (rn = 4; rn-- > 0; ) {
            mask <<= 8;
            mask |= pmx_efuse_ac_rd_mask(s, (tile->row + rn), bn);
        }
        return mask;
    case 5:
        /* all 4 byte lanes in same row */
        for (bn = 4; bn-- > 0; ) {
            mask <<= 8;
            mask |= pmx_efuse_ac_rd_mask(s, tile->row, bn);
        }
        return mask;
    default:
        return 0;
    }
}

static uint32_t pmx_efuse_tile_get_u32(XlnxPmxEfuseTile *tile, XlnxEFuse *efuse)
{
    unsigned lsb_lane = 8 * (tile->byte_lane - 1);
    unsigned rn, r0 = tile->row;
    uint32_t u32;

    switch (tile->byte_lane) {
    case 0:
        return 0;
    case 1 ... 4:
        break;
    default:
        return efuse->fuse32[r0];
    }

    /* Retrieve the 4x8bit tile */
    for (u32 = 0, rn = 4; rn-- > 0;) {
        uint8_t u8 = efuse->fuse32[r0 + rn] >> lsb_lane;

        u32 = (u32 << 8) | u8;
    }

    return u32;
}

static uint32_t pmx_efuse_tile_get_u8(XlnxPmxEfuseTile *tile, XlnxEFuse *efuse)
{
    unsigned lsb_lane = 8 * (tile->byte_lane - 1);
    uint32_t row_word = efuse->fuse32[tile->row];

    return 255 & (row_word >> lsb_lane);
}

static void pmx_efuse_tile_get_be(XlnxPmxEfuseTile *tile, size_t tile_cnt,
                                  void *d, size_t len, XlnxEFuse *efuse)
{
    uint8_t *u8 = d;
    unsigned i, bcnt = MIN(tile_cnt, len);

    /* Truncate on least-significant part of efuse source */
    for (i = 0; i < bcnt; i++) {
        u8[i] = pmx_efuse_tile_get_u8(&tile[tile_cnt - i - 1], efuse);
    }

    /* 0-pad on least-significant excess */
    if (bcnt < len) {
        memset(u8 + bcnt, 0, (len - bcnt));
    }
}

static void pmx_efuse_tile_get_le(XlnxPmxEfuseTile *tile, size_t tile_cnt,
                                  void *d, size_t len, XlnxEFuse *efuse)
{
    uint8_t *u8 = d;
    unsigned i, bcnt = MIN(tile_cnt, len);

    /* Truncate on most-significant part of efuse source */
    for (i = 0; i < bcnt; i++) {
        u8[i] = pmx_efuse_tile_get_u8(&tile[i], efuse);
    }

    /* 0-pad on most-significant excess */
    if (bcnt < len) {
        memset(u8 + bcnt, 0, (len - bcnt));
    }
}

static uint32_t pmx_efuse_get_u32(DeviceState *dev, uint32_t bit, bool *denied)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(dev);
    XlnxPmxEfuseTile *tile = NULL;
    unsigned slot = bit / 32;
    uint32_t mask;
    bool denied_local;

    if (!denied) {
        denied = &denied_local;
    }

    if (slot > ARRAY_SIZE(pmx_efuse_u32)) {
        goto denied;
    }

    tile = &pmx_efuse_u32[slot];
    mask = pmx_efuse_tile_read_mask(tile, s);
    if (!mask) {
        goto denied;
    }

    *denied = false;
    return mask & pmx_efuse_tile_get_u32(tile, s->efuse);

 denied:
    /* Silient if passing status back to caller */
    if (denied == &denied_local) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: logical[0x%02x] is unreadable.", path, (4 * slot));
    }

    *denied = true;
    return 0;
}

static bool pmx_efuse_in_dme_mode(XlnxEFuse *efuse)
{
    enum {
        DME_FIPS_CACHE_ADDR = 0x234,
    };

    XlnxPmxEfuseTile *tile = &pmx_efuse_u32[DME_FIPS_CACHE_ADDR / 4];
    unsigned u32 = pmx_efuse_tile_get_u8(tile, efuse);

    return !!(u32 & 0xF);
}

static void pmx_efuse_ac_dme_sync(XlnxPmxEFuseCtrl *s)
{
    s->ac_dme = pmx_efuse_in_dme_mode(s->efuse);
}

static void efuse_imr_update_irq(XlnxPmxEFuseCtrl *s)
{
    bool pending = s->regs[R_EFUSE_ISR] & ~s->regs[R_EFUSE_IMR];
    qemu_set_irq(s->irq_efuse_imr, pending);
}

static void efuse_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    efuse_imr_update_irq(s);
}

static uint64_t efuse_ier_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_EFUSE_IMR] &= ~val;
    efuse_imr_update_irq(s);
    return 0;
}

static uint64_t efuse_idr_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_EFUSE_IMR] |= val;
    efuse_imr_update_irq(s);
    return 0;
}

static void efuse_extract_aes_key_be(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_be(pmx_efuse_u8_aes_key,
                          ARRAY_SIZE(pmx_efuse_u8_aes_key),
                          d, (256 / 8), s->efuse);
}

static void efuse_extract_user_key_0_be(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_be(pmx_efuse_u8_user0_key,
                          ARRAY_SIZE(pmx_efuse_u8_user0_key),
                          d, (256 / 8), s->efuse);
}

static void efuse_extract_user_key_1_be(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_be(pmx_efuse_u8_user1_key,
                          ARRAY_SIZE(pmx_efuse_u8_user1_key),
                          d, (256 / 8), s->efuse);
}

static void efuse_extract_aes_key(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_le(pmx_efuse_u8_aes_key,
                          ARRAY_SIZE(pmx_efuse_u8_aes_key),
                          d, (256 / 8), s->efuse);
}

static void efuse_extract_user_key_0(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_le(pmx_efuse_u8_user0_key,
                          ARRAY_SIZE(pmx_efuse_u8_user0_key),
                          d, (256 / 8), s->efuse);
}

static void efuse_extract_user_key_1(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_le(pmx_efuse_u8_user1_key,
                          ARRAY_SIZE(pmx_efuse_u8_user1_key),
                          d, (256 / 8), s->efuse);
}

static void efuse_extract_dice_uds(XlnxPmxEFuseCtrl *s, uint32_t *d)
{
    pmx_efuse_tile_get_le(pmx_efuse_u8_uds,
                          ARRAY_SIZE(pmx_efuse_u8_uds),
                          d, (384 / 8), s->efuse);
}

static bool bit_in_tbit_range(uint32_t bit)
{
    return FIELD_EX32(bit, EFUSE_PGM_ADDR, ROW) == 0 &&
           FIELD_EX32(bit, EFUSE_PGM_ADDR, COLUMN) >= 28;
}

static void efuse_status_tbits_sync(XlnxPmxEFuseCtrl *s)
{
    uint32_t check = xlnx_efuse_tbits_check(s->efuse);
    uint32_t val = s->regs[R_STATUS];

    val = FIELD_DP32(val, STATUS, EFUSE_0_TBIT, !!(check & (1 << 0)));
    val = FIELD_DP32(val, STATUS, EFUSE_1_TBIT, !!(check & (1 << 1)));
    val = FIELD_DP32(val, STATUS, EFUSE_2_TBIT, !!(check & (1 << 2)));

    s->regs[R_STATUS] = val;
}

static void efuse_pgm_addr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    unsigned bit = val64;
    bool ok = false;

    /* Always zero out PGM_ADDR because it is write-only */
    s->regs[R_EFUSE_PGM_ADDR] = 0;

    if (bit >= pmx_efuse_bits(s->efuse)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Denied setting out-of-range efuse<%u, %u, %u>",
                      path,
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, PAGE),
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, ROW),
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, COLUMN));
        goto done;
    }

    /*
     * Indicate error if write-access to the bit is prohibited.
     *
     * Keep it simple by not modeling program timing.
     *
     * Note: model must NEVER clear the PGM_ERROR bit; it is
     *       up to guest to do so (or by reset).
     */
    if (!pmx_efuse_ac_writable(s, bit)) {
        g_autofree char *path = object_get_canonical_path(OBJECT(s));

        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Denied setting read-only efuse<%u, %u, %u>",
                      path,
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, PAGE),
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, ROW),
                      FIELD_EX32(bit, EFUSE_PGM_ADDR, COLUMN));
        goto done;
    }

    if (xlnx_efuse_set_bit(s->efuse, bit)) {
        ok = true;
        pmx_efuse_ac_dme_sync(s);
        if (bit_in_tbit_range(bit)) {
            efuse_status_tbits_sync(s);
        }
    }

 done:
    if (!ok) {
        ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, PGM_ERROR, 1);
    }

    ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, PGM_DONE, 1);
    efuse_imr_update_irq(s);
}

static void efuse_rd_addr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    uint32_t bit = val64;
    uint32_t page;
    uint32_t data;

    /* Fold unmodelled B pages onto A */
    page = FIELD_EX32(bit, EFUSE_RD_ADDR, PAGE);
    bit = FIELD_DP32(bit, EFUSE_RD_ADDR, PAGE, (page & 3));

    if (bit >= pmx_efuse_bits(s->efuse)) {
        data = 0;
    } else {
        /* Apply mask to zeroize write-only bits */
        XlnxPmxEfuseTile tile = { .row = bit / 32, .byte_lane = 5 };
        uint32_t mask;

        mask = pmx_efuse_tile_read_mask(&tile, s);
        data = xlnx_efuse_get_row(s->efuse, bit) & mask;
    }

    s->regs[R_EFUSE_RD_DATA] = data;

    ARRAY_FIELD_DP32(s->regs, EFUSE_ISR, RD_DONE, 1);
    efuse_imr_update_irq(s);
}

static void efuse_data_sync(XlnxPmxEFuseCtrl *s)
{
    union {
        uint8_t u8[256 / 8];
        uint32_t u32[256 / 32];
    } key;

    pmx_efuse_ac_dme_sync(s);
    efuse_status_tbits_sync(s);

    efuse_extract_aes_key_be(s, key.u32);
    zynqmp_aes_key_update(s->aes_key_sink, key.u8, sizeof(key.u8));

    efuse_extract_user_key_0_be(s, key.u32);
    zynqmp_aes_key_update(s->usr_key0_sink, key.u8, sizeof(key.u8));

    efuse_extract_user_key_1_be(s, key.u32);
    zynqmp_aes_key_update(s->usr_key1_sink, key.u8, sizeof(key.u8));
}

static uint64_t efuse_cache_load_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);

    if (val64 & R_EFUSE_CACHE_LOAD_LOAD_MASK) {
        efuse_data_sync(s);

        ARRAY_FIELD_DP32(s->regs, STATUS, CACHE_DONE, 1);
        efuse_imr_update_irq(s);
    }

    return 0;
}

static uint64_t efuse_pgm_lock_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);

    /* Ignore all other bits */
    val64 = FIELD_EX32(val64, EFUSE_PGM_LOCK, REVOCATION_ID_LOCK);

    /* Once the bit is written 1, only reset will clear it to 0 */
    val64 |= ARRAY_FIELD_EX32(s->regs, EFUSE_PGM_LOCK, REVOCATION_ID_LOCK);

    return val64;
}

static void efuse_crc_compare(XlnxPmxEFuseCtrl *s,
                              uint32_t crc_a, uint32_t crc_b,
                              uint32_t done_mask, uint32_t pass_mask)
{
    uint32_t *reg = &s->regs[R_STATUS];

    *reg |= done_mask;
    if (crc_a == crc_b) {
        *reg |= pass_mask;
    } else {
        *reg &= ~pass_mask;
    }
}

static void efuse_key_crc_chk(XlnxPmxEFuseCtrl *s, uint32_t crc_a,
                              uint32_t done_mask, uint32_t pass_mask,
                              void (*get_key)(XlnxPmxEFuseCtrl *, uint32_t *))
{
    uint32_t crc_b;

    if (get_key) {
        uint32_t aes_key[256 / 32];

        get_key(s, aes_key);
        crc_b = xlnx_aes_k256_crc(aes_key, 0);
    } else {
        /* Force unequal compare on disabled key */
        crc_b = crc_a ^ 1;
    }

    efuse_crc_compare(s, crc_a, crc_b, done_mask, pass_mask);
}

static void efuse_aes_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    bool dis = pmx_efuse_ac_5a9(s) || pmx_efuse_ac_5aa(s);

    efuse_key_crc_chk(s, val64,
                      R_STATUS_AES_CRC_DONE_MASK,
                      R_STATUS_AES_CRC_PASS_MASK,
                      (dis ? NULL : efuse_extract_aes_key));
}

static void efuse_aes_u0_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    bool dis = pmx_efuse_ac_5ac(s);

    efuse_key_crc_chk(s, val64,
                      R_STATUS_AES_USER_KEY_0_CRC_DONE_MASK,
                      R_STATUS_AES_USER_KEY_0_CRC_PASS_MASK,
                      (dis ? NULL : efuse_extract_user_key_0));
}

static void efuse_aes_u1_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    bool dis = pmx_efuse_ac_5ae(s);

    efuse_key_crc_chk(s, val64,
                      R_STATUS_AES_USER_KEY_1_CRC_DONE_MASK,
                      R_STATUS_AES_USER_KEY_1_CRC_PASS_MASK,
                      (dis ? NULL : efuse_extract_user_key_1));
}

static void efuse_uds_dice_crc_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(reg->opaque);
    uint32_t crc_b, dice_uds[384 / 32];

    efuse_extract_dice_uds(s, dice_uds);
    crc_b = xlnx_calc_crc(dice_uds, ARRAY_SIZE(dice_uds));

    efuse_crc_compare(s, val64, crc_b,
                      R_STATUS_UDS_DICE_CRC_DONE_MASK,
                      R_STATUS_UDS_DICE_CRC_PASS_MASK);
}

static uint64_t efuse_wr_lock_prew(RegisterInfo *reg, uint64_t val)
{
    return val != R_WR_LOCK_UNLOCK_PASSCODE;
}

QEMU_BUILD_BUG_ON(PMX_EFUSE_CTRL_R_MAX
                  != ARRAY_SIZE(((XlnxPmxEFuseCtrl *)0)->regs_info));

static const RegisterAccessInfo pmx_efuse_ctrl_regs_info[] = {
    {   .name = "WR_LOCK",  .addr = A_WR_LOCK,
        .reset = 0x1,
        .pre_write = efuse_wr_lock_prew,
    },{ .name = "CFG",  .addr = A_CFG,
        .rsvd = 0x9,
    },{ .name = "STATUS",  .addr = A_STATUS,
        .rsvd = 0x8,
        .ro = 0x3fff,
    },{ .name = "EFUSE_PGM_ADDR",  .addr = A_EFUSE_PGM_ADDR,
        .post_write = efuse_pgm_addr_postw,
    },{ .name = "EFUSE_RD_ADDR",  .addr = A_EFUSE_RD_ADDR,
        .rsvd = 0x1f,
        .post_write = efuse_rd_addr_postw,
    },{ .name = "EFUSE_RD_DATA",  .addr = A_EFUSE_RD_DATA,
        .ro = 0xffffffff,
    },{ .name = "TPGM",  .addr = A_TPGM,
    },{ .name = "TRD",  .addr = A_TRD,
        .reset = 0x19,
    },{ .name = "TSU_H_PS",  .addr = A_TSU_H_PS,
        .reset = 0xff,
    },{ .name = "TSU_H_PS_CS",  .addr = A_TSU_H_PS_CS,
        .reset = 0x11,
    },{ .name = "TRDM",  .addr = A_TRDM,
        .reset = 0x3a,
    },{ .name = "TSU_H_CS",  .addr = A_TSU_H_CS,
        .reset = 0x16,
    },{ .name = "EFUSE_ISR",  .addr = A_EFUSE_ISR,
        .rsvd = 0x7ff80000,
        .w1c = 0x8007ffff,
        .post_write = efuse_isr_postw,
    },{ .name = "EFUSE_IMR",  .addr = A_EFUSE_IMR,
        .reset = 0x8007ffff,
        .rsvd = 0x7ff80000,
        .ro = 0xffffffff,
    },{ .name = "EFUSE_IER",  .addr = A_EFUSE_IER,
        .rsvd = 0x7ff80000,
        .pre_write = efuse_ier_prew,
    },{ .name = "EFUSE_IDR",  .addr = A_EFUSE_IDR,
        .rsvd = 0x7ff80000,
        .pre_write = efuse_idr_prew,
    },{ .name = "EFUSE_CACHE_LOAD",  .addr = A_EFUSE_CACHE_LOAD,
        .pre_write = efuse_cache_load_prew,
    },{ .name = "EFUSE_PGM_LOCK",  .addr = A_EFUSE_PGM_LOCK,
        .pre_write = efuse_pgm_lock_prew,
    },{ .name = "EFUSE_AES_CRC",  .addr = A_EFUSE_AES_CRC,
        .post_write = efuse_aes_crc_postw,
    },{ .name = "EFUSE_AES_USR_KEY0_CRC",  .addr = A_EFUSE_AES_USR_KEY0_CRC,
        .post_write = efuse_aes_u0_crc_postw,
    },{ .name = "EFUSE_AES_USR_KEY1_CRC",  .addr = A_EFUSE_AES_USR_KEY1_CRC,
        .post_write = efuse_aes_u1_crc_postw,
    },{ .name = "ANLG_OSC_SW_1LP",  .addr = A_ANLG_OSC_SW_1LP,
    },{ .name = "UDS_DICE_CRC",  .addr = A_UDS_DICE_CRC,
        .post_write = efuse_uds_dice_crc_postw,
    },
};

static void efuse_ctrl_register_reset(RegisterInfo *reg)
{
    if (!reg->data || !reg->access) {
        return;
    }

    /* Reset must not trigger some registers' writers */
    switch (reg->access->addr) {
    case A_EFUSE_PGM_ADDR:
    case A_EFUSE_RD_ADDR:
    case A_EFUSE_AES_CRC:
    case A_EFUSE_AES_USR_KEY0_CRC:
    case A_EFUSE_AES_USR_KEY1_CRC:
    case A_UDS_DICE_CRC:
        *(uint32_t *)reg->data = reg->access->reset;
        return;
    }

    register_reset(reg);
}

static void efuse_anchor_bits_check(XlnxPmxEFuseCtrl *s)
{
    unsigned page;

    if (!s->efuse || !s->efuse->init_tbits) {
        return;
    }

    for (page = 0; page < s->efuse->efuse_nr; page++) {
        unsigned r0, bit;

        r0 = FIELD_DP32(0, EFUSE_PGM_ADDR, PAGE, page);

        bit = FIELD_DP32(r0, EFUSE_PGM_ADDR, COLUMN, EFUSE_ANCHOR_3_COL);
        xlnx_efuse_set_bit(s->efuse, bit);

        bit = FIELD_DP32(r0, EFUSE_PGM_ADDR, COLUMN, EFUSE_ANCHOR_1_COL);
        xlnx_efuse_set_bit(s->efuse, bit);
    }
}

static void pmx_efuse_ctrl_reset_enter(Object *obj, ResetType type)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        efuse_ctrl_register_reset(&s->regs_info[i]);
    }

    efuse_anchor_bits_check(s);
    efuse_data_sync(s);
}

static void pmx_efuse_ctrl_reset_hold(Object *obj)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(obj);

    efuse_imr_update_irq(s);
}

static const MemoryRegionOps pmx_efuse_ctrl_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static bool pmx_efuse_get_aes_dis(Object *efuse, Error **errp)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(efuse->parent);

    return pmx_efuse_ac_588(s);
}

static XlnxEFusePufData *pmx_efuse_get_puf(DeviceState *dev,
                                           uint16_t pufsyn_max)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(dev);
    unsigned pd_r0 = 0x300 / 4;
    unsigned pd_nr = 128 / 4;
    uint16_t pd_max = 127 * 4;
    XlnxEFusePufData *pd;

    if (pd_max > pufsyn_max && pufsyn_max) {
        pd_max = pufsyn_max;
    }

    pd = g_malloc0(offsetof(XlnxEFusePufData, pufsyn) + pd_max);
    pd->puf_dis = pmx_efuse_ac_5ca(s);
    pd->pufsyn_len = pd_max;
    pmx_efuse_tile_get_le(&pmx_efuse_u32[pd_r0], pd_nr,
                          pd->pufsyn, pd_max, s->efuse);

    return pd;
}

static bool pmx_efuse_get_sysmon(DeviceState *dev,
                                 XlnxEFuseSysmonData *data)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(dev);
    XlnxPmxEfuseTile tile_lo = { .row = 32, .byte_lane = 3 };
    XlnxPmxEfuseTile tile_hi = { .row = 36, .byte_lane = 3 };
    unsigned gd_en_bit = 23 * 32 + 29;

    assert(data);
    memset(data, 0, sizeof(*data));

    /* Fetch data with access-control bypassed */
    data->rdata_low = pmx_efuse_tile_get_u32(&tile_lo, s->efuse);
    data->rdata_high = pmx_efuse_tile_get_u32(&tile_hi, s->efuse);
    data->glitch_monitor_en = xlnx_efuse_get_bit(s->efuse, gd_en_bit);

    return true;
}

static void pmx_efuse_ctrl_realize(DeviceState *dev, Error **errp)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(dev);
    g_autofree char *prefix = object_get_canonical_path(OBJECT(dev));

    if (!s->efuse) {
        error_setg(errp, "%s: no XLNX-EFUSE provided", prefix);
        return;
    }

    if (!s->aes_key_sink) {
        warn_report("%s: eFuse AES key sink not connected", prefix);
    }

    if (!s->usr_key0_sink) {
        warn_report("%s: eFuse USR_KEY0 key sink not connected", prefix);
    }

    if (!s->usr_key1_sink) {
        warn_report("%s: eFuse USR_KEY1 key sink not connected", prefix);
    }

    /* Bind method(s) */
    s->efuse->dev = dev;
    s->efuse->get_u32 = pmx_efuse_get_u32;
    s->efuse->get_puf = pmx_efuse_get_puf;
    s->efuse->get_sysmon = pmx_efuse_get_sysmon;

    /*
     * 'get'-only properties on 's->efuse' to expose discrete
     * fuse values to other components.
     */
    object_property_add_bool(OBJECT(s->efuse), "aes-disabled",
                             pmx_efuse_get_aes_dis, NULL);
}

static void pmx_efuse_ctrl_init(Object *obj)
{
    XlnxPmxEFuseCtrl *s = XLNX_PMX_EFUSE_CTRL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    reg_array =
        register_init_block32(DEVICE(obj), pmx_efuse_ctrl_regs_info,
                              ARRAY_SIZE(pmx_efuse_ctrl_regs_info),
                              s->regs_info, s->regs,
                              &pmx_efuse_ctrl_ops,
                              XLNX_PMX_EFUSE_CTRL_ERR_DEBUG,
                              PMX_EFUSE_CTRL_R_MAX * 4);

    sysbus_init_mmio(sbd, &reg_array->mem);
    sysbus_init_irq(sbd, &s->irq_efuse_imr);
}

static const VMStateDescription vmstate_pmx_efuse_ctrl = {
    .name = TYPE_XLNX_PMX_EFUSE_CTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxPmxEFuseCtrl, PMX_EFUSE_CTRL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property efuse_ctrl_props[] = {
    DEFINE_PROP_BOOL("dna-lock",
                     XlnxPmxEFuseCtrl, ac_dna, false),
    DEFINE_PROP_BOOL("factory-lock",
                     XlnxPmxEFuseCtrl, ac_factory, true),
    DEFINE_PROP_BOOL("rfsoc-lock",
                     XlnxPmxEFuseCtrl, ac_rfsoc, false),
    DEFINE_PROP_BOOL("row0-lock",
                     XlnxPmxEFuseCtrl, ac_row0, false),
    DEFINE_PROP_LINK("efuse",
                     XlnxPmxEFuseCtrl, efuse,
                     TYPE_XLNX_EFUSE, XlnxEFuse *),

    DEFINE_PROP_LINK("zynqmp-aes-key-sink-efuses",
                     XlnxPmxEFuseCtrl, aes_key_sink,
                     TYPE_ZYNQMP_AES_KEY_SINK, ZynqMPAESKeySink *),
    DEFINE_PROP_LINK("zynqmp-aes-key-sink-efuses-user0",
                     XlnxPmxEFuseCtrl, usr_key0_sink,
                     TYPE_ZYNQMP_AES_KEY_SINK, ZynqMPAESKeySink *),
    DEFINE_PROP_LINK("zynqmp-aes-key-sink-efuses-user1",
                     XlnxPmxEFuseCtrl, usr_key1_sink,
                     TYPE_ZYNQMP_AES_KEY_SINK, ZynqMPAESKeySink *),

    DEFINE_PROP_END_OF_LIST(),
};

static void pmx_efuse_ctrl_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pmx_efuse_ctrl_realize;
    dc->vmsd = &vmstate_pmx_efuse_ctrl;
    rc->phases.enter = pmx_efuse_ctrl_reset_enter;
    rc->phases.hold = pmx_efuse_ctrl_reset_hold;
    device_class_set_props(dc, efuse_ctrl_props);
}

static const TypeInfo pmx_efuse_ctrl_info = {
    .name          = TYPE_XLNX_PMX_EFUSE_CTRL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxPmxEFuseCtrl),
    .class_init    = pmx_efuse_ctrl_class_init,
    .instance_init = pmx_efuse_ctrl_init,
};

static void pmx_efuse_ctrl_register_types(void)
{
    type_register_static(&pmx_efuse_ctrl_info);
}

type_init(pmx_efuse_ctrl_register_types)
