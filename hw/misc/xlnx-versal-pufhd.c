/*
 * Fictitious PUF Helper-Data for Xilinx Versal
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
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "crypto/hash.h"
#include "hw/qdev-properties.h"
#include "hw/hw.h"

#include "hw/misc/xlnx-aes.h"
#include "xlnx-versal-pufhd.h"

/*
 * === Format of the fictitious Versal PUF helper-data ===
 *
 * REGIS - PUF-data presented through PUF_WORD register during registration.
 *         There must be exactly 140 words for 4k, and 350 words for 12k.
 *
 * eFUSE - PUF-data in stored in eFUSE; Versal eFUSE supports only 4k mode.
 *         (trimmed prior to being written; see URL below)
 *
 * For REGIS, "Byte Offset" is u8-index into a memory buffer that
 * xpuf.c uses to store the 32-bit REGIS words read through the
 * PUF_WORD register (it is also the byte offset for PUF helper-data
 * stored in boot header).
 *
 * For eFUSE, "Byte Offset" is u8-index, subtracted by 0xA04, into
 * the blockdev file emulation the eFUSE.
 *
 * The info is either "REGIS only" or "eFUSE only" if the byte
 * offset is blank in the other column.
 *
 * /--Byte Offset--\
 * REGIS    eFUSE      Bytes  Content
 * -----------------------------------------------
 * 0x0000   0x0000     12     magic text "<<FAKEvPUF>>"
 * 0x000C              1      '\n'
 *          0x000C     1      0
 * 0x000D   0x000D     3      0
 *
 * 0x0010   0x0010     12     0
 * 0x001C              4      0
 * 0x0020              12     pufkey_u8[3,2,1,0,7,6,5,4,11,10,9,8]
 *          0x001C     3      pufkey_u8[2,1,0]
 *          0x001F     1      0
 *          0x0020     8      pufkey_u8[6,5,4,3,10,9,8,7]
 *          0x0028     3      0
 *          0x002B     1      pufkey_u8[11]
 * 0x002C   0x002C     4      0
 *
 * 0x0030   0x0030     8      0
 * 0x0038              8      0
 *
 * 0x0040              12     pufkey_u8[15,14,13,12,19,18,17,16,23,22,21,20]
 *          0x0038     2      pufkey_u8[13,12]
 *          0x003A     2      0
 *          0x003C     8      pufkey_u8[17,16,15,14,21,20,19,18]
 *          0x0044     2      0
 *          0x0046     2      pufkey_u8[23,22]
 *          0x0048     4      0
 * 0x004C   0x004C     8      0
 * 0x0058              12     0
 *
 * 0x0060              8      pufkey_u8[27,26,25,24,31,30,29,28]
 * 0x0068              4      32-bit key-check hash (C-Hash), in little-endian
 * 0x006C              452    0
 * 0x0230              -      <END of 4K REGIS; total 1124 bytes, 140 words>
 *          0x0054     1      pufkey_u8[24]
 *          0x0055     3      0
 *          0x0058     4      pufkey_u8[28,27,26,25]
 *          0x005C     1      (C-Hash >> 24)
 *          0x005D     3      pufkey_u8[31,30,29]
 *          0x0060     1      0
 *          0x0061     3      (C-Hash & 0x00FFFFFF), 24 bits, in little-endian
 *          0x0064     3      0
 *          0x0067     1      (C-Hash & 255), 8 bits.
 *          0x0068     409    0
 *          0x01FC     -      <END of eFUSE; total 508 bytes, 4064 bits>
 *
 * (See later for the rationale behind the format design)
 *
 * When data are presented through the PUF_WORD register, the word count
 * must be exactly 140 (4K) or 350 (12K) words, as dictated by XilPuf.
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
 * There are 2 recipients of the PUF helper data read through
 * the PUF_WORD register during PUF-registration:
 * o-- As input to Xilinx 'bootgen' tool (UG1209), or
 * o-- As input to Versal eFUSE Programmer (see XilPuf examples)
 *
 *
 * === Input to 'bootgen' ===
 *
 * Either 4k or 12k mode can be used.
 *
 * The 'bootgen' tool (https://github.com/Xilinx/bootgen) needs the
 * PUF helper data when user BIF constructs a Versal boot-image in
 * "PUF Bootheader Mode" (UG1209, "PUF Registration - Boot Header Mode).
 *
 * The input is a u8 (byte-wise) hexdump of the memory buffer that
 * xpuf.c uses to store the 32-bit REGIS words of PUF helper-data.
 *
 * The hexdump is then byte-wise parsed into binary buffer then
 * byte-wise stored in the boot image.
 *
 *
 * === Input to eFUSE programmer ===
 *
 * Only 4k mode can be used.
 *
 * When given to the XilNvm eFUSE programmer, the data are first
 * "trimmed" before stored in a dedicated area of the Versal eFUSE; see:
 *   https://github.com/Xilinx/embeddedsw/blob/release-2020.1/lib/sw_services/xilpuf/src/xpuf.c#L469
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
 * o-- As hexdump, to UART by xilpuf_example.c.
 * o-- Stored in emulated eFUSE block-device file, at offset 0xA04.
 *     from file byte offset 0x0A04.
 *
 * The UART-console output is byte-wise hexdump of efuse format,
 * a total of 127 words (508 bytes).
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
 * Versal eFUSE.
 *
 * The format is also structured such that all embedded information is
 * byte-aligned to simplify data encoding and extraction.
 */
