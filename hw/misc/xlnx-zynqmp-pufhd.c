/*
 * Fictitious PUF Helper-Data for Xilinx ZynqMP
 *
 * Copyright (c) 2020 Xilinx Inc.
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
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/hw.h"

#include "hw/misc/xlnx-aes.h"
#include "xlnx-zynqmp-pufhd.h"

/*
 * === Format of the fictitious ZynqMP PUF helper-data ===
 *
 * REGIS - PUF-data presented through PUF_WORD register during registration.
 *         (must be exactly 141 words as dictated by XilSKey; see URL below)
 *
 * eFUSE - PUF-data in stored in eFSUE
 *         (trimmed prior to being written; see URL below)
 *
 * For REGIS, "Byte Offset" is u8-index into a memory buffer that
 * xilskey_eps_zynqmp_puf.c uses to store the 32-bit REGIS words
 * read through the PUF_WORD register (it is also the byte offset
 * for PUF helper-data stored in boot header).
 *
 * For eFUSE, "Byte Offset" is u8-index, subtracted by 256, into
 * the blockdev file emulation the eFUSE.
 *
 * The info is either "REGIS only" or "eFUSE only" if the byte
 * offset is blank in other column.
 *
 * /--Byte Offset--\
 * REGIS    eFUSE      Bytes  Content
 * -----------------------------------------------
 * 0x0000              12     magic text "KE<<UFFA>>zP"
 * 0x000C              1      '\n'
 * 0x000D              3      0
 *          0x0000     2      magic text "<<"
 *          0x0002     2      (not for PUF helper data)
 *          0x0004     8      magic text "FAKEzPUF"
 *          0x000C     2      0
 *          0x000E     2      magic text ">>"
 *
 * 0x0010   0x0010    12      0
 * 0x001C              4      0
 * 0x0020             12      pufkey_u8[3,2,1,0,7,6,5,4,11,10,9,8]
 *          0x001C     1      pufkey_u8[0]
 *          0x001D     3      0
 *          0x0020     8      pufkey_u8[4,3,2,1,8,7,6,5]
 *          0x0028     1      0
 *          0x0029     3      pufkey_u8[11,10,9]
 * 0x002C   0x002C     4      0
 *
 * 0x0030   0x0030     12     0
 * 0x003C              4      0
 * 0x0040   0x003C     12     pufkey_u8[15,14,13,12,19,18,17,16,23,22,21,20]
 *          0x0048     4      0
 * 0x004C   0x004C     12     0
 * 0x0058              8      0
 *
 * 0x0060              8      pufkey_u8[27,26,25,24,31,30,29,28]
 * 0x0068              4      32-bit key-check hash (C-Hash), in little-endian
 * 0x006C              452    0
 * 0x0230              4      C-Hash, in host-endian, as required.
 * 0x0234              -      <END of REGIS; total 1128 bytes, 141 words>
 *          0x0058     3      pufkey_u8[26,25,24]
 *          0x005B     1      0
 *          0x005C     4      pufkey_u8[30,29,28,27]
 *          0x0060     3      (C-Hash >> 8), 24 bits, in little-endian
 *          0x0063     1      pufkey_u8[31]
 *          0x0064     3      0
 *          0x0067     1      (C-Hash & 255), 8 bits.
 *          0x0068     409    0
 *          0x01FC     -      <END of eFUSE; total 508 bytes, 4064 bits>
 *
 * (See later for the rationale behind the format design)
 *
 * When data are presented through the PUF_WORD register, the word count
 * must be exactly 141 words, as dictated by XilSKey. See lines 666, 707, 733:
 *   https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_eps_zynqmp_puf.c
 *
 * The "fake PUF key" is supplied as a "secret" object, whose id
 * is specified as the string value of the "puf-key-id" property
 * of the AES-engine node in the hardware (FDT-generic) device-tree.
 * A fictitious default "puf key" is used when either the "puf-key-id"
 * property or the "secret" object is missing.
 *
 * The format embeds the "fake PUF key" in plain-text inside the helper
 * data so the resulting blob can be fed into another simulation session.
 * That is, the QEMU PUF is very much clonable ;-j
 *
 * To ensure backward compatibility in future implementation, C-Hash
 * is also embedded into the helper data, to serve as versioning tag.
 *
 * There are 2 recipients of the 141-word PUF helper data read through
 * the PUF_WORD register during PUF-registration:
 * o-- As input to Xilinx 'bootgen' tool (UG1209, XAPP1333), or
 * o-- As input to ZynqMP eFUSE Programmer (XAPP1319)
 *
 *
 * === Input to 'bootgen' ===
 *
 * The 'bootgen' tool (https://github.com/Xilinx/bootgen) needs the
 * PUF helper data when user BIF constructs a ZynqMP boot-image in
 * "PUF Bootheader Mode" (UG1209, "PUF Registration - Boot Header Mode).
 *
 * The input is a u8 (byte-wise) hexdump of the memory buffer that
 * xilskey_eps_zynqmp_puf.c uses to store the 32-bit REGIS words of
 * PUF helper-data; see:
 *   https://www.xilinx.com/support/documentation/sw_manuals/xilinx2019_2/ug1209-embedded-design-tutorial.pdf
 *   (p.120, step 25)
 *   https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/examples/xilskey_puf_registration.c#L170
 *
 * The hexdump (all 141 words, plus extra padding) is then byte-wise
 * parsed into binary buffer then byte-wise stored in the boot image;
 * see lines 213 and 538 of:
 *   https://github.com/Xilinx/bootgen/blob/f9f477a/bootheader-zynqmp.cpp
 *
 *
 * === Input to eFUSE programmer ===
 *
 * When given to the ZynqMP eFUSE programmer, the data are first
 * "trimmed" before stored in a dedicated area of the ZynqMP eFUSE; see:
 *   https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/examples/xilskey_puf_registration.c#L423
 *
 * Note, in the trim code, the comment shows the data in little endian,
 * i.e., dropping
 *   1. All 8 bits at '(byte_offset % 16) + 12', and
 *   2. The least-significant nibble at '(byte_offset % 16) + 13'
 *
 * Pictorially ('X' is dropped nibble, 'n' is stored nibble):
 *   00 01 02 03  04 05 06 07  08 09 0a 0b  0c 0d 0e 0f <--(byte_offset % 16)
 *   nn nn nn nn  nn nn nn nn  nn nn nn nn  XX nX nn nn
 *   |  |  |  |
 *   |  |  |  \__ PUF_WORD >> 24
 *   |  |  \_____ PUF_WORD >> 16
 *   |  \________ PUF_WORD >>  8
 *   \___________ PUF_WORD >>  0
 *
 *
 * === Output from eFUSE programmer ===
 *
 * There are 2 outputs of the trimmed PUF helper-data:
 * o-- As hexdump, to UART by xilskey_puf_registration.c.
 * o-- Stored in emulated eFUSE block-device file, at offset 0x100.
 *     from file byte offset 0x0100.
 *
 * The UART-console hexdump is u32 (word-wise) big-endian
 * (using "%08X" format at line 608 of xilskey_puf_registration.c).
 *
 *
 * === REGIS Format Design Notes ===
 *
 * The embedded "fake PUF key" is in little-endian byte order, such
 * that 'xxd -e' of the eFUSE-blockdev file will see the key's nibbles
 * in the same order as that given by the "secret" object (via either
 * the ".data" or ".file" property).
 *
 * This format's data layout is designed for the embedded "fake PUF key"
 * to survive the bit trimming made prior to storing the helper data to
 * ZynqMP eFUSE. See:
 *
 * The format is also structured such that all embedded information is
 * byte-aligned to simplify data encoding and extraction.
 *
 * Note: 2019.1 release of XilSKey has a bug, where C-Hash was incorrectly
 *       extracted from offset 0x22C. This defect has been corrected in
 *       2019.2 or newer releases.
 */
