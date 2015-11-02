/*
 * Flash NAND memory emulation.  Based on "16M x 8 Bit NAND Flash
 * Memory" datasheet for the KM29U128AT / K9F2808U0A chips from
 * Samsung Electronic.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * Support for additional features based on "MT29F2G16ABCWP 2Gx16"
 * datasheet from Micron Technology and "NAND02G-B2C" datasheet
 * from ST Microelectronics.
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

# include "hw/hw.h"
# include "hw/block/flash.h"
#include "sysemu/block-backend.h"
#include "hw/qdev.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
#include "qemu/log.h"

#ifndef NAND_ERR_DEBUG
#define NAND_ERR_DEBUG 1
#endif

#define DB_PRINT_L(...) do { \
    if (NAND_ERR_DEBUG) { \
        qemu_log_mask(DEV_LOG_NAND, ": %s: ", __func__); \
        qemu_log_mask(DEV_LOG_NAND, ## __VA_ARGS__); \
    } \
} while (0);

# define NAND_CMD_READ0		0x00
# define NAND_CMD_READ1		0x01
# define NAND_CMD_READ2		0x50
# define NAND_CMD_LPREAD2	0x30
# define NAND_CMD_NOSERIALREAD2	0x35
# define NAND_CMD_RANDOMREAD1	0x05
# define NAND_CMD_RANDOMREAD2	0xe0
# define NAND_CMD_READID	0x90
# define NAND_CMD_RESET		0xff
# define NAND_CMD_PAGEPROGRAM1	0x80
# define NAND_CMD_PAGEPROGRAM2	0x10
# define NAND_CMD_CACHEPROGRAM2	0x15
# define NAND_CMD_BLOCKERASE1	0x60
# define NAND_CMD_BLOCKERASE2	0xd0
# define NAND_CMD_READSTATUS	0x70
# define NAND_CMD_READSTATUS_ENHANCED	0x78
# define NAND_CMD_COPYBACKPRG1	0x85
# define NAND_CMD_READ_PARAMETER_PAGE 0xec

#define NAND_CMD_GET_FEATURES 0xee
#define NAND_CMD_SET_FEATURES 0xef

# define NAND_IOSTATUS_ERROR	(1 << 0)
# define NAND_IOSTATUS_PLANE0	(1 << 1)
# define NAND_IOSTATUS_PLANE1	(1 << 2)
# define NAND_IOSTATUS_PLANE2	(1 << 3)
# define NAND_IOSTATUS_PLANE3	(1 << 4)
# define NAND_IOSTATUS_AREADY   (1 << 5)
# define NAND_IOSTATUS_READY    (1 << 6)
# define NAND_IOSTATUS_UNPROTCT	(1 << 7)

# define MAX_PAGE		0x10000
# define MAX_OOB		0x1000
# define MAX_PARM_PAGE_SIZE     256
# define MAX_EXT_PARM_PAGE_SIZE 48

# define NUM_PARAMETER_PAGES_OFFSET    14
typedef struct NANDFlashState NANDFlashState;
struct NANDFlashState {
    DeviceState parent_obj;

    uint8_t manf_id, chip_id;
    uint8_t buswidth; /* in BYTES */
    uint64_t size;
    uint64_t pages;
    int page_shift, erase_shift, addr_shift, oob_size;
    uint8_t *storage;
    BlockBackend *blk;
    int mem_oob;

    uint8_t cle, ale, ce, wp, gnd;

    uint8_t io[MAX_PAGE + MAX_OOB + 0x400];
    uint8_t *ioaddr, *ioaddr0;
    int iolen;

    uint8_t reg_data[16];
    int reglen;
    uint8_t *regaddr;

    uint32_t cmd;
    uint64_t addr;
    int addrlen;
    int status;
    int offset;

    uint8_t features[0x100];
    uint32_t ioaddr_vmstate;
};

#define TYPE_NAND "nand"

#define NAND(obj) \
    OBJECT_CHECK(NANDFlashState, (obj), TYPE_NAND)

# define NAND_NO_AUTOINCR	0x00000001
# define NAND_BUSWIDTH_16	0x00000002
# define NAND_NO_PADDING	0x00000004
# define NAND_CACHEPRG		0x00000008
# define NAND_COPYBACK		0x00000010
# define NAND_IS_AND		0x00000020
# define NAND_4PAGE_ARRAY	0x00000040
# define NAND_NO_READRDY	0x00000100
# define NAND_SAMSUNG_LP	(NAND_NO_PADDING | NAND_COPYBACK)

static inline uint64_t nand_page_size(NANDFlashState *s)
{
    return 1ull << s->page_shift;
}

/* Get the current page address */

static inline uint64_t nand_page(NANDFlashState *s)
{
    return s->addr >> s->addr_shift;
}