enum Versal_PUFHD_Const {
    PUFHD_AUX_U24 = 0xaa22aa,
    PUFHD_CHASH_U32 = 0x44332211,
    PUFHD_FILLER_U32 = 0,

    PUFHD_WCNT_4K = 140,
    PUFHD_WCNT_12K = 350,
};

/*
 * Magic-string.
 *
 * 'xxd -e' of the eFUSE binary file will show "<<FAKEvPUF>>"
 * at offset 0x0A04.
 */
static const char Versal_PUFHD_magic[12] = "<<FAKEvPUF>>";

typedef struct Versal_CommPUF {
    union {
        uint8_t  u8[12];
        uint16_t u16[6];
        uint32_t u32[3];
        char     magic[sizeof(Versal_PUFHD_magic)];
    };
    uint8_t  x00c_ascii_012;
    uint8_t  x00d_0fill[3];
} Versal_CommPUF;

typedef struct Versal_RegisPUF {
    Versal_CommPUF h;

    uint8_t  x010_0fill[16];
    uint8_t  pkey_00_11[12];
    uint8_t  x02c_0fill[20];
    uint8_t  pkey_12_23[12];
    uint8_t  x04c_0fill[20];
    uint8_t  pkey_24_31[8];
    uint32_t c_hash;

    /* tailing 0-fill is auto-generated */
} Versal_RegisPUF;

typedef struct Versal_EFusePUF {
    Versal_CommPUF h;

    uint8_t  x010_0fill[12];
    uint8_t  pkey_00_02[3];
    uint8_t  x01f_0fill[1];
    uint8_t  pkey_03_10[8];
    uint8_t  x028_0fill[3];
    uint8_t  pkey_11[1];
    uint8_t  x02c_0fill[12];
    uint8_t  pkey_12_13[2];
    uint8_t  x03a_0fill[2];
    uint8_t  pkey_14_21[8];
    uint8_t  x044_0fill[2];
    uint8_t  pkey_22_23[2];
    uint8_t  x048_0fill[12];
    uint8_t  pkey_24[1];
    uint8_t  x055_0fill[3];
    uint8_t  pkey_25_28[4];
    uint8_t  c_hash_msb8[1];
    uint8_t  pkey_29_31[3];
    uint8_t  x060_0fill[1];
    uint8_t  c_hash_le24[3];

    /* tailing 0-fill is ignored */
} Versal_EFusePUF;