enum Zynqmp_PUFHD_Const {
    XSK_ZYNQMP_MAX_RAW_4K_PUF_SYN_LEN = 140U,  /* excluding 32-bit C-hash */

    PUFHD_AUX_U24 = 0xaa22aa,
    PUFHD_CHASH_U32 = 0x44332211,
    PUFHD_FILLER_U32 = 0,
};

/*
 * Magic-string, in a strange byte-order such that hex-dump (or the
 * output from the Unix 'strings' command) of eFUSE binary file will
 * reveal a string of "FAKEzPUF".
 *
 * 'xxd -e' of the eFUSE binary file will show "<<..FAKEzPUF..>>"
 * at offset 0x0100.
 */
static const char Zynqmp_PUFHD_magic[12] = {
    [2]  = '<',
    [3]  = '<',

    [6]  = 'F',
    [7]  = 'A',
    [0]  = 'K',
    [1]  = 'E',

    [10] = 'z',
    [11] = 'P',
    [4]  = 'U',
    [5]  = 'F',

    [8]  = '>',
    [9]  = '>',
};

typedef struct Zynqmp_CommPUF {
    union {
        uint8_t  u8[2];
        uint16_t u16[6];
        uint32_t u32[3];
        char     magic[sizeof(Zynqmp_PUFHD_magic)];
    };
    uint8_t  x00c_ascii_012;
    uint8_t  x00d_0fill[3];
} Zynqmp_CommPUF;

