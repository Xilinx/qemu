/*
 * QEMU model for pmc sss
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
    DMA0,
    DMA1,
    PTPI,
    AES,
    SHA0,
    SBI,
    PZM,
    SHA1,
    PMC_NUM_REMOTES
} PMCSSSRemote;

static const char *pmc_sss_remote_names[] = {
    [DMA0] = "dma0",
    [DMA1] = "dma1",
    [PTPI] = "ptpi",
    [AES] = "aes",
    [SHA0] = "sha",
    [SBI] = "sbi",
    [PZM] = "pzm",
    [SHA1] = "sha1",
};

static const uint32_t pmc_sss_population[] = {
    [DMA0] = (1 << DMA0) | (1 << AES) | (1 << SBI) | (1 << PZM),
    [DMA1] = (1 << DMA1) | (1 << AES) | (1 << SBI) | (1 << PZM),
    [PTPI] = (1 << DMA0) | (1 << DMA1),
    [AES] = (1 << DMA0) | (1 << DMA1),
    [SHA0] = (1 << DMA0) | (1 << DMA1),
    [SBI] = (1 << DMA0) | (1 << DMA1),
    [SHA1] = (1 << DMA0) | (1 << DMA1),
    [PMC_NUM_REMOTES] = 0,
};

static const int r_pmc_cfg_sss_shifts[] = {
    [DMA0] = 0,
    [DMA1] = 4,
    [PTPI] = 8,
    [AES] = 12,
    [SHA0] = 16,
    [SBI] = 20,
    [PZM] = -1,
    [SHA1] = 24,
};

static const uint8_t r_pmc_cfg_sss_encodings[] = {
    [DMA0] = DMA0,
    [DMA1] = DMA1,
    [PTPI] = PTPI,
    [AES] = AES,
    [SHA0] = SHA0,
    [SBI] = SBI,
    [PZM] = PZM,
    [SHA1] = SHA1,
};

/*
 * Remote Encodings:
 *               DMA0  DMA1  PTPI  AES   SHA0   SBI   PZM    SHA1  NONE
 */
#define DMA0_MAP {0xD,  0xFF, 0xFF, 0x6,  0xFF, 0xB,  0x3,   0xFF, 0xFF}
#define DMA1_MAP {0xFF, 0x9,  0xFF, 0x7,  0xFF, 0xE,  0x4,   0xFF, 0xFF}
#define PTPI_MAP {0xD,  0xA,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF}
#define AES_MAP  {0xE,  0x5,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF}
#define SHA0_MAP {0xC,  0x7,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF}
#define SBI_MAP  {0x5,  0xB,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF}
#define SHA1_MAP {0xA,  0xF,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF}

static const uint8_t pmc_sss_cfg_mapping[][MAX_REMOTE] = {
    [DMA0] = DMA0_MAP,
    [DMA1] = DMA1_MAP,
    [PTPI] = PTPI_MAP,
    [AES]  = AES_MAP,
    [SHA0] = SHA0_MAP,
    [SBI]  = SBI_MAP,
    [SHA1] = SHA1_MAP,
};