union Versal_CheckPUF {
    /* Just a trick to do compile-time assert of struct layout */
    uint8_t  hdrsz[sizeof(Versal_CommPUF) == 0x10 ? 1 : -1];
    uint8_t  headr[offsetof(Versal_CommPUF,  x00c_ascii_012) == 0x0c ? 1 : -1];
    uint8_t  regis[offsetof(Versal_RegisPUF, c_hash)         == 0x68 ? 1 : -1];
    uint8_t  efuse[offsetof(Versal_EFusePUF, c_hash_le24)    == 0x61 ? 1 : -1];
};

typedef union Versal_PufKey {
    uint32_t u32[256 / 32];
    uint8_t  u8[256 / 8];
} Versal_PufKey;

typedef struct Versal_PUFHD {
    ZynqMPAESKeySink *keysink;
    XLNXEFuse *efuse;
    qemu_irq *acc_err;

    Versal_PufKey key;  /* In byte-wise big-endian */

    uint32_t pufhd_words;
    uint32_t pufhd_fills;
    uint32_t pufhd_wnext;
    Versal_RegisPUF pufhd_data;

} Versal_PUFHD;

static void versal_pufhd_kcpy(uint8_t *out, const uint8_t *in, size_t bcnt)
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

static bool versal_pufkey_from_regis(const Versal_RegisPUF *rd, uint32_t size,
                                     uint32_t *c_hash, Versal_PufKey *key)
{
    if (size < sizeof(*rd)) {
        qemu_log("error: Versal PUF-REGENERATION: "
                 "PUF_WORD-form helper-data size (%u) < %u bytes!\n",
                 size, (uint32_t)sizeof(*rd));
        return false;
    }

    /* Extract PUF key from untrimmed helper-data into byte-wise big-endian */
    versal_pufhd_kcpy(&(key->u8[0]),  rd->pkey_00_11, sizeof(rd->pkey_00_11));
    versal_pufhd_kcpy(&(key->u8[12]), rd->pkey_12_23, sizeof(rd->pkey_12_23));
    versal_pufhd_kcpy(&(key->u8[24]), rd->pkey_24_31, sizeof(rd->pkey_24_31));

    *c_hash = le32_to_cpu(rd->c_hash);
    return true;
}

static bool versal_pufkey_from_efuse(const Versal_EFusePUF *ed, uint32_t size,
                                     uint32_t *c_hash, Versal_PufKey *key)
{
    union {
        uint8_t u8[4];
        uint32_t u32;
    } le;

    if (size < sizeof(*ed)) {
        qemu_log("error: Versal PUF-REGENERATION: "
                 "eFUSE-form helper-data size (%u) < %u bytes!\n",
                 size, (uint32_t)sizeof(*ed));
        return false;
    }

    /* Extract PUF key from trimmed helper-data into byte-wise big-endian */
    versal_pufhd_kcpy(&(key->u8[0]), ed->pkey_00_02, sizeof(ed->pkey_00_02));
    versal_pufhd_kcpy(&(key->u8[3]), ed->pkey_03_10, sizeof(ed->pkey_03_10));
    versal_pufhd_kcpy(&(key->u8[11]), ed->pkey_11, sizeof(ed->pkey_11));
    versal_pufhd_kcpy(&(key->u8[12]), ed->pkey_12_13, sizeof(ed->pkey_12_13));
    versal_pufhd_kcpy(&(key->u8[14]), ed->pkey_14_21, sizeof(ed->pkey_14_21));
    versal_pufhd_kcpy(&(key->u8[22]), ed->pkey_22_23, sizeof(ed->pkey_22_23));
    versal_pufhd_kcpy(&(key->u8[24]), ed->pkey_24, sizeof(ed->pkey_24));
    versal_pufhd_kcpy(&(key->u8[25]), ed->pkey_25_28, sizeof(ed->pkey_25_28));
    versal_pufhd_kcpy(&(key->u8[29]), ed->pkey_29_31, sizeof(ed->pkey_29_31));

    memcpy(&le.u8[3], ed->c_hash_msb8, 1);
    memcpy(&le.u8[0], ed->c_hash_le24, 3);

    *c_hash = le32_to_cpu(le.u32);
    return true;
}

