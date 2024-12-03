/*
 * QEMU model of the Xilinx ASU AES computation engine.
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef XLNX_ASU_AES_H
#define XLNX_ASU_AES_H

#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/stream.h"
#include "hw/crypto/xlnx-pmxc-key-transfer.h"

#define TYPE_XLNX_ASU_AES "xlnx.asu_aes"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxAsuAes, XLNX_ASU_AES);

#define ASU_AES_KV_R_MAX    (0x1a4 / 4 + 1)
#define ASU_AES_R_MAX       (0x114 / 4 + 1)
#define ASU_AES_128BITS_U8  (128 / 8)
#define ASU_AES_256BITS_U8  (256 / 8)

typedef struct XlnxAsuAes {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq_kv_interrupt;
    qemu_irq irq_aes_interrupt;

    bool kv_qtest;
    bool noisy_gerr;

    /* Cipher stream-output context */
    struct {
        StreamSink *dev;
        void *buf;
        size_t bcnt;
        size_t next;
        bool   last;
    } out;

    /* Cipher stream-input ready notification context */
    struct {
        StreamCanPushNotifyFn notify;
        void *notify_opaque;
    } inp;

    /* Partial-block data collected from stream-input */
    uint8_t partial[ASU_AES_128BITS_U8] QEMU_ALIGNED(8);
    unsigned partial_bcnt;

    /*
     * Cipher operation context.
     *
     * Key/iv/mac are all in big-endian, and 128b-key are in key[16..31],
     * with [0..15] as 0s.
     *
     * 2 copies of keys:
     * - one to hold "load into cipher" by software, and used to
     *   initialize the crypto lib used by the model.
     * - another as "in use", latched upon init of a new stream
     *   session and possibly used again mid-stream, without being
     *   concerned about disruption by software doing a mid-stream
     *   "key load" (which is invalid anyway).
     */
    struct {
        void    *cntx;
        uint64_t aad_bcnt;
        uint64_t aad_used;
        uint64_t aad_bmax;
        uint64_t txt_bcnt;
        uint64_t txt_bmax;
        uint64_t txt_used;
        union {
            uint32_t flags;
            struct {
                uint32_t mode:4,
                         enc:1,
                         aad_phase:1,
                         txt_phase:1,
                         fin_phase:1,
                         mac_valid:1,
                         in_error:1;
            };
        };

        uint8_t  be_iv_in[ASU_AES_128BITS_U8] QEMU_ALIGNED(8);
        uint8_t  be_iv_out[ASU_AES_128BITS_U8];
        uint8_t  be_mac_out[ASU_AES_128BITS_U8];

        uint8_t  be_key_in[ASU_AES_256BITS_U8] QEMU_ALIGNED(8);
        uint8_t  be_key_in_len;

        uint8_t  be_key_out[ASU_AES_256BITS_U8] QEMU_ALIGNED(8);
        uint8_t  be_key_out_len;
    } cipher;

    /* Transferred-in keys, all in big-endian */
    uint8_t efuse_ukey0_black[ASU_AES_256BITS_U8] QEMU_ALIGNED(8);
    uint8_t efuse_ukey1_black[ASU_AES_256BITS_U8];
    uint8_t efuse_ukey0_red[ASU_AES_256BITS_U8];
    uint8_t efuse_ukey1_red[ASU_AES_256BITS_U8];
    uint8_t puf_key[ASU_AES_256BITS_U8];

    /* Local key stores */
    uint32_t kv[ASU_AES_KV_R_MAX];
    RegisterInfo kv_regs_info[ASU_AES_KV_R_MAX];

    PmxcKeyXferIf *pmxc_aes;
    /* Controller */
    uint32_t regs[ASU_AES_R_MAX];
    RegisterInfo regs_info[ASU_AES_R_MAX];
} XlnxAsuAes;

#undef ASU_AES_KV_R_MAX
#undef ASU_AES_R_MAX
#undef ASU_AES_128BITS_U8
#undef ASU_AES_256BITS_U8

#endif