/* Get the starting address in backing storage for the speficied page */

static inline uint64_t nand_page_start(NANDFlashState *s, uint64_t page)
{
    uint64_t stride = (1 << s->page_shift);

    /* OOB Size is added to stride between pages */
    stride +=  s->oob_size;
    return page * stride;
}

static inline uint64_t nand_oob_size(NANDFlashState *s)
{
    return s->oob_size;
}

static inline uint64_t nand_sector(NANDFlashState *s, uint64_t addr)
{
    return addr >> (BDRV_SECTOR_BITS + s->addr_shift - s->page_shift);
}

static inline uint64_t nand_sector_offset(NANDFlashState *s, uint64_t addr)
{
    /* FIXME: This code assumes that when the page size is smaller than the
     * block sector size, the addr shift is 8. All NAND devices are as such,
     * bus assert this condition.
     */
    assert(s->page_shift >= BDRV_SECTOR_BITS || s->addr_shift == 8);
    return addr & (BDRV_SECTOR_OFFSET_MASK >> s->page_shift) << 8;
}

/* Information based on Linux drivers/mtd/nand/nand_ids.c */
static const struct {
    uint64_t size;
    int width;
    int page_shift;
    int erase_shift;
    uint32_t options;
    uint32_t oob_size;
    uint8_t param_page[MAX_PARM_PAGE_SIZE + MAX_EXT_PARM_PAGE_SIZE];
} nand_flash_ids[0x100] = {
    [0 ... 0xff] = { 0 },

    [0x44] = { 4096, 8, 14, 8, NAND_SAMSUNG_LP, 1216,
        .param_page = {
            0x4F, 0x4E, 0x46, 0x49, 0x7E, 0x00, 0xF8, 0x1D,
            0xFF, 0x0F, 0x0F, 0x00, 0x03, 0x00, 0x03, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x4D, 0x49, 0x43, 0x52, 0x4F, 0x4E, 0x20, 0x20,
/* 40  */   0x20, 0x20, 0x20, 0x20, 0x4D, 0x54, 0x32, 0x39,
            0x46, 0x33, 0x32, 0x47, 0x30, 0x38, 0x41, 0x42,
            0x43, 0x44, 0x42, 0x4A, 0x34, 0x20, 0x20, 0x20,
            0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 80  */   0x00, 0x40, 0x00, 0x00, 0xC0, 0x04, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x18, 0x04, 0x00, 0x00, 0x01, 0x23, 0x01, 0x31,
            0x00, 0x06, 0x04, 0x01, 0x00, 0x00, 0x02, 0x00,
            0xFF, 0x01, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 120 */   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x05, 0x3F, 0x00, 0x00, 0x00, 0x94, 0x02, 0x40,
            0x1F, 0x2D, 0x00, 0xC8, 0x00, 0x3F, 0x7F, 0x02,
            0x28, 0x00, 0x2D, 0x00, 0x28, 0x00, 0x05, 0x07,
            0x2D, 0x00, 0x46, 0x00, 0x00, 0x00, 0x1F, 0xFF,
/* 160 */   0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
            0x00, 0x00, 0x04, 0x10, 0x01, 0x81, 0x04, 0x02,
            0x02, 0x01, 0x1E, 0x90, 0x0A, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 200 */   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 240 */   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x63, 0x8A,
/* 256 */   0xBD, 0x70, 0x45, 0x50, 0x50, 0x53, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x18, 0x0A, 0x64, 0x00, 0x05, 0x03, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        }
    },
    [0x6e] = { 1,	8,	8, 4, 0 },
    [0x64] = { 2,	8,	8, 4, 0 },
    [0x6b] = { 4,	8,	9, 4, 0 },
    [0xe8] = { 1,	8,	8, 4, 0 },
    [0xec] = { 1,	8,	8, 4, 0 },
    [0xea] = { 2,	8,	8, 4, 0 },
    [0xd5] = { 4,	8,	9, 4, 0 },
    [0xe3] = { 4,	8,	9, 4, 0 },
    [0xe5] = { 4,	8,	9, 4, 0 },
    [0xd6] = { 8,	8,	9, 4, 0 },

    [0x39] = { 8,	8,	9, 4, 0 },
    [0xe6] = { 8,	8,	9, 4, 0 },
    [0x49] = { 8,	16,	9, 4, NAND_BUSWIDTH_16 },
    [0x59] = { 8,	16,	9, 4, NAND_BUSWIDTH_16 },

    [0x33] = { 16,	8,	9, 5, 0 },
    [0x73] = { 16,	8,	9, 5, 0 },
    [0x43] = { 16,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x53] = { 16,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x35] = { 32,	8,	9, 5, 0 },
    [0x75] = { 32,	8,	9, 5, 0 },
    [0x45] = { 32,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x55] = { 32,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x36] = { 64,	8,	9, 5, 0 },
    [0x76] = { 64,	8,	9, 5, 0 },
    [0x46] = { 64,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x56] = { 64,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x78] = { 128,	8,	9, 5, 0 },
    [0x39] = { 128,	8,	9, 5, 0 },
    [0x79] = { 128,	8,	9, 5, 0 },
    [0x72] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x49] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x74] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },
    [0x59] = { 128,	16,	9, 5, NAND_BUSWIDTH_16 },

    [0x71] = { 256,	8,	9, 5, 0 },

    /*
     * These are the new chips with large page size. The pagesize and the
     * erasesize is determined from the extended id bytes
     */