typedef struct Zynqmp_RegisPUF {
    Zynqmp_CommPUF h;

    uint8_t  x010_0fill[16];
    uint8_t  pkey_00_11[12];
    uint8_t  x02c_0fill[20];
    uint8_t  pkey_12_23[12];
    uint8_t  x04c_0fill[20];
    uint8_t  pkey_24_31[8];
    uint32_t c_hash;

    /* tailing 0-fill is auto-generated */
} Zynqmp_RegisPUF;

typedef struct Zynqmp_EFusePUF {
    Zynqmp_CommPUF h;

    uint8_t  x010_0fill[12];
    uint8_t  pkey_00[1];
    uint8_t  x01d_0fill[3];
    uint8_t  pkey_01_08[8];
    uint8_t  x028_0fill[1];
    uint8_t  pkey_09_11[3];
    uint8_t  x02c_0fill[16];
    uint8_t  pkey_12_23[12];
    uint8_t  x048_0fill[16];
    uint8_t  pkey_24_26[3];
    uint8_t  x05b_0fill[1];
    uint8_t  pkey_27_30[4];
    uint8_t  c_hash_le24[3];
    uint8_t  pkey_31[1];
    uint8_t  x064_0fill[3];
    uint8_t  c_hash_lsb8[1];

    /* tailing 0-fill is ignored */
} Zynqmp_EFusePUF;

union Zynqmp_CheckPUF {
    /* Just a trick to do compile-time assert of struct layout */
    uint8_t  hdrsz[sizeof(Zynqmp_CommPUF) == 0x10 ? 1 : -1];
    uint8_t  headr[offsetof(Zynqmp_CommPUF,  x00c_ascii_012) == 0x0c ? 1 : -1];
    uint8_t  regis[offsetof(Zynqmp_RegisPUF, c_hash)         == 0x68 ? 1 : -1];
    uint8_t  efuse[offsetof(Zynqmp_EFusePUF, c_hash_lsb8)    == 0x67 ? 1 : -1];
};

typedef union Zynqmp_PUFKey {
    uint32_t u32[256 / 32];
    uint8_t  u8[256 / 8];
} Zynqmp_PUFKey;

typedef struct Zynqmp_PUFHD {
    ZynqMPAESKeySink *keysink;
    XLNXEFuse *efuse;
    qemu_irq *acc_err;

    Zynqmp_PUFKey key;  /* In byte-wise big-endian */

    uint32_t pufhd_words;
    uint32_t pufhd_fills;
    uint32_t pufhd_wnext;
    Zynqmp_RegisPUF pufhd_data;

} Zynqmp_PUFHD;

static void zynqmp_pufhd_kcpy(uint8_t *out, const uint8_t *in, size_t bcnt)
{
    size_t i;
    size_t r = bcnt % 4, m = bcnt - r;

    for (i = 0; i < m; i++) {
        out[i] = in[i ^ 3];
    }

    out += m;
    in  += m;
    switch (r) {
    case 1:
        out[0] = in[0];
        break;
    case 2:
        out[0] = in[1];
        out[1] = in[0];
        break;
    case 3:
        out[0] = in[2];
        out[2] = in[0];
        out[1] = in[1];
        break;
    }
}

static bool zynqmp_pufkey_from_regis(const Zynqmp_RegisPUF *rd, uint32_t size,
                                     uint32_t *c_hash, Zynqmp_PUFKey *key)
{
    if (size < sizeof(*rd)) {
        qemu_log("error: ZYNQMP PUF-REGENERATION: "
                 "PUF_WORD-form helper-data size (%u) < %u bytes!\n",
                 size, (uint32_t)sizeof(*rd));
        return false;
    }

    /* Extract PUF key from untrimmed helper-data into byte-wise big-endian */
    zynqmp_pufhd_kcpy(&(key->u8[0]),  rd->pkey_00_11, sizeof(rd->pkey_00_11));
    zynqmp_pufhd_kcpy(&(key->u8[12]), rd->pkey_12_23, sizeof(rd->pkey_12_23));
    zynqmp_pufhd_kcpy(&(key->u8[24]), rd->pkey_24_31, sizeof(rd->pkey_24_31));

    *c_hash = le32_to_cpu(rd->c_hash);
    return true;
}

