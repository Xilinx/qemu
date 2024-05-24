/*
 * QEMU model of AMD/Xilinx Type-1 True Random Number Generator,
 * release 2.
 *
 * This is not a full device but an object to be embedded into
 * other devices based on this TRNG.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XLNX_TRNG1_R2_H
#define XLNX_TRNG1_R2_H

#include "hw/qdev-core.h"
#include "hw/register.h"
#include "hw/misc/xlnx-prng-if.h"

#define TYPE_XLNX_TRNG1_R2 "xlnx-trng1-r2"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxTRng1r2, XLNX_TRNG1_R2)

#define R_MAX_XLNX_TRNG1_R2 ((0x14 / 4) + 1)

typedef struct XlnxTRng1r2 {
    DeviceState parent;

    /* Support from container device */
    void (*intr_update)(Object *parent, bool pending);
    bool (*accessible)(Object *parent, bool wr);
    bool (*trss_avail)(Object *parent);
    const uint32_t *seed_life;

    /* Services to container device */
    void (*autoproc)(XlnxTRng1r2 *s, uint32_t seeding_ctrl);
    void (*get_data)(XlnxTRng1r2 *s, void *out, size_t bcnt);
    void (*hard_rst)(XlnxTRng1r2 *s);

    /* Device states */
    MemoryRegion *iomem;
    uint32_t forced_faults;
    uint32_t autoproc_ctrl;
    uint32_t int_status;

    /* Generator */
    struct {
        char *type;
        XlnxPRngIfClass *cls;
        XlnxPRngIf *obj;
        uint32_t seed_age;
    } prng;

    struct {
        unsigned wcnt;
        uint32_t vals[256 / 32];
    } rand;

    /* Entropy and seeding */
    struct {
        uint64_t trss_seed;
        uint64_t trss_fake_cnt;
        GArray  *test_input;
        uint8_t  test_input_buf;
        uint8_t  test_input_vld;
        size_t   test_output;
    } entropy;

    uint8_t sd384[384 / 8];

    uint32_t regs[R_MAX_XLNX_TRNG1_R2];
    RegisterInfo regs_info[R_MAX_XLNX_TRNG1_R2];
} XlnxTRng1r2;

#undef R_MAX_XLNX_TRNG1_R2

#endif
