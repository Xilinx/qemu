/*
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef XLNX_PMX_EFUSE_H
#define XLNX_PMX_EFUSE_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/zynqmp_aes_key.h"
/*=== Temporary hack until integrated with upstream hw/nvram/xlnx-efuse.h */
#include "hw/block/xlnx-efuse.h"
#define XlnxEFuse XLNXEFuse
#define xlnx_efuse_get_row(e, b) efuse_get_row((e), (b))
#define xlnx_efuse_get_bit(e, b) efuse_get_bit((e), (b))
#define xlnx_efuse_set_bit(e, b) efuse_set_bit((e), (b))
#define xlnx_efuse_tbits_check(e) efuse_tbits_check(e)
/*---*/

#ifndef XLNX_PMX_EFUSE_CTRL_R_MAX
#define XLNX_PMX_EFUSE_CTRL_R_MAX  (1 + (0x70 / 4))
#endif

#define TYPE_XLNX_PMX_EFUSE_CTRL  "xlnx.pmx_efuse_ctrl"
#define TYPE_XLNX_PMX_EFUSE_CACHE "xlnx.pmx_efuse_cache"

OBJECT_DECLARE_SIMPLE_TYPE(XlnxPmxEFuseCtrl,  XLNX_PMX_EFUSE_CTRL);
OBJECT_DECLARE_SIMPLE_TYPE(XlnxPmxEFuseCache, XLNX_PMX_EFUSE_CACHE);

typedef struct XlnxPmxEFuseCtrl {
    SysBusDevice parent_obj;
    qemu_irq irq_efuse_imr;

    XlnxEFuse *efuse;
    ZynqMPAESKeySink *aes_key_sink;
    ZynqMPAESKeySink *usr_key0_sink;
    ZynqMPAESKeySink *usr_key1_sink;

    bool ac_dme;
    bool ac_dna;
    bool ac_factory;
    bool ac_rfsoc;
    bool ac_row0;

    uint32_t regs[XLNX_PMX_EFUSE_CTRL_R_MAX];
    RegisterInfo regs_info[XLNX_PMX_EFUSE_CTRL_R_MAX];
} XlnxPmxEFuseCtrl;

typedef struct XlnxPmxEFuseCache {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    XlnxEFuse *efuse;
} XlnxPmxEFuseCache;

/**
 * Retrieve, from the @efuse storage, the 32-bit word that would be
 * visible at byte-offset (@bit / 8) through the XLNX_PMX_EFUSE_CACHE
 * device.
 *
 * For example, CRC for all data in the @efuse storage will be
 * returned when given @bit == (8 * 0x23c + 1).
 *
 * If @denied is not NULL, it is set to true if all bits of the requested
 * word is write-only; it is set to false if at least 1 bit is readable.
 *
 * Return: 0 if @denied is set (or would have been set) to true.
 */
static inline uint32_t xlnx_pmx_efuse_read_row(XlnxEFuse *efuse,
                                               uint32_t bit, bool *denied)
{
    return xlnx_efuse_get_u32(efuse, bit, denied);
}

#endif
