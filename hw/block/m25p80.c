/*
 * ST M25P80 emulator. Emulate all SPI flash devices based on the m25p80 command
 * set. Known devices table current as of Jun/2012 and taken from linux.
 * See drivers/mtd/devices/m25p80.c.
 *
 * Copyright (C) 2011 Edgar E. Iglesias <edgar.iglesias@gmail.com>
 * Copyright (C) 2012 Peter A. G. Crosthwaite <peter.crosthwaite@petalogix.com>
 * Copyright (C) 2012 PetaLogix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) a later version of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/ssi.h"
#include "qemu/config-file.h"
#include "qemu/log.h"

#ifndef M25P80_ERR_DEBUG
#define M25P80_ERR_DEBUG 0
#endif

#define DB_PRINT_L(level, ...) do { \
    if (M25P80_ERR_DEBUG > (level)) { \
        qemu_log_mask_level(DEV_LOG_SPI_DEV, level, ": %s: ", __func__); \
        qemu_log_mask_level(DEV_LOG_SPI_DEV, level, ## __VA_ARGS__); \
    } \
} while (0);

/* Fields for FlashPartInfo->flags */

/* erase capabilities */
#define ER_4K 1
#define ER_32K 2
/* set to allow the page program command to write 0s back to 1. Useful for
 * modelling EEPROM with SPI flash command set
 */
#define WR_1 0x100

typedef struct FlashPartInfo {
    const char *part_name;
    /* jedec code. (jedec >> 16) & 0xff is the 1st byte, >> 8 the 2nd etc */
    uint32_t jedec;
    /* extended jedec code */
    uint16_t ext_jedec;
    /* there is confusion between manufacturers as to what a sector is. In this
     * device model, a "sector" is the size that is erased by the ERASE_SECTOR
     * command (opcode 0xd8).
     */
    uint32_t sector_size;
    uint32_t n_sectors;
    uint32_t page_size;
    uint8_t flags;

    uint8_t manf_id;
    uint8_t dev_id;
} FlashPartInfo;

/* FIXME: Make this accessible as a debug mode utility */

static void dump_flash_part_info(const FlashPartInfo *fp)
{
    fprintf(stderr, "%s jedec:%06x ext-jedec:%04x sector-size:%06x "
                    "num-sectors:%04x write-1:%c erase-4k:%c erase-32k:%c\n",
            fp->part_name, fp->jedec, fp->ext_jedec, fp->sector_size,
            fp->n_sectors, fp->flags & WR_1 ? 'Y' : 'N',
            fp->flags & ER_4K ? 'Y' : 'N',  fp->flags & ER_32K ? 'Y' : 'N');
}

/* adapted from linux */

#define INFO(_part_name, _jedec, _ext_jedec, _manf_id, _dev_id, _sector_size, _n_sectors, _flags)\
    .part_name = (_part_name),\
    .jedec = (_jedec),\
    .ext_jedec = (_ext_jedec),\
    .manf_id = (_manf_id), \
    .dev_id = (_dev_id), \
    .sector_size = (_sector_size),\
    .n_sectors = (_n_sectors),\
    .page_size = 256,\
    .flags = (_flags),\

#define JEDEC_NUMONYX 0x20
#define JEDEC_WINBOND 0xEF
#define JEDEC_SPANSION 0x01