static bool versal_pufkey_from_buf(const void *base, uint32_t size,
                                   uint32_t *c_hash, Versal_PufKey *key)
{
    const Versal_CommPUF *hd = base;

    if (size < sizeof(*hd)) {
        qemu_log("error: Versal PUF-REGENERATION: "
                 "Helper-data size too small!\n");
        return false;
    }

    /*
     * Check common header. This is to reject real PUF helper-data
     * given to simulation session, .e.g., booting a real-hardware
     * BOOT.BIN with real PUF helper-data.
     */
    if (memcmp(hd->magic, Versal_PUFHD_magic, sizeof(hd->magic))) {
        qemu_log("error: Versal PUF-REGENERATION: "
                 "Helper-data header is missing magic string '%s'!\n",
                 Versal_PUFHD_magic);
        return false;
    }

    switch (hd->x00c_ascii_012) {
    case '\n':
        return versal_pufkey_from_regis(base, size, c_hash, key);
    case 0:
        return versal_pufkey_from_efuse(base, size, c_hash, key);
    default:
        qemu_log("error: Versal PUF-REGENERATION: "
                 "Helper-data header type-tag invalid: %#x\n",
                 hd->x00c_ascii_012);
        return false;
    }
}

static Object *versal_pufkey_parent(ZynqMPAESKeySink *sink)
{
    Object *obj;

    obj = OBJECT_CHECK(Object, sink, TYPE_OBJECT);
    assert(obj != NULL);
    assert(obj->parent != NULL);

    return obj->parent;
}

static void versal_pufkey_to_id(const Versal_PufKey *key, Versal_PUFExtra *info)
{
    int i, n = ARRAY_SIZE(info->puf_id);
    char *hash;

    assert(n == (256 / 32));

    /* For simulation, ID is just sha256 of the 256-bit key */
    qcrypto_hash_digest(QCRYPTO_HASH_ALG_SHA256, (const char *)key->u8,
                        sizeof(key->u8), &hash, &error_abort);

    /* puf_id[0] is always the least significant word */
    for (i = 0; i < n; i++) {
        info->puf_id[n - 1 - i] = ldl_be_p(hash + (4 * i));
    }

    g_free(hash);
}

static void versal_pufkey_export(const Versal_PufKey *be,
                                 ZynqMPAESKeySink *sink, Versal_PUFExtra *info)
{
    size_t i;
    Versal_PufKey key;

    /* Derive the ID from the key */
    versal_pufkey_to_id(be, info);

    if (!sink) {
        return;
    }

    if (info->id_only) {
        /* Don't reveal the puf-key to keysink if id only */
        memset(&key, 0, sizeof(key));
    } else {
        /*
         * Key-sink expects:
         * 1. Each 32-bit in cpu endian; yet,
         * 2. The order of 8 32b-words in big endian.
         */
        for (i = 0; i < ARRAY_SIZE(key.u32); i++) {
            key.u32[i] = be32_to_cpu(be->u32[i]);
        }
    }

    zynqmp_aes_key_update(sink, key.u8, sizeof(key.u8));
}

static void versal_pufkey_import(Versal_PUFHD *s)
{
    /*
     * The fake PUF key is provided by user, via the cmdline-provided
     * or FDT-provided "secret" object whose id is a string-valued
     * property of the parent object containing the PUF key-sink.
     *
     * The value is given and stored as big-endian in s->key.
     */
    xlnx_aes_k256_get_provided(versal_pufkey_parent(s->keysink), "puf-key-id",
                               NULL, &s->key.u8, &error_abort);
}

static bool versal_pufhd_efuse_regen(const Versal_PUFRegen *data,
                                     uint32_t *c_hash, Versal_PufKey *key)
{
    uint32_t nr = data->efuse.base_row;
    uint32_t *hd_u32, *hd_e32;
    Versal_EFusePUF hd;

    /* Only need a small portion from the start of the fake helper-data. */
    hd_u32 = hd.h.u32;
    hd_e32 = (uint32_t *)(&hd + 1);
    do {
        uint32_t curr;

        curr = efuse_get_row(data->efuse.dev, nr * 32);
        nr++;

        *hd_u32 = cpu_to_le32(curr);
        hd_u32++;
    } while (hd_u32 < hd_e32);

    return versal_pufkey_from_buf(hd.h.u32, sizeof(hd), c_hash, key);
}

