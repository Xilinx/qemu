/*
 * QEMU model for asu sss
 *
 * Copyright (c) 2023 Advanced Micro Devices Inc.
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

typedef enum {
    ASU_DMA0,
    ASU_AES,
    ASU_SHA2,
    ASU_SHA3,
    ASU_PLI,
    ASU_NUM_REMOTES
} ASUSSSRemote;

static const char *asu_sss_remote_names[] = {
    [ASU_DMA0] = "dma0",
    [ASU_AES]  = "aes",
    [ASU_SHA2] = "sha2",
    [ASU_SHA3] = "sha3",
    [ASU_PLI]  = "pli",
};

static const uint32_t asu_sss_population[] = {
    [ASU_DMA0] = (1 << ASU_DMA0) | (1 << ASU_AES) | (1 << ASU_SHA2) |
                 (1 << ASU_SHA3),
    [ASU_AES]  = (1 << ASU_DMA0) | (1 << ASU_PLI),
    [ASU_SHA2] = (1 << ASU_DMA0) | (1 << ASU_PLI),
    [ASU_SHA3] = (1 << ASU_DMA0) | (1 << ASU_PLI),
    [ASU_PLI]  = (1 << ASU_DMA0) | (1 << ASU_AES),
    [ASU_NUM_REMOTES] = 0,
};

static const int r_asu_cfg_sss_shifts[] = {
    [ASU_DMA0] = 0,
    [ASU_AES]  = 4,
    [ASU_SHA2] = 8,
    [ASU_SHA3] = 12,
    [ASU_PLI]  = 16,
};

static const uint8_t r_asu_cfg_sss_encodings[] = {
    [ASU_DMA0] = ASU_DMA0,
    [ASU_AES]  = ASU_AES,
    [ASU_SHA2] = ASU_SHA2,
    [ASU_SHA3] = ASU_SHA3,
    [ASU_PLI]  = ASU_PLI,
};

/*
 * Remote Encodings:
 *                   DMA0   AES   SHA2  SHA3  PLI   NONE
 */
#define ASU_DMA0_MAP {0x05, 0x09, 0xFF, 0xFF, 0x0a, 0xFF}
#define ASU_AES_MAP  {0x05, 0xFF, 0xFF, 0xFF, 0x0a, 0xFF}
#define ASU_SHA2_MAP {0x05, 0xFF, 0xFF, 0xFF, 0x0a, 0xFF}
#define ASU_SHA3_MAP {0x05, 0xFF, 0xFF, 0xFF, 0x0a, 0xFF}
#define ASU_PLI_MAP  {0x05, 0x09, 0xFF, 0xFF, 0xFF, 0xFF}

static const uint8_t asu_sss_cfg_mapping[][MAX_REMOTE] = {
    [ASU_DMA0] = ASU_DMA0_MAP,
    [ASU_AES]  = ASU_AES_MAP,
    [ASU_SHA2] = ASU_SHA2_MAP,
    [ASU_SHA3] = ASU_SHA3_MAP,
    [ASU_PLI]  = ASU_PLI_MAP,
};