static bool zynqmp_pufkey_from_efuse(const Zynqmp_EFusePUF *ed, uint32_t size,
                                     uint32_t *c_hash, Zynqmp_PUFKey *key)
{
    union {
        uint8_t u8[4];
        uint32_t u32;
    } le;

    if (size < sizeof(*ed)) {
        qemu_log("error: ZYNQMP PUF-REGENERATION: "
                 "eFUSE-form helper-data size (%u) < %u bytes!\n",
                 size, (uint32_t)sizeof(*ed));
        return false;
    }

    /* Extract PUF key from trimmed helper-data into byte-wise big-endian */
    zynqmp_pufhd_kcpy(&(key->u8[0]),  ed->pkey_00,    sizeof(ed->pkey_00));
    zynqmp_pufhd_kcpy(&(key->u8[1]),  ed->pkey_01_08, sizeof(ed->pkey_01_08));
    zynqmp_pufhd_kcpy(&(key->u8[9]),  ed->pkey_09_11, sizeof(ed->pkey_09_11));
    zynqmp_pufhd_kcpy(&(key->u8[12]), ed->pkey_12_23, sizeof(ed->pkey_12_23));
    zynqmp_pufhd_kcpy(&(key->u8[24]), ed->pkey_24_26, sizeof(ed->pkey_24_26));
    zynqmp_pufhd_kcpy(&(key->u8[27]), ed->pkey_27_30, sizeof(ed->pkey_27_30));
    zynqmp_pufhd_kcpy(&(key->u8[31]), ed->pkey_31,    sizeof(ed->pkey_31));

    memcpy(&le.u8[0], ed->c_hash_lsb8, 1);
    memcpy(&le.u8[1], ed->c_hash_le24, 3);

    *c_hash = le32_to_cpu(le.u32);
    return true;
}

static bool zynqmp_pufkey_from_buf(const Zynqmp_PUFRegen *data,
                                   uint32_t *c_hash, Zynqmp_PUFKey *key)
{
    uint32_t size = data->buffr.u8_cnt;
    const void *base = data->buffr.base;
    const Zynqmp_CommPUF *hd = base;

    if (size < sizeof(*hd)) {
        qemu_log("error: ZYNQMP PUF-REGENERATION: "
                 "Helper-data size too small!\n");
        return false;
    }

    /*
     * Check common header. This is to reject real PUF helper-data
     * given to simulation session, .e.g., booting a real-hardware
     * BOOT.BIN with real PUF helper-data.
     */
    if (memcmp(hd->magic, Zynqmp_PUFHD_magic, sizeof(hd->magic))) {
        qemu_log("error: ZYNQMP PUF-REGENERATION: "
                 "Helper-data header is missing magic string '%s'!\n",
                 Zynqmp_PUFHD_magic);
        return false;
    }

    switch (hd->x00c_ascii_012) {
    case '\n':
        return zynqmp_pufkey_from_regis(base, size, c_hash, key);
    case 0:
        return zynqmp_pufkey_from_efuse(base, size, c_hash, key);
    default:
        qemu_log("error: ZYNQMP PUF-REGENERATION: "
                 "Helper-data header type-tag invalid: %#x\n",
                 hd->x00c_ascii_012);
        return false;
    }
}

static Object *zynqmp_pufkey_parent(ZynqMPAESKeySink *sink)
{
    Object *obj;

    obj = OBJECT_CHECK(Object, sink, TYPE_OBJECT);
    assert(obj != NULL);
    assert(obj->parent != NULL);

    return obj->parent;
}

static void zynqmp_pufkey_import(Zynqmp_PUFHD *s)
{
    /*
     * The fake PUF key is provided by user, via the cmdline-provided
     * or FDT-provided "secret" object whose id is a string-valued
     * property of the parent object containing the PUF key-sink.
     *
     * The value is given and stored as big-endian in s->key.
     */
    xlnx_aes_k256_get_provided(zynqmp_pufkey_parent(s->keysink), "puf-key-id",
                               NULL, &s->key.u8, &error_abort);
}