static bool versal_pufhd_mem_regen(const Versal_PUFRegen *data,
                                   uint32_t *c_hash, Versal_PufKey *key)
{
    Versal_RegisPUF hd;
    MemTxResult result;

    /* helper-data from memory is not trimmed */
    result = address_space_read(data->mem.as, data->mem.addr, data->mem.attr,
                                &hd, sizeof(hd));
    if (result != MEMTX_OK) {
        return false;
    }

    return versal_pufkey_from_buf(hd.h.u32, sizeof(hd), c_hash, key);
}

bool versal_pufhd_regen(Versal_PUFRegen *data, ZynqMPAESKeySink *keysink)
{
    Versal_PufKey  key;
    uint32_t c_hash;

    switch (data->source) {
    case Versal_PUFRegen_EFUSE:
        if (!versal_pufhd_efuse_regen(data, &c_hash, &key)) {
            goto regen_failed;
        }
        break;
    case Versal_PUFRegen_MEM:
        if (!versal_pufhd_mem_regen(data, &c_hash, &key)) {
            goto regen_failed;
        }
        break;
    default:
        qemu_log("error: Versal PUF-REGENERATION: "
                 "Unsupported helper-data source type: %u\n",
                 data->source);
        goto regen_failed;
    }

    /* Return C-Hash for caller to make use of, if any */
    data->info.c_hash = c_hash;

    /* Derive ID from key and export the key to key-sink */
    versal_pufkey_export(&key, keysink, &data->info);

    return true;

 regen_failed:
    return false;
}

Versal_PUFHD *versal_pufhd_new(ZynqMPAESKeySink *puf_keysink, bool is_12k)
{
    Versal_RegisPUF *pd;
    Versal_PUFHD *s;

    s = g_malloc(sizeof(*s));
    memset(s, 0, sizeof(*s));

    s->keysink = puf_keysink;

    s->pufhd_words = is_12k ? PUFHD_WCNT_12K : PUFHD_WCNT_4K;
    s->pufhd_fills = sizeof(s->pufhd_data) / 4;

    pd = &s->pufhd_data;

    memcpy(pd->h.magic, Versal_PUFHD_magic, sizeof(pd->h.magic));
    pd->h.x00c_ascii_012 = '\n';

    /* Import PUF-Key to populate the fake helper-data */
    versal_pufkey_import(s);

    /* Copy byte-wise big-endian key into helper-data with byte-lanes swapped */
    versal_pufhd_kcpy(pd->pkey_00_11, &(s->key.u8[0]),  sizeof(pd->pkey_00_11));
    versal_pufhd_kcpy(pd->pkey_12_23, &(s->key.u8[12]), sizeof(pd->pkey_12_23));
    versal_pufhd_kcpy(pd->pkey_24_31, &(s->key.u8[24]), sizeof(pd->pkey_24_31));

    /* Copy fixed-value C-Hash into helper-data in little-endian */
    pd->c_hash = cpu_to_le32(PUFHD_CHASH_U32);

    return s;
}

bool versal_pufhd_next(Versal_PUFHD *s, uint32_t *word, Versal_PUFExtra *info)
{
    /*
     * If reading past the end, there is nothing to update PUF_WORD with.
     * While not strictly API-compliant, client can re-read AUX from
     * PUF_STATUS and CHASH from PUF_WORD.
     */
    uint32_t next = s->pufhd_wnext;
    uint32_t last = s->pufhd_words - 1;

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
        *word = PUFHD_FILLER_U32;

        info->c_hash = PUFHD_CHASH_U32;
        info->aux = PUFHD_AUX_U24;
        versal_pufkey_export(&s->key, s->keysink, info);
    } else {
        qemu_log("warning: Versal PUF-REGISTRATION "
                 "attempted to read beyond %u'th PUF_WORD\n",
                 last);
        return false;
    }

    s->pufhd_wnext = next + 1;

    return (next == last);
}