# define LP_OPTIONS	(NAND_SAMSUNG_LP | NAND_NO_READRDY | NAND_NO_AUTOINCR)
# define LP_OPTIONS16	(LP_OPTIONS | NAND_BUSWIDTH_16)

    /* 512 Megabit */
    [0xa2] = { 64,	8,	0, 0, LP_OPTIONS },
    [0xf2] = { 64,	8,	0, 0, LP_OPTIONS },
    [0xb2] = { 64,	16,	0, 0, LP_OPTIONS16 },
    [0xc2] = { 64,	16,	0, 0, LP_OPTIONS16 },

    /* 1 Gigabit */
    [0xa1] = { 128, 8, 0, 0, LP_OPTIONS, 64,
             .param_page = {
                0x4F, 0x4E, 0x46, 0x49, 0x02, 0x00, 0x1, 0x0,
                0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         /*32*/ 0x4D, 0x49, 0x43, 0x52, 0x4F, 0x4E, 0x20, 0x20,
                0x20, 0x20, 0x20, 0x20, 0x4D, 0x54, 0x32, 0x39,
                0x46, 0x31, 0x47, 0x30, 0x38, 0x41, 0x42, 0x42,
                0x44, 0x41, 0x33, 0x57, 0x20, 0x20, 0x20, 0x20,
         /*64*/ 0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         /*80*/ 0x00, 0x08, 0x00, 0x00, 0x40, 0x00, 0x00, 0x20,
                0x00, 0x00, 0x10, 0x00, 0x40, 0x00, 0x00, 0x00,
                0x00, 0x04, 0x00, 0x00, 0x01, 0x22, 0x01, 0x14,
                0x00, 0x01, 0x05, 0x01, 0x00, 0x00, 0x04, 0x00,
                0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*120*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x0A, 0x1F, 0x00, 0x1F, 0x00, 0x58, 0x02, 0xB8,
                0x0B, 0x19, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*160*/ 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
                0x00, 0x02, 0x04, 0x80, 0x01, 0x81, 0x04, 0x01,
                0x02, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*200*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*240*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x09,
            },
        },
    [0xf1] = { 128,	8,	0, 0, LP_OPTIONS },
    [0xb1] = { 128,	16,	0, 0, LP_OPTIONS16 },
    [0xc1] = { 128,	16,	0, 0, LP_OPTIONS16 },

    /* 2 Gigabit */
    [0xaa] = {
        256, 8, 0, 0, LP_OPTIONS,
        .param_page = {
	        0x4F, 0x4E, 0x46, 0x49, 0x02, 0x00, 0x00, 0x00,
	        0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x4D, 0x49, 0x43, 0x52, 0x4F, 0x4E, 0x20, 0x20,
	        0x20, 0x20, 0x20, 0x20, 0x4D, 0x54, 0x32, 0x39,
	        0x46, 0x32, 0x47, 0x30, 0x38, 0x41, 0x42, 0x42,
	        0x45, 0x41, 0x48, 0x43, 0x20, 0x20, 0x20, 0x20,
	        0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x08, 0x00, 0x00, 0x40, 0x00, 0x00, 0x02,
	        0x00, 0x00, 0x10, 0x00, 0x40, 0x00, 0x00, 0x00,
	        0x00, 0x08, 0x00, 0x00, 0x01, 0x23, 0x01, 0x28,
	        0x00, 0x01, 0x05, 0x01, 0x00, 0x00, 0x04, 0x00,
	        0x04, 0x01, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x0A, 0x1F, 0x00, 0x1F, 0x00, 0x58, 0x02, 0xB8,
	        0x0B, 0x19, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
	        0x00, 0x02, 0x04, 0x80, 0x01, 0x81, 0x04, 0x01,
	        0x02, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x57, 0x1E
        }
    },
    [0xda] = { 256,	8,	0, 0, LP_OPTIONS },
    [0xba] = { 256,	16,	0, 0, LP_OPTIONS16 },
    [0xca] = { 256,	16,	0, 0, LP_OPTIONS16 },

    /* 4 Gigabit */
    [0xac] = { 512,	8,	0, 0, LP_OPTIONS },
    [0xdc] = { 512,	8,	0, 0, LP_OPTIONS },
    [0xbc] = { 512,	16,	0, 0, LP_OPTIONS16 },
    [0xcc] = { 512,	16,	0, 0, LP_OPTIONS16 },

    /* 8 Gigabit */
    [0xa3] = { 1024,	8,	0, 0, LP_OPTIONS },
    [0xd3] = { 1024,	8,	0, 0, LP_OPTIONS },
    [0xb3] = { 1024,	16,	0, 0, LP_OPTIONS16 },
    [0xc3] = { 1024,	16,	0, 0, LP_OPTIONS16 },

    /* 16 Gigabit */
    [0xa5] = { 2048,	8,	0, 0, LP_OPTIONS },
    [0xd5] = { 2048,	8,	0, 0, LP_OPTIONS },
    [0xb5] = { 2048,	16,	0, 0, LP_OPTIONS16 },
    [0xc5] = { 2048,	16,	0, 0, LP_OPTIONS16 },
};