static void zynqmp_pufkey_export(const Zynqmp_PUFKey *be,
                                 ZynqMPAESKeySink *sink)
{
    size_t i;
    Zynqmp_PUFKey key;
    ZynqMPAESKeySink *aes;
    uint8_t aes_devkey = 'P';

    if (!sink) {
        return;
    }

    /*
     * Key-sink expects:
     * 1. Each 32-bit in cpu endian; yet,
     * 2. The order of 8 32b-words in big endian.
     */
    for (i = 0; i < ARRAY_SIZE(key.u32); i++) {
        key.u32[i] = be32_to_cpu(be->u32[i]);
    }

    zynqmp_aes_key_update(sink, key.u8, sizeof(key.u8));

    aes = ZYNQMP_AES_KEY_SINK(zynqmp_pufkey_parent(sink));
    zynqmp_aes_key_update(aes, &aes_devkey, sizeof(aes_devkey));
}

static bool zynqmp_pufhd_efuse_regen(const Zynqmp_PUFRegen *data,
                                     uint32_t *c_hash, Zynqmp_PUFKey *key)
{
    uint32_t nr = data->efuse.base_row;
    uint32_t *hd_u32, *hd_e32;
    uint8_t *hd_u8, *hd_e8;
    Zynqmp_EFusePUF hd;
    Zynqmp_PUFRegen regen;

    /*
     * Only need a small portion from the start of the fake helper-data.
     *
     * Start of PUF helper-data stored in eFUSE are shifted by 16 bits from
     * <row 64, column 0> (XSK_ZYNQMP_EFUSEPS_PUF_ROW_HALF_WORD_SHIFT); See
     * function XilSKey_ZynqMp_EfusePs_ReadPufHelprData(), starting from 245:
     *   https://github.com/Xilinx/embeddedsw/blob/d70f3cd/lib/sw_services/xilskey/src/xilskey_eps_zynqmp_puf.c#L245
     *
     * And from the above code, the byte-order is quite perculiar:
     * ------------------------------------------------------
     *  HD      ROW    SHIFT    ROW BYTE-LANE (0 is LSB)
     * ------------------------------------------------------
     *  u8[2]   u32[0] >>  0    0
     *  u8[3]   u32[0] >>  8    1
     *  u8[0]   u32[1] >> 16    2
     *  u8[1]   u32[1] >> 24    3
     *
     *  u8[6]   u32[1] >>  0    0
     *  u8[7]   u32[1] >>  8    1
     *  u8[4]   u32[2] >> 16    2
     *  u8[5]   u32[2] >> 24    3
     *
     *  u8[a]   u32[2] >>  0    0
     *  u8[b]   u32[2] >>  8    1
     *  u8[8]   u32[3] >> 16    2
     *  u8[9]   u32[3] >> 24    3
     *
     *  u8[e]   u32[3] >>  0    0
     *  u8[f]   u32[3] >>  8    1
     *  u8[c]   u32[4] >> 16    2
     *  u8[d]   u32[4] >> 24    3
     * ------------------------------------------------------
     *
     * Apply this for copying the header only, so that the header
     * layout is identical to that of REGIS helper-data.
     *
     * The body will be copied as-is, i.e., little-endian bytes,
     * and no offset.
     */
    uint32_t prev, curr;

    hd_u8 = hd.h.u8;
    hd_e8 = hd.h.u8 + sizeof(hd.h);
    curr = efuse_get_row(data->efuse.dev, nr * 32);
    do {
        nr++;
        prev = curr;
        curr = efuse_get_row(data->efuse.dev, nr * 32);

        hd_u8[2] = 255 & prev;
        hd_u8[3] = 255 & (prev >>  8);
        hd_u8[0] = 255 & (curr >> 16);
        hd_u8[1] = 255 & (curr >> 24);
        hd_u8 += 4;
    } while (hd_u8 < hd_e8);

    hd_u32 = (uint32_t *)hd_e8;
    hd_e32 = (uint32_t *)(&hd + 1);
    do {
        curr = efuse_get_row(data->efuse.dev, nr * 32);
        nr++;

        *hd_u32 = cpu_to_le32(curr);
        hd_u32++;
    } while (hd_u32 < hd_e32);

    /* Now regen from filled buffer */
    memset(&regen, 0, sizeof(regen));
    regen.source = Zynqmp_PUFRegen_BUFFR;
    regen.buffr.base = hd.h.u8;
    regen.buffr.u8_cnt = sizeof(hd);

    return zynqmp_pufkey_from_buf(&regen, c_hash, key);
}