static const FlashPartInfo known_devices[] = {
    /* Atmel -- some are (confusingly) marketed as "DataFlash" */
    { INFO("at25fs010",   0x1f6601,      0, 0x00, 0x00,  32 << 10,   4, ER_4K) },
    { INFO("at25fs040",   0x1f6604,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },

    { INFO("at25df041a",  0x1f4401,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },
    { INFO("at25df321a",  0x1f4701,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("at25df641",   0x1f4800,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },

    { INFO("at26f004",    0x1f0400,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },
    { INFO("at26df081a",  0x1f4501,      0, 0x00, 0x00,  64 << 10,  16, ER_4K) },
    { INFO("at26df161a",  0x1f4601,      0, 0x00, 0x00,  64 << 10,  32, ER_4K) },
    { INFO("at26df321",   0x1f4700,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },

    { INFO("at45db081d",  0x1f2500,      0, 0x00, 0x00,  64 << 10,  16, ER_4K) },

    /* EON -- en25xxx */
    { INFO("en25f32",     0x1c3116,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("en25p32",     0x1c2016,      0, 0x00, 0x00,  64 << 10,  64, 0) },
    { INFO("en25q32b",    0x1c3016,      0, 0x00, 0x00,  64 << 10,  64, 0) },
    { INFO("en25p64",     0x1c2017,      0, 0x00, 0x00,  64 << 10, 128, 0) },
    { INFO("en25q64",     0x1c3017,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },

    /* GigaDevice */
    { INFO("gd25q32",     0xc84016,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("gd25q64",     0xc84017,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },

    /* Intel/Numonyx -- xxxs33b */
    { INFO("160s33b",     0x898911,      0, 0x00, 0x00,  64 << 10,  32, 0) },
    { INFO("320s33b",     0x898912,      0, 0x00, 0x00,  64 << 10,  64, 0) },
    { INFO("640s33b",     0x898913,      0, 0x00, 0x00,  64 << 10, 128, 0) },
    { INFO("n25q064",     0x20ba17,      0, 0x00, 0x00,  64 << 10, 128, 0) },

    /* Macronix */
    { INFO("mx25l2005a",  0xc22012,      0, 0x00, 0x00,  64 << 10,   4, ER_4K) },
    { INFO("mx25l4005a",  0xc22013,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },
    { INFO("mx25l8005",   0xc22014,      0, 0x00, 0x00,  64 << 10,  16, 0) },
    { INFO("mx25l1606e",  0xc22015,      0, 0x00, 0x00,  64 << 10,  32, ER_4K) },
    { INFO("mx25l3205d",  0xc22016,      0, 0x00, 0x00,  64 << 10,  64, 0) },
    { INFO("mx25l6405d",  0xc22017,      0, 0x00, 0x00,  64 << 10, 128, 0) },
    { INFO("mx25l12805d", 0xc22018,      0, 0x00, 0x00,  64 << 10, 256, 0) },
    { INFO("mx25l12855e", 0xc22618,      0, 0x00, 0x00,  64 << 10, 256, 0) },
    { INFO("mx25l25635e", 0xc22019,      0, 0x00, 0x00,  64 << 10, 512, 0) },
    { INFO("mx25l25655e", 0xc22619,      0, 0x00, 0x00,  64 << 10, 512, 0) },

    /* Micron */
    { INFO("n25q032a11",  0x20bb16,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("n25q032a13",  0x20ba16,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("n25q064a11",  0x20bb17,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },
    { INFO("n25q064a13",  0x20ba17,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },
    { INFO("n25q128a11",  0x20bb18,      0, 0x00, 0x00,  64 << 10, 256, ER_4K) },
    { INFO("n25q128a13",  0x20ba18,      0, 0x00, 0x00,  64 << 10, 256, ER_4K) },
    { INFO("n25q256a11",  0x20bb19,      0, 0x00, 0x00,  64 << 10, 512, ER_4K) },
    { INFO("n25q256a13",  0x20ba19,      0, 0x00, 0x00,  64 << 10, 512, ER_4K) },
    { INFO("n25q512a11",  0x20bb20,      0, 0x00, 0x00,  64 << 10, 1024, ER_4K) },
    { INFO("n25q512a13",  0x20ba20,      0, 0x00, 0x00,  64 << 10, 1024, ER_4K) },

    /* Spansion -- single (large) sector size only, at least
     * for the chips listed here (without boot sectors).
     */
    { INFO("s25sl032p",   0x010215, 0x4d00, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("s25sl064p",   0x010216, 0x4d00, 0x00, 0x00,  64 << 10, 128, ER_4K) },
    { INFO("s25fl256s0",  0x010219, 0x4d00, 0x00, 0x00, 256 << 10, 128, 0) },
    { INFO("s25fl256s1",  0x010219, 0x4d01, 0x00, 0x00,  64 << 10, 512, 0) },
    { INFO("s25fl512s",   0x010220, 0x4d00, 0x00, 0x00, 256 << 10, 256, 0) },
    { INFO("s70fl01gs",   0x010221, 0x4d00, 0x00, 0x00, 256 << 10, 256, 0) },
    { INFO("s25sl12800",  0x012018, 0x0300, 0x00, 0x00, 256 << 10,  64, 0) },
    { INFO("s25sl12801",  0x012018, 0x0301, 0x00, 0x00,  64 << 10, 256, 0) },
    { INFO("s25fl129p0",  0x012018, 0x4d00, 0x00, 0x00, 256 << 10,  64, 0) },
    { INFO("s25fl129p1",  0x012018, 0x4d01, 0x00, 0x00,  64 << 10, 256, 0) },
    { INFO("s25sl004a",   0x010212,      0, 0x00, 0x00,  64 << 10,   8, 0) },
    { INFO("s25sl008a",   0x010213,      0, 0x00, 0x00,  64 << 10,  16, 0) },
    { INFO("s25sl016a",   0x010214,      0, 0x00, 0x00,  64 << 10,  32, 0) },
    { INFO("s25sl032a",   0x010215,      0, 0x00, 0x00,  64 << 10,  64, 0) },
    { INFO("s25sl064a",   0x010216,      0, 0x00, 0x00,  64 << 10, 128, 0) },
    { INFO("s25fl016k",   0xef4015,      0, 0x00, 0x00,  64 << 10,  32, ER_4K | ER_32K) },
    { INFO("s25fl064k",   0xef4017,      0, 0x00, 0x00,  64 << 10, 128, ER_4K | ER_32K) },

    /* SST -- large erase sizes are "overlays", "sectors" are 4<< 10 */
    { INFO("sst25vf040b", 0xbf258d,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },
    { INFO("sst25vf080b", 0xbf258e,      0, 0x00, 0x00,  64 << 10,  16, ER_4K) },
    { INFO("sst25vf016b", 0xbf2541,      0, 0x00, 0x00,  64 << 10,  32, ER_4K) },
    { INFO("sst25vf032b", 0xbf254a,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("sst25wf512",  0xbf2501,      0, 0x00, 0x00,  64 << 10,   1, ER_4K) },
    { INFO("sst25wf010",  0xbf2502,      0, 0x00, 0x00,  64 << 10,   2, ER_4K) },
    { INFO("sst25wf020",  0xbf2503,      0, 0x00, 0x00,  64 << 10,   4, ER_4K) },
    { INFO("sst25wf040",  0xbf2504,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },
    { INFO("sst25wf080",  0xbf2505,      0, 0xbf, 0x05,  64 << 10,  16, ER_4K) },

    /* ST Microelectronics -- newer production may have feature updates */
    { INFO("m25p05",      0x202010,      0, 0x00, 0x00,  32 << 10,   2, 0) },
    { INFO("m25p10",      0x202011,      0, 0x00, 0x00,  32 << 10,   4, 0) },
    { INFO("m25p20",      0x202012,      0, 0x00, 0x00,  64 << 10,   4, 0) },
    { INFO("m25p40",      0x202013,      0, 0x00, 0x00,  64 << 10,   8, 0) },
    { INFO("m25p80",      0x202014,      0, 0x00, 0x00,  64 << 10,  16, 0) },
    { INFO("m25p16",      0x202015,      0, 0x00, 0x00,  64 << 10,  32, 0) },
    { INFO("m25p32",      0x202016,      0, 0x00, 0x00,  64 << 10,  64, 0) },
    { INFO("m25p64",      0x202017,      0, 0x00, 0x00,  64 << 10, 128, 0) },
    { INFO("m25p128",     0x202018,      0, 0x00, 0x00, 256 << 10,  64, 0) },
    { INFO("n25q032",     0x20ba16,      0, 0x00, 0x00,  64 << 10,  64, 0) },

    { INFO("m45pe10",     0x204011,      0, 0x00, 0x00,  64 << 10,   2, 0) },
    { INFO("m45pe80",     0x204014,      0, 0x00, 0x00,  64 << 10,  16, 0) },
    { INFO("m45pe16",     0x204015,      0, 0x00, 0x00,  64 << 10,  32, 0) },

    { INFO("m25pe20",     0x208012,      0, 0x00, 0x00,  64 << 10,   4, 0) },
    { INFO("m25pe80",     0x208014,      0, 0x00, 0x00,  64 << 10,  16, 0) },
    { INFO("m25pe16",     0x208015,      0, 0x00, 0x00,  64 << 10,  32, ER_4K) },

    { INFO("m25px32",     0x207116,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("m25px32-s0",  0x207316,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("m25px32-s1",  0x206316,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("m25px64",     0x207117,      0, 0x00, 0x00,  64 << 10, 128, 0) },

    /* Winbond -- w25x "blocks" are 64k, "sectors" are 4KiB */
    { INFO("w25x10",      0xef3011,      0, 0x00, 0x00,  64 << 10,   2, ER_4K) },
    { INFO("w25x20",      0xef3012,      0, 0x00, 0x00,  64 << 10,   4, ER_4K) },
    { INFO("w25x40",      0xef3013,      0, 0x00, 0x00,  64 << 10,   8, ER_4K) },
    { INFO("w25x80",      0xef3014,      0, 0x00, 0x00,  64 << 10,  16, ER_4K) },
    { INFO("w25x16",      0xef3015,      0, 0x00, 0x00,  64 << 10,  32, ER_4K) },
    { INFO("w25x32",      0xef3016,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("w25q32",      0xef4016,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("w25q32dw",    0xef6016,      0, 0x00, 0x00,  64 << 10,  64, ER_4K) },
    { INFO("w25x64",      0xef3017,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },
    { INFO("w25q64",      0xef4017,      0, 0x00, 0x00,  64 << 10, 128, ER_4K) },
    { INFO("w25q80",      0xef5014,      0, 0x00, 0x00,  64 << 10,  16, ER_4K) },
    { INFO("w25q80bl",    0xef4014,      0, 0x00, 0x00,  64 << 10,  16, ER_4K) },
    { INFO("w25q256",     0xef4019,      0, 0x00, 0x00,  64 << 10, 512, ER_4K) },

    /* Numonyx -- n25q128 */
    { INFO("n25q128",      0x20ba18,      0, 0x00, 0x00,  64 << 10, 256, 0) },
};

typedef enum {
    NOP = 0,
    WRSR = 0x1,
    WRDI = 0x4,
    RDSR = 0x5,
    RDFSR = 0x70,
    WREN = 0x6,
    BRRD = 0x16,
    BRWR = 0x17,
    JEDEC_READ = 0x9f,
    RDID_90 = 0x90,
    RDID_AB = 0xab,
    READ_EXT_ADDR = 0xc8,
    WRITE_EXT_ADDR = 0xc5,
    ENTR_4B_ADDR = 0xb7,
    EXIT_4B_ADDR = 0xe9,

    BULK_ERASE_C7 = 0xc7,
    BULK_ERASE_60 = 0x60,

    READ = 0x3,                 READ_4 = 0x13,
    FAST_READ = 0xb,            FAST_READ_4 = 0x0c,
    DOR = 0x3b,                 DOR_4 = 0x3c,
    QOR = 0x6b,                 QOR_4 = 0x6c,
    DIOR = 0xbb,                DIOR_4 = 0xbc,
    QIOR = 0xeb,                QIOR_4 = 0xec,

    PP = 0x2,                   PP_4 = 0x12,
    DPP = 0xa2,                 /* FIXME: figure out DPP_4 */
    QPP = 0x32,                 QPP_4 = 0x34,
    AAI = 0xad,

    ERASE_4K = 0x20,
    ERASE_32K = 0x52,
    ERASE_SECTOR = 0xd8,        ERASE_SECTOR_4 = 0xdc,
} FlashCMD;

typedef enum {
    STATE_IDLE,
    STATE_PAGE_PROGRAM,
    STATE_READ,
    STATE_COLLECTING_DATA,
    STATE_READING_DATA,
    DUMMY_CYCLE_WAIT,
} CMDState;

typedef struct Flash {
    SSISlave parent_obj;

    uint32_t r;

    BlockBackend *blk;

    uint8_t *storage;
    uint32_t size;
    int page_size;

    uint8_t state;
    uint8_t data[16];
    uint32_t len;
    uint32_t pos;
    bool data_read_loop;
    uint8_t needed_bytes;
    uint8_t cmd_in_progress;
    uint64_t cur_addr;
    bool write_enable;

    bool aai_in_progress;
    bool addr_4b;
    int64_t dirty_page;

    uint8_t bar;
    uint8_t n_datalines;
    uint8_t n_dummy_cycles;
    uint8_t dummy_count;
    const FlashPartInfo *pi;

} Flash;

typedef struct M25P80Class {
    SSISlaveClass parent_class;
    FlashPartInfo *pi;
} M25P80Class;

#define TYPE_M25P80 "m25p80-generic"
#define M25P80(obj) \
     OBJECT_CHECK(Flash, (obj), TYPE_M25P80)
#define M25P80_CLASS(klass) \
     OBJECT_CLASS_CHECK(M25P80Class, (klass), TYPE_M25P80)
#define M25P80_GET_CLASS(obj) \
     OBJECT_GET_CLASS(M25P80Class, (obj), TYPE_M25P80)

static void blk_sync_complete(void *opaque, int ret)
{
    /* do nothing. Masters do not directly interact with the backing store,
     * only the working copy so no mutexing required.
     */
}

static void flash_sync_page(Flash *s, int page)
{
    int blk_sector, nb_sectors;
    QEMUIOVector iov;

    if (!s->blk || blk_is_read_only(s->blk)) {
        return;
    }

    blk_sector = (page * s->pi->page_size) / BDRV_SECTOR_SIZE;
    nb_sectors = DIV_ROUND_UP(s->pi->page_size, BDRV_SECTOR_SIZE);
    qemu_iovec_init(&iov, 1);
    qemu_iovec_add(&iov, s->storage + blk_sector * BDRV_SECTOR_SIZE,
                   nb_sectors * BDRV_SECTOR_SIZE);
    blk_aio_writev(s->blk, blk_sector, &iov, nb_sectors, blk_sync_complete,
                   NULL);
}

static inline void flash_sync_area(Flash *s, int64_t off, int64_t len)
{
    int64_t start, end, nb_sectors;
    QEMUIOVector iov;

    if (!s->blk || blk_is_read_only(s->blk)) {
        return;
    }

    assert(!(len % BDRV_SECTOR_SIZE));
    start = off / BDRV_SECTOR_SIZE;
    end = (off + len) / BDRV_SECTOR_SIZE;
    nb_sectors = end - start;
    qemu_iovec_init(&iov, 1);
    qemu_iovec_add(&iov, s->storage + (start * BDRV_SECTOR_SIZE),
                                        nb_sectors * BDRV_SECTOR_SIZE);
    blk_aio_writev(s->blk, start, &iov, nb_sectors, blk_sync_complete, NULL);
}

static void flash_erase(Flash *s, int offset, FlashCMD cmd)
{
    uint32_t len;
    uint8_t capa_to_assert = 0;

    switch (cmd) {
    case ERASE_4K:
        len = 4 << 10;
        capa_to_assert = ER_4K;
        break;
    case ERASE_32K:
        len = 32 << 10;
        capa_to_assert = ER_32K;
        break;
    case ERASE_SECTOR:
        len = s->pi->sector_size;
        break;
    case BULK_ERASE_60:
    case BULK_ERASE_C7:
        len = s->size;
        break;
    default:
        abort();
    }

    DB_PRINT_L(0, "offset = %#x, len = %d\n", offset, len);
    if ((s->pi->flags & capa_to_assert) != capa_to_assert) {
        qemu_log_mask(LOG_GUEST_ERROR, "M25P80: %d erase size not supported by"
                      " device\n", len);
    }

    if (!s->write_enable) {
        qemu_log_mask(LOG_GUEST_ERROR, "M25P80: erase with write protect!\n");
        return;
    }
    memset(s->storage + offset, 0xff, len);
    flash_sync_area(s, offset, len);
}

static inline void flash_sync_dirty(Flash *s, int64_t newpage)
{
    if (s->dirty_page >= 0 && s->dirty_page != newpage) {
        flash_sync_page(s, s->dirty_page);
        s->dirty_page = newpage;
    }
}

static inline void flash_write(Flash *s, uint8_t data, int num_bits)
{
    int64_t page = (s->cur_addr >> 3) / s->pi->page_size;
    uint8_t prev = s->storage[s->cur_addr >> 3];
    uint32_t data_mask = ((1ul << num_bits) - 1) <<
                         (8 - (s->cur_addr & 0x7) - num_bits);

    assert(!(data_mask & ~0xfful));
    data <<= 8 - (s->cur_addr & 0x7) - num_bits;

    if (!s->write_enable) {
        qemu_log_mask(LOG_GUEST_ERROR, "M25P80: write with write protect!\n");
    }

    if (s->pi->flags & WR_1) {
        s->storage[s->cur_addr >> 3] = (prev & ~data_mask) | (data & data_mask);
    } else {
        if ((prev ^ data) & data & data_mask) {
            DB_PRINT_L(1, "programming zero to one! addr=%" PRIx64 "  %" PRIx8
                       " -> %" PRIx8 ", mask = %" PRIx32 "\n",
                       s->cur_addr >> 3, prev, data, data_mask);
        }
        s->storage[s->cur_addr >> 3] &= data | ~data_mask;
    }

    flash_sync_dirty(s, page);
    s->dirty_page = page;
}


static inline void do_4_byte_address(Flash *s)
{
    s->cur_addr <<= 8;
    s->cur_addr |= s->data[3];
}

#define BAR_7_4_BYTE_ADDR    (1<<7)

static inline void check_4_byte_address(Flash *s)
{
    /* Allow 4byte address if MSB of bar register is set to 1
     * Or if 4byte addressing is allowed.
     */
    if ((s->bar & BAR_7_4_BYTE_ADDR) || s->addr_4b) {
        do_4_byte_address(s);
    } else {
        s->cur_addr |= s->bar << 24;
    }
}

static inline bool set_dummy_cycles(Flash *s, uint8_t num)
{
    if (s->dummy_count == 0) {
        /* Dummy Phase Yet to start */
        s->n_dummy_cycles = num * s->n_datalines;
        return true;
    } else {
        /* Dummy Phase done */
        s->dummy_count = 0;
        return false;
    }
}

static void complete_collecting_data(Flash *s)
{
    bool dummy_state = false;
    s->cur_addr = s->data[0] << 16;
    s->cur_addr |= s->data[1] << 8;
    s->cur_addr |= s->data[2];

    s->state = STATE_IDLE;

    switch (s->cmd_in_progress) {
    case DPP:
    case QPP:
    case AAI:
    case PP:
        check_4_byte_address(s);
        s->state = STATE_PAGE_PROGRAM;
        break;
    case QPP_4:
    case PP_4:
        do_4_byte_address(s);
        s->state = STATE_PAGE_PROGRAM;
        break;
    case FAST_READ:
    case DOR:
    case QOR:
    case DIOR:
    case QIOR:
        /* Fall through after executing dummy cycles/bytes */
        dummy_state = set_dummy_cycles(s, 1);
    case READ:
        if (dummy_state == true) {
            s->state = DUMMY_CYCLE_WAIT;
        } else {
            check_4_byte_address(s);
            s->state = STATE_READ;
        }
        break;
    case FAST_READ_4:
    case DOR_4:
    case QOR_4:
    case DIOR_4:
    case QIOR_4:
        /* Fall through after executing dummy cycles/bytes */
        dummy_state = set_dummy_cycles(s, 1);
    case READ_4:
        if (dummy_state == false) {
            do_4_byte_address(s);
            s->state = STATE_READ;
        } else {
            s->state = DUMMY_CYCLE_WAIT;
        }
        break;
    case ERASE_SECTOR:
    case ERASE_4K:
    case ERASE_32K:
        check_4_byte_address(s);
        flash_erase(s, s->cur_addr, s->cmd_in_progress);
        break;
    case ERASE_SECTOR_4:
        do_4_byte_address(s);
        flash_erase(s, s->cur_addr, s->cmd_in_progress);
        break;
    case WRSR:
        if (s->write_enable) {
            s->write_enable = false;
        }
        break;
    case WRITE_EXT_ADDR:
    case BRWR:
        s->bar = s->data[0];
        break;
    default:
        break;
    }

    s->cur_addr <<= 3;
}

static void decode_new_cmd(Flash *s, uint32_t value)
{
    s->cmd_in_progress = value;
    DB_PRINT_L(0, "decoded new command:%x\n", value);
    s->needed_bytes = 0;

    switch (value) {

    case READ_4:
    case ERASE_SECTOR_4:
    case QPP_4:
    case PP_4:
        if (s->addr_4b == false) {
            s->needed_bytes += 1;
        }
    case ERASE_4K:
    case ERASE_32K:
    case ERASE_SECTOR:
    case READ:
    case DPP:
    case QPP:
    case PP:
    case QOR:
    case FAST_READ:
    case DOR:
        if (s->addr_4b) {
            s->needed_bytes += 1;
        }
        s->needed_bytes += 3;
        s->pos = 0;
        s->len = 0;
        s->state = STATE_COLLECTING_DATA;
        break;

    case AAI:
        if (!s->aai_in_progress) {
            s->aai_in_progress = true;
            s->needed_bytes += 3;
            s->pos = 0;
            s->len = 0;
            s->state = STATE_COLLECTING_DATA;
        } else {
            s->state = STATE_PAGE_PROGRAM;
        }
        break;
    case FAST_READ_4:
    case DOR_4:
    case QOR_4:
        s->needed_bytes += 4;
        s->pos = 0;
        s->len = 0;
        s->state = STATE_COLLECTING_DATA;
        break;

    case DIOR_4:
        s->needed_bytes += 1;
    case DIOR:
        switch ((s->pi->jedec >> 16) & 0xFF) {
        case JEDEC_WINBOND:
        case JEDEC_SPANSION:
            s->needed_bytes += 4;
            break;
        case JEDEC_NUMONYX:
        default:
            s->needed_bytes += 5;
        }
        s->pos = 0;
        s->len = 0;
        s->state = STATE_COLLECTING_DATA;
        break;

    case QIOR_4:
        s->needed_bytes += 1;
    case QIOR:
        switch ((s->pi->jedec >> 16) & 0xFF) {
        case JEDEC_WINBOND:
        case JEDEC_SPANSION:
            s->needed_bytes += 6;
            break;
        case JEDEC_NUMONYX:
        default:
            s->needed_bytes += 8;
        }
        s->pos = 0;
        s->len = 0;
        s->state = STATE_COLLECTING_DATA;
        break;

    case WRSR:
        if (s->write_enable) {
            s->needed_bytes = 1;
            s->pos = 0;
            s->len = 0;
            s->state = STATE_COLLECTING_DATA;
        }
        break;

    case WRITE_EXT_ADDR:
    case BRWR:
        if (s->write_enable) {
            s->needed_bytes = 1;
            s->pos = 0;
            s->len = 0;
            s->state = STATE_COLLECTING_DATA;
        }
        break;

    case WRDI:
        s->write_enable = false;
        s->aai_in_progress = false;
        break;
    case WREN:
        s->write_enable = true;
        break;

    case RDSR:
        s->data[0] = (!!s->write_enable) << 1;
        s->pos = 0;
        s->len = 1;
        s->data_read_loop = true;
        s->state = STATE_READING_DATA;
        break;

    case RDFSR:
        s->data[0] = 1 << 7;
        s->pos = 0;
        s->len = 1;
        s->data_read_loop = true;
        s->state = STATE_READING_DATA;
        break;


    case READ_EXT_ADDR:
    case BRRD:
        s->data[0] = s->bar;
        s->pos = 0;
        s->len = 1;
        s->data_read_loop = false;
        s->state = STATE_READING_DATA;
        break;

    case JEDEC_READ:
        DB_PRINT_L(0, "populated jedec code\n");
        s->data[0] = (s->pi->jedec >> 16) & 0xff;
        s->data[1] = (s->pi->jedec >> 8) & 0xff;
        s->data[2] = s->pi->jedec & 0xff;
        if (s->pi->ext_jedec) {
            s->data[3] = (s->pi->ext_jedec >> 8) & 0xff;
            s->data[4] = s->pi->ext_jedec & 0xff;
            s->len = 5;
        } else {
            s->len = 3;
        }
        s->pos = 0;
        s->data_read_loop = false;
        s->state = STATE_READING_DATA;
        break;

    case RDID_90:
    case RDID_AB:
        DB_PRINT_L(0, "populated manf/dev ID\n");
        s->data[0] = s->pi->manf_id;
        s->data[1] = s->pi->dev_id;
        s->pos = 0;
        s->len = 2;
        s->data_read_loop = true;
        s->state = STATE_READING_DATA;
        break;

    case BULK_ERASE_60:
    case BULK_ERASE_C7:
        if (s->write_enable) {
            DB_PRINT_L(0, "chip erase\n");
            flash_erase(s, 0, value);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "M25P80: chip erase with write "
                          "protect!\n");
        }
        break;
    case ENTR_4B_ADDR:
        s->addr_4b = true;
        break;
    case EXIT_4B_ADDR:
        s->addr_4b = false;
        break;
    case NOP:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "M25P80: Unknown cmd %x\n", value);
        break;
    }
}

static int m25p80_cs(SSISlave *ss, bool select)
{
    Flash *s = M25P80(ss);

    if (select) {
        s->len = 0;
        s->pos = 0;
        s->state = STATE_IDLE;
        flash_sync_dirty(s, -1);
    }

    DB_PRINT_L(0, "%sselect\n", select ? "de" : "");

    return 0;
}

static void m25p80_num_datalines(SSISlave *ss, uint8_t lines)
{
    Flash *s = M25P80(ss);
    lines = lines == 0 ? 1 : lines;
    DB_PRINT_L(0, "Num of Data Lines change %d -> %d\n", s->n_datalines, lines);
    if (s->n_dummy_cycles) {
        s->n_dummy_cycles *= (lines / s->n_datalines);
    }
    s->n_datalines = lines;
}

static uint32_t m25p80_transfer(SSISlave *ss, uint32_t tx, int num_bits)
{
    Flash *s = M25P80(ss);
    uint32_t r = 0;

    if (!num_bits) {
        num_bits = 8;
    }

    switch (s->state) {

    case STATE_PAGE_PROGRAM:
        DB_PRINT_L(1, "page program cur_addr=%#" PRIx64 " data=%" PRIx8 "\n",
                   s->cur_addr, (uint8_t)tx);
        flash_write(s, (uint8_t)tx, num_bits);
        s->cur_addr += num_bits;
        break;

    case STATE_READ:
        assert((s->cur_addr & 0x7) + num_bits <= 8);
        r = s->storage[s->cur_addr >> 3] >>
            (8 - (s->cur_addr & 0x7) - num_bits);
        DB_PRINT_L(1, "READ 0x%" PRIx64 "=%" PRIx8 "\n", s->cur_addr,
                   (uint8_t)r);
        s->cur_addr = (s->cur_addr + num_bits) % (s->size * 8);
        break;

    case STATE_COLLECTING_DATA:
        assert(num_bits == 8);
        s->data[s->len] = (uint8_t)tx;
        s->len++;

        if (s->len == s->needed_bytes) {
            complete_collecting_data(s);
        }
        break;

    case STATE_READING_DATA:
        assert(num_bits == 8);
        r = s->data[s->pos];
        s->pos++;
        if (s->pos == s->len) {
            s->pos = 0;
            if (!s->data_read_loop) {
                s->state = STATE_IDLE;
            }
        }
        break;

    case DUMMY_CYCLE_WAIT:
        s->dummy_count++;
        DB_PRINT_L(0, "Dummy Byte/Cycle %d\n", s->dummy_count);
        s->n_dummy_cycles--;
        if (!s->n_dummy_cycles) {
            complete_collecting_data(s);
        }
        break;
    default:
    case STATE_IDLE:
        assert(num_bits == 8);
        decode_new_cmd(s, (uint8_t)tx);
        break;
    }

    return r;
}

static int m25p80_init(SSISlave *ss)
{
    QemuOpts *machine_opts;
    DriveInfo *dinfo;
    Flash *s = M25P80(ss);
    M25P80Class *mc = M25P80_GET_CLASS(s);

    s->pi = mc->pi;

    s->size = s->pi->sector_size * s->pi->n_sectors;
    s->dirty_page = -1;
    s->storage = blk_blockalign(s->blk, s->size);
    s->n_datalines = 1;

    dinfo = drive_get_next(IF_MTD);

    if (dinfo) {
        DB_PRINT_L(0, "Binding to IF_MTD drive\n");
        s->blk = blk_by_legacy_dinfo(dinfo);

        /* FIXME: Move to late init */
        if (blk_read(s->blk, 0, s->storage,
                     DIV_ROUND_UP(s->size, BDRV_SECTOR_SIZE))) {
            fprintf(stderr, "Failed to initialize SPI flash!\n");
            return 1;
        }
    } else {
        DB_PRINT_L(0, "No BDRV - binding to RAM\n");
        memset(s->storage, 0xFF, s->size);
    }

    machine_opts = qemu_opts_find(qemu_find_opts("machine"), 0);
    if (machine_opts && qemu_opt_get_bool(machine_opts, "trial", 0)) {
        int i;

        for (i = 0; i < ARRAY_SIZE(known_devices); ++i) {
            dump_flash_part_info(&known_devices[i]);
        }
    }
    return 0;
}

static void m25p80_reset(DeviceState *dev)
{
    Flash *s = M25P80(dev);

    s->bar = 0;
    s->addr_4b = false;
    s->dummy_count = 0;
}

static void m25p80_pre_save(void *opaque)
{
    flash_sync_dirty((Flash *)opaque, -1);
}

static const VMStateDescription vmstate_m25p80 = {
    .name = "xilinx_spi",
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_save = m25p80_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(state, Flash),
        VMSTATE_UINT8_ARRAY(data, Flash, 16),
        VMSTATE_UINT32(len, Flash),
        VMSTATE_UINT32(pos, Flash),
        VMSTATE_UINT8(needed_bytes, Flash),
        VMSTATE_UINT8(cmd_in_progress, Flash),
        VMSTATE_UINT64(cur_addr, Flash),
        VMSTATE_BOOL(write_enable, Flash),
        VMSTATE_END_OF_LIST()
    }
};

static void m25p80_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);
    M25P80Class *mc = M25P80_CLASS(klass);

    k->init = m25p80_init;
    k->transfer_bits = m25p80_transfer;
    k->set_cs = m25p80_cs;
    k->set_data_lines = m25p80_num_datalines;
    k->cs_polarity = SSI_CS_LOW;
    dc->reset = m25p80_reset;
    dc->vmsd = &vmstate_m25p80;
    mc->pi = data;
}

static const TypeInfo m25p80_info = {
    .name           = TYPE_M25P80,
    .parent         = TYPE_SSI_SLAVE,
    .instance_size  = sizeof(Flash),
    .class_size     = sizeof(M25P80Class),
    .abstract       = true,
};

static void m25p80_register_types(void)
{
    int i;

    type_register_static(&m25p80_info);
    for (i = 0; i < ARRAY_SIZE(known_devices); ++i) {
        TypeInfo ti = {
            .name       = known_devices[i].part_name,
            .parent     = TYPE_M25P80,
            .class_init = m25p80_class_init,
            .class_data = (void *)&known_devices[i],
        };
        type_register(&ti);
    }
}

type_init(m25p80_register_types)