static void nand_reset(DeviceState *dev)
{
    NANDFlashState *s = NAND(dev);
    s->cmd = NAND_CMD_READ0;
    s->addr = 0;
    s->addrlen = 0;
    s->iolen = 0;
    s->offset = 0;
    s->status &= NAND_IOSTATUS_UNPROTCT;
    s->status |= NAND_IOSTATUS_READY;
    s->status |= NAND_IOSTATUS_AREADY;
}

static inline void nand_pushio_byte(NANDFlashState *s, uint8_t value, bool reg)
{
    if (reg) { /* Push bytes for register read */
        s->regaddr[s->reglen++] = value;
        for (value = s->buswidth; --value;) {
            s->reg_data[s->reglen++] = 0;
        }
    } else {
        s->ioaddr[s->iolen++] = value;
        for (value = s->buswidth; --value;) {
            s->ioaddr[s->iolen++] = 0;
        }
    }
}

static void nand_blk_erase(NANDFlashState *s);
static void nand_blk_rw(NANDFlashState *s, int offset, bool is_write);

static void nand_command(NANDFlashState *s)
{
    int i, j;
    unsigned int offset;

    DB_PRINT_L("Executing NAND Command %x\n", s->cmd);
    switch (s->cmd) {

    case NAND_CMD_READID:
        s->reglen = 0;
        s->regaddr = s->reg_data;
        switch (s->addr & 0xFF) {
        case 0x20:
            nand_pushio_byte(s, 'O', true);
            nand_pushio_byte(s, 'N', true);
            nand_pushio_byte(s, 'F', true);
            nand_pushio_byte(s, 'I', true);
            break;
        case 0x00:
            nand_pushio_byte(s, s->manf_id, true);
            nand_pushio_byte(s, s->chip_id, true);
            nand_pushio_byte(s, 'Q', true); /* Don't-care byte (often 0xa5) */
            if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) {
                /* Page Size, Block Size, Spare Size; bit 6 indicates
                 * 8 vs 16 bit width NAND.
                 */
                nand_pushio_byte(s, (s->buswidth == 2) ? 0x55 : 0x15, true);
            } else {
                nand_pushio_byte(s, 0xc0, true); /* Multi-plane */
            }
            break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "Invalid address for NAND "
                              "Read ID command: %#02" PRIx8 "\n",
                              (uint8_t)s->addr);
        }
        break;

    case NAND_CMD_READ_PARAMETER_PAGE:
        s->ioaddr = s->io;
        s->ioaddr0 = s->io;
        s->iolen = 0;
        int num_parameter_pages = \
            nand_flash_ids[s->chip_id].param_page[NUM_PARAMETER_PAGES_OFFSET];

        /* If number of parameter pages not mentioned, use 3 as default */
        if (!num_parameter_pages) {
            num_parameter_pages = 3;
        }

        /* Copy Required number of parameter Pages */
        for (j = 0; j < num_parameter_pages; ++j) {
            for (i = 0; i < MAX_PARM_PAGE_SIZE; ++i) {
                nand_pushio_byte(s, nand_flash_ids[s->chip_id].param_page[i],
                                 false);
            }
        }

        /* Copy Required number of Ext parameter Pages */
        for (j = 0; j < num_parameter_pages; ++j) {
            for (i = MAX_PARM_PAGE_SIZE; \
                i < (MAX_PARM_PAGE_SIZE + MAX_EXT_PARM_PAGE_SIZE); ++i) {
                nand_pushio_byte(s, nand_flash_ids[s->chip_id].param_page[i],
                                 false);
            }
        }
        break;

    case NAND_CMD_SET_FEATURES:
        DB_PRINT_L("NAND set features started\n");
        s->iolen = 4;
        s->ioaddr = s->io;
        s->ioaddr0 = s->io;
        break;

    case NAND_CMD_GET_FEATURES:
        s->iolen = 0;
        s->ioaddr = s->io;
        s->ioaddr0 = s->io;
        for (i = s->addr & 0xFF; i < s->addr + 4; ++i) {
            nand_pushio_byte(s, s->features[i], false);
        }
        break;

    case NAND_CMD_COPYBACKPRG1:
        s->cmd = NAND_CMD_PAGEPROGRAM1;
    case NAND_CMD_RANDOMREAD2:
        if (s->addrlen <= 2) { /* column change only */
            offset = s->addr & ((1 << s->addr_shift) - 1);
            s->iolen += s->ioaddr - s->ioaddr0;
            s->ioaddr = s->ioaddr0 + offset;
            s->iolen -= offset;
            break;
        }
        /* random read with page change - fallthrough */
    case NAND_CMD_NOSERIALREAD2:
        if (!(nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP))
            break;
        s->offset = 0;
        /* fallthrough to regular read */
    case NAND_CMD_PAGEPROGRAM1:
    case NAND_CMD_READ0:
        offset = s->offset + (s->addr & ((1 << s->addr_shift) - 1));
        nand_blk_rw(s, offset, false);
        break;

    case NAND_CMD_RESET:
        nand_reset(DEVICE(s));
        break;

    case NAND_CMD_PAGEPROGRAM2:
        if (s->wp) {
            nand_blk_rw(s, 0, true);
        }
        break;

    case NAND_CMD_BLOCKERASE1:
        break;

    case NAND_CMD_BLOCKERASE2:
        s->addr &= (1ull << s->addrlen * 8) - 1;
        s->addr <<= s->addr_shift;
        if (s->wp) {
            nand_blk_erase(s);
        }
        break;

    case NAND_CMD_READSTATUS_ENHANCED:
    case NAND_CMD_READSTATUS:
        s->ioaddr = s->io;
        s->ioaddr0 = NULL;
        s->iolen = 0;
        nand_pushio_byte(s, s->status, false);
        break;

    default:
        printf("%s: Unknown NAND command 0x%02x\n", __FUNCTION__, s->cmd);
    }
}