bool zynqmp_pufhd_regen(const Zynqmp_PUFRegen *data,
                        ZynqMPAESKeySink *keysink, uint32_t *c_hash)
{
    Zynqmp_PUFKey  key;
    uint32_t ch_tmp;

    c_hash = c_hash ? c_hash : &ch_tmp;

    switch (data->source) {
    case Zynqmp_PUFRegen_BUFFR:
        if (!zynqmp_pufkey_from_buf(data, c_hash, &key)) {
            goto regen_failed;
        }
        break;
    case Zynqmp_PUFRegen_EFUSE:
        if (!zynqmp_pufhd_efuse_regen(data, c_hash, &key)) {
            goto regen_failed;
        }
        break;
    default:
        qemu_log("error: ZYNQMP PUF-REGENERATION: "
                 "Unsupported helper-data source type: %u\n",
                 data->source);
        goto regen_failed;
    }

    /* Export the key to key-sink */
    zynqmp_pufkey_export(&key, keysink);

    /* Return C-Hash for caller to make use of, if any */
    *c_hash = be32_to_cpu(*c_hash);
    return true;

 regen_failed:
    return false;
}

Zynqmp_PUFHD *zynqmp_pufhd_new(ZynqMPAESKeySink *puf_keysink)
{
    Zynqmp_RegisPUF *pd;
    Zynqmp_PUFHD *s;

    s = g_malloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    s->keysink = puf_keysink;

    s->pufhd_words = XSK_ZYNQMP_MAX_RAW_4K_PUF_SYN_LEN + 1;
    s->pufhd_fills = sizeof(s->pufhd_data) / 4;

    pd = &s->pufhd_data;

    memcpy(pd->h.magic, Zynqmp_PUFHD_magic, sizeof(pd->h.magic));
    pd->h.x00c_ascii_012 = '\n';

    /* Import PUF-Key to populate the fake helper-data */
    zynqmp_pufkey_import(s);

    /* Copy byte-wise big-endian key into helper-data with byte-lanes swapped */
    zynqmp_pufhd_kcpy(pd->pkey_00_11, &(s->key.u8[0]),  sizeof(pd->pkey_00_11));
    zynqmp_pufhd_kcpy(pd->pkey_12_23, &(s->key.u8[12]), sizeof(pd->pkey_12_23));
    zynqmp_pufhd_kcpy(pd->pkey_24_31, &(s->key.u8[24]), sizeof(pd->pkey_24_31));

    /* Copy fixed-value C-Hash into helper-data in little-endian */
    pd->c_hash = cpu_to_le32(PUFHD_CHASH_U32);

    return s;
}

void zynqmp_pufhd_next(Zynqmp_PUFHD *s, uint32_t *word, uint32_t *status)
{
    /*
     * If reading past the end, there is nothing to update PUF_WORD with.
     * While not strictly API-compliant, client can re-read AUX from
     * PUF_STATUS and CHASH from PUF_WORD.
     */
    uint32_t next = s->pufhd_wnext;
    uint32_t last = s->pufhd_words - 1;

    *status &= ~PUF_STATUS_WRD_RDY;

    /*
     * For registration, the model for PUF_WORD-read is similar to
     * reading UART RX from a fifo, albeit each read is 32-bit wide.
     *
     * The 32-bit C-Hash presented in PUF_WORD must be in machine-endian.
     *
     * Push the Also, push the key out upon returning the last word.
     */
    if (next < s->pufhd_fills) {
        *word = s->pufhd_data.h.u32[next];
    } else if (next < last) {
        *word = PUFHD_FILLER_U32;
    } else if (next == last) {
        *word = le32_to_cpu(s->pufhd_data.c_hash);
        zynqmp_pufkey_export(&s->key, s->keysink);
    } else {
        qemu_log("warning: PUF-REGISTRATION "
                 "attempted to read beyond %u'th PUF_WORD\n",
                 last);
        return;
    }

    s->pufhd_wnext = next + 1;

    /*
     * 'status' will be updated to indicate state of next call, i.e.,
     * whether next call to this function will return a new word.
     *
     * Pushing of the key should be deferred as the side-effect of
     * reading C-HASH from PUF_WORD. But, the key-ready status must
     * not be deferred.
     */
    if (s->pufhd_wnext == last) {
        *status = (PUFHD_AUX_U24 << PUF_STATUS_AUX_SHIFT) | PUF_STATUS_KEY_RDY;
    }

    *status |= PUF_STATUS_WRD_RDY;
}