static void nand_pre_save(void *opaque)
{
    NANDFlashState *s = NAND(opaque);

    s->ioaddr_vmstate = s->ioaddr - s->io;
}

static int nand_post_load(void *opaque, int version_id)
{
    NANDFlashState *s = NAND(opaque);

    if (s->ioaddr_vmstate > sizeof(s->io)) {
        return -EINVAL;
    }
    s->ioaddr = s->io + s->ioaddr_vmstate;

    return 0;
}

static const VMStateDescription vmstate_nand = {
    .name = "nand",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = nand_pre_save,
    .post_load = nand_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(cle, NANDFlashState),
        VMSTATE_UINT8(ale, NANDFlashState),
        VMSTATE_UINT8(ce, NANDFlashState),
        VMSTATE_UINT8(wp, NANDFlashState),
        VMSTATE_UINT8(gnd, NANDFlashState),
        VMSTATE_BUFFER(io, NANDFlashState),
        VMSTATE_UINT32(ioaddr_vmstate, NANDFlashState),
        VMSTATE_INT32(iolen, NANDFlashState),
        VMSTATE_UINT32(cmd, NANDFlashState),
        VMSTATE_UINT64(addr, NANDFlashState),
        VMSTATE_INT32(addrlen, NANDFlashState),
        VMSTATE_INT32(status, NANDFlashState),
        VMSTATE_INT32(offset, NANDFlashState),
        /* XXX: do we want to save s->storage too? */
        VMSTATE_END_OF_LIST()
    }
};

static void nand_realize(DeviceState *dev, Error **errp)
{
    uint64_t pagesize;
    NANDFlashState *s = NAND(dev);

    s->buswidth = nand_flash_ids[s->chip_id].width >> 3;
    s->size = nand_flash_ids[s->chip_id].size << 20;
    if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) {
        if (!nand_flash_ids[s->chip_id].page_shift) {
            s->page_shift = 11;
        } else {
            s->page_shift = nand_flash_ids[s->chip_id].page_shift;
        }
        if (!nand_flash_ids[s->chip_id].erase_shift) {
            s->erase_shift = 6;
        } else {
            s->erase_shift = nand_flash_ids[s->chip_id].erase_shift;
        }
    } else {
        s->page_shift = nand_flash_ids[s->chip_id].page_shift;
        s->erase_shift = nand_flash_ids[s->chip_id].erase_shift;
    }

    if (!nand_flash_ids[s->chip_id].oob_size) {
        s->oob_size = s->page_shift - (s->page_shift > 11 ? 3 : 5);
    } else {
        s->oob_size = nand_flash_ids[s->chip_id].oob_size;
    }

    s->pages = s->size >> s->page_shift;
    /* FIXME: Include in the table as a parameterisable property,
     * this assumption is not reliable!
     */
    s->addr_shift = s->page_shift > 9 ? 16 : 8;

    pagesize = s->oob_size;
    s->mem_oob = 1;
    if (s->blk) {
        if (blk_getlength(s->blk) >=
                (s->pages << s->page_shift) + (s->pages * s->oob_size)) {
            pagesize = 0;
            s->mem_oob = 0;
        }
    } else {
        pagesize += 1 << s->page_shift;
    }
    if (pagesize) {
        s->storage = (uint8_t *) memset(g_malloc(s->pages * pagesize),
                        0xff, s->pages * pagesize);
    }
    /* Give s->ioaddr a sane value in case we save state before it is used. */
    s->ioaddr = s->io;
    s->ioaddr = NULL;
}

static Property nand_properties[] = {
    DEFINE_PROP_UINT8("manufacturer_id", NANDFlashState, manf_id, 0),
    DEFINE_PROP_UINT8("chip_id", NANDFlashState, chip_id, 0),
    DEFINE_PROP_DRIVE("drive", NANDFlashState, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void nand_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nand_realize;
    dc->reset = nand_reset;
    dc->vmsd = &vmstate_nand;
    dc->props = nand_properties;
}

static const TypeInfo nand_info = {
    .name          = TYPE_NAND,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(NANDFlashState),
    .class_init    = nand_class_init,
};

static void nand_register_types(void)
{
    type_register_static(&nand_info);
}

/*
 * Chip inputs are CLE, ALE, CE, WP, GND and eight I/O pins.  Chip
 * outputs are R/B and eight I/O pins.
 *
 * CE, WP and R/B are active low.
 */
void nand_setpins(DeviceState *dev, uint8_t cle, uint8_t ale,
                  uint8_t ce, uint8_t wp, uint8_t gnd)
{
    NANDFlashState *s = NAND(dev);

    s->cle = cle;
    s->ale = ale;
    s->ce = ce;
    s->wp = wp;
    s->gnd = gnd;
    if (wp) {
        s->status |= NAND_IOSTATUS_UNPROTCT;
    } else {
        s->status &= ~NAND_IOSTATUS_UNPROTCT;
    }
}

void nand_getpins(DeviceState *dev, int *rb)
{
    *rb = 1;
}

void nand_setio(DeviceState *dev, uint32_t value)
{
    int i;
    NANDFlashState *s = NAND(dev);

    if (!s->ce && s->cle) {
        if (nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) {
            if (s->cmd == NAND_CMD_READ0 && value == NAND_CMD_LPREAD2)
                return;
            if (value == NAND_CMD_RANDOMREAD1) {
                s->addr &= ~((1 << s->addr_shift) - 1);
                s->addrlen = 0;
                s->cmd = value;
                return;
            }
        }
        if (value == NAND_CMD_READ0 || value == NAND_CMD_PAGEPROGRAM1) {
            s->offset = 0;
        } else if (value == NAND_CMD_READ1) {
            s->offset = 0x100;
            value = NAND_CMD_READ0;
        } else if (value == NAND_CMD_READ2) {
            s->offset = 1 << s->page_shift;
            value = NAND_CMD_READ0;
        }

        s->cmd = value;

        if (s->cmd == NAND_CMD_READSTATUS ||
                s->cmd == NAND_CMD_PAGEPROGRAM2 ||
                s->cmd == NAND_CMD_BLOCKERASE1 ||
                s->cmd == NAND_CMD_BLOCKERASE2 ||
                s->cmd == NAND_CMD_NOSERIALREAD2 ||
                s->cmd == NAND_CMD_RANDOMREAD2 ||
                s->cmd == NAND_CMD_RESET ||
                s->cmd == NAND_CMD_SET_FEATURES) {
            nand_command(s);
        }

        if (s->cmd != NAND_CMD_RANDOMREAD2) {
            s->addrlen = 0;
        }
    }

    if (s->ale) {
        unsigned int shift = s->addrlen * 8;
        uint64_t mask = ~(0xffULL << shift);
        uint64_t v = (uint64_t) value << shift;

        s->addr = (s->addr & mask) | v;
        s->addrlen ++;

        switch (s->addrlen) {
        case 1:
            if (s->cmd == NAND_CMD_READID ||
                    s->cmd == NAND_CMD_READ_PARAMETER_PAGE ||
                    s->cmd == NAND_CMD_GET_FEATURES) {
                nand_command(s);
            }
            break;
        case 2: /* fix cache address as a byte address */
            s->addr <<= (s->buswidth - 1);
            break;
        case 3:
            if (!(nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
                    (s->cmd == NAND_CMD_READ0 ||
                     s->cmd == NAND_CMD_PAGEPROGRAM1)) {
                nand_command(s);
            }
            break;
        case 4:
            if ((nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
                    nand_flash_ids[s->chip_id].size < 256 && /* 1Gb or less */
                    (s->cmd == NAND_CMD_READ0 ||
                     s->cmd == NAND_CMD_PAGEPROGRAM1)) {
                nand_command(s);
            }
            break;
        case 5:
            if ((nand_flash_ids[s->chip_id].options & NAND_SAMSUNG_LP) &&
                    nand_flash_ids[s->chip_id].size >= 256 && /* 2Gb or more */
                    (s->cmd == NAND_CMD_READ0 ||
                     s->cmd == NAND_CMD_PAGEPROGRAM1)) {
                nand_command(s);
            }
            break;
        default:
            break;
        }
    }

    if (!s->cle && !s->ale && s->cmd == NAND_CMD_COPYBACKPRG1) {
        nand_command(s);
    }

    if (!s->cle && !s->ale && (s->cmd == NAND_CMD_PAGEPROGRAM1)) {
        for (i = s->buswidth; i-- && s->iolen; value >>= 8) {
            *s->ioaddr &= (uint8_t)value;
            s->ioaddr++;
            s->iolen--;
        }
    }

    if (!s->cle && !s->ale && s->cmd == NAND_CMD_SET_FEATURES) {
        if (s->iolen--) {
            *s->ioaddr = value;
            s->ioaddr++;
        }
        if (!s->iolen) {
        s->features[s->addr & 0xFF] = (uint8_t)*s->ioaddr0;
        DB_PRINT_L("setting nand features: %x\n",
                       *(uint32_t *)s->ioaddr0);
        }
   }
}

static uint32_t nand_readreg(NANDFlashState *s)
{
    int offset;
    uint32_t x = 0;

    if (s->ce || s->reglen <= 0) {
        return 0;
    }

    for (offset = s->buswidth; offset--;) {
        x |= s->regaddr[offset] << (offset << 3);
    }
    s->reglen -= s->buswidth;
    s->regaddr += s->buswidth;
    return x;
}

uint32_t nand_getio(DeviceState *dev)
{
    int offset;
    uint32_t x = 0;
    NANDFlashState *s = NAND(dev);

    if (s->cmd == NAND_CMD_READID) {
        return nand_readreg(s);
    }

    if (s->ce || s->iolen <= 0) {
        return 0;
    }

    for (offset = s->buswidth; offset--;) {
        x |= s->ioaddr[offset] << (offset << 3);
    }
    /* after receiving READ STATUS command all subsequent reads will
     * return the status register value until another command is issued
     */
    if (s->cmd != NAND_CMD_READSTATUS) {
        s->addr   += s->buswidth;
        if (s->addr & (1 << s->page_shift)) {
            s->addr -= 1 << s->page_shift;
            s->addr += 1 << s->addr_shift;
        }
        s->ioaddr += s->buswidth;
        s->iolen  -= s->buswidth;
    }

    /* Allow sequential reading */
    if (!s->iolen && s->cmd == NAND_CMD_READ0) {
        nand_blk_rw(s, 0, false);
    }

    return x;
}

uint32_t nand_getbuswidth(DeviceState *dev)
{
    NANDFlashState *s = (NANDFlashState *) dev;
    return s->buswidth << 3;
}

DeviceState *nand_init(BlockBackend *blk, int manf_id, int chip_id)
{
    DeviceState *dev;

    if (nand_flash_ids[chip_id].size == 0) {
        hw_error("%s: Unsupported NAND chip ID.\n", __FUNCTION__);
    }
    dev = DEVICE(object_new(TYPE_NAND));
    qdev_prop_set_uint8(dev, "manufacturer_id", manf_id);
    qdev_prop_set_uint8(dev, "chip_id", chip_id);
    if (blk) {
        qdev_prop_set_drive_nofail(dev, "drive", blk);
    }

    qdev_init_nofail(dev);
    return dev;
}

type_init(nand_register_types)

/* Erase a single block */
static void nand_blk_erase(NANDFlashState *s)
{
    uint64_t page_size = nand_page_size(s);
    uint64_t erase_page = nand_page(s) & ~((1 << s->erase_shift) - 1);
    uint64_t erase_addr = erase_page << s->addr_shift;
    uint64_t erase_page_start = nand_page_start(s, erase_page);
    uint64_t oob_size = nand_oob_size(s);

    uint64_t i, sector;
    uint8_t iobuf[BDRV_SECTOR_SIZE] = { [0 ... BDRV_SECTOR_SIZE - 1] = 0xff, };

    if (erase_page >= s->pages) {
        return;
    }

    if (!s->blk) {
        memset(s->storage + erase_page_start,
                        0xff, (page_size + oob_size) << s->erase_shift);
    } else if (s->mem_oob) {
        memset(s->storage + (erase_page * s->oob_size),
                        0xff, oob_size << s->erase_shift);
        i = nand_sector(s, erase_addr);
        sector = nand_sector(s, erase_addr + (s->addr_shift + s->erase_shift));
        for (; i < sector; i ++) {
            if (blk_write(s->blk, i, iobuf, 1) < 0) {
                printf("%s: write error in sector %" PRIu64 "\n", __func__, i);
            }
        }
    } else {
        uint64_t erase_page_end = erase_page_start +
                        ((page_size + oob_size) << s->erase_shift);

        if (erase_page_start & BDRV_SECTOR_OFFSET_MASK) {
            i = erase_page_start;
            sector = i >> BDRV_SECTOR_BITS;
            if (blk_read(s->blk, sector, iobuf, 1) < 0) {
                printf("%s: read error in sector %" PRIu64 "\n", __func__, sector);
            }
            i &= BDRV_SECTOR_OFFSET_MASK;
            memset(iobuf + i, 0xff, BDRV_SECTOR_SIZE - i);
            if (blk_write(s->blk, sector, iobuf, 1) < 0) {
                printf("%s: write error in sector %" PRIu64 "\n", __func__, sector);
            }
        }

        memset(iobuf, 0xff, BDRV_SECTOR_SIZE);
        i = ROUND_UP(erase_page_start, BDRV_SECTOR_SIZE);
        for  (; i <= erase_page_end - BDRV_SECTOR_SIZE; i += BDRV_SECTOR_SIZE) {
            sector = i >> BDRV_SECTOR_BITS;
            if (blk_write(s->blk, sector, iobuf, 1) < 0) {
                printf("%s: write error in sector %" PRIu64 "\n",
                       __func__, sector);
            }
        }

        if (erase_page_end & BDRV_SECTOR_OFFSET_MASK) {
            sector = i >> BDRV_SECTOR_BITS;
            if (blk_read(s->blk, sector, iobuf, 1) < 0) {
                printf("%s: read error in sector %" PRIu64 "\n", __func__,
                       sector);
            }
            memset(iobuf, 0xff, erase_page_end & BDRV_SECTOR_OFFSET_MASK);
            if (blk_write(s->blk, sector, iobuf, 1) < 0) {
                printf("%s: write error in sector %" PRIu64 "\n", __func__,
                       sector);
            }
        }
    }
}

static void memcpy_dir(void *a, void *b, size_t n, bool reverse)
{
    memcpy(reverse ? b : a, reverse ? a : b, n);
}

static void nand_blk_rw(NANDFlashState *s, int offset, bool is_write)
{
    uint64_t page_addr = s->addr & ~ONES(s->addr_shift);
    uint64_t page_size = nand_page_size(s);
    uint64_t page = nand_page(s);
    uint64_t page_start = nand_page_start(s, page);
    uint64_t oob_size = nand_oob_size(s);
    uint64_t sector = nand_sector(s, page_addr);
    uint64_t sector_offset = nand_sector_offset(s, page_addr);
    int page_sectors = DIV_ROUND_UP(page_size, BDRV_SECTOR_SIZE);

    if (page >= s->pages) {
        return;
    }

    if (s->blk) {
        if (s->mem_oob) {
            if (blk_rw(s->blk, sector, s->io, page_sectors, is_write) < 0) {
                printf("%s: %s error in sector %" PRIu64 "\n",
                       __func__, is_write ? "write" : "read", sector);
            }
            memcpy_dir(s->io + sector_offset + page_size,
                       s->storage + (page * s->oob_size), oob_size, is_write);
            s->ioaddr0 = s->io + sector_offset;
        } else {
            if (blk_rw(s->blk, page_start >> BDRV_SECTOR_BITS,
                        s->io, (page_sectors + 4), is_write) < 0) {
                printf("%s: %s error in sector %" PRIu64 "\n",
                       __func__, is_write ? "write" : "read",
                       page_start >> BDRV_SECTOR_BITS);
            }
            s->ioaddr0 = s->io + (page_start & BDRV_SECTOR_OFFSET_MASK);
        }
    } else {
        memcpy_dir(s->io, s->storage + page_start, page_size + oob_size,
                   is_write);
        s->ioaddr0 = s->io;
    }
    s->ioaddr = s->ioaddr0 + offset;
    if (s->gnd)
        s->iolen = (1 << s->page_shift) - offset;
    else
        s->iolen = (1 << s->page_shift) + s->oob_size - offset;
}
