/*
 * QEMU model of the ARM MALI-400 (utgard) GPU.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * Hardware introduction can be found at:
 *  https://www.highperformancegraphics.org/previous/www_2010/media/Hot3D/HPG2010_Hot3D_ARM.pdf
 *  https://docs.xilinx.com/r/en-US/ug1085-zynq-ultrascale-trm/Graphics-Processing-Unit
 *  https://linux-sunxi.org/Mali
 *
 * Info for this model is based on the following FOSS projects:
 *  https://elixir.bootlin.com/linux/v6.7/source/drivers/gpu/drm/lima
 *  https://developer.arm.com/downloads/-/mali-drivers/utgard-kernel
 *  https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/drivers/lima.rst?ref_type=heads
 *
 * ARM has also published a video training series on Mali GPU:
 *  https://www.youtube.com/playlist?list=PLKjl7IFAwc4QUTejaX2vpIwXstbgf8Ik7
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/display/arm-mali400.h"

#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

/*
 * Register details are:
 *
 * -- First, derived from:
 *      https://elixir.bootlin.com/linux/latest/source/drivers/gpu/drm/lima/lima_regs.h
 *        (mostly lima_regs.h, lima_drm.h, lima_gp.c, lima_pp.c)
 *      https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/docs/drivers/lima.rst?ref_type=heads
 *        (mostly lima_gpu.h)
 *    Whose conventions for names of registers and fields are used here.
 *
 * -- Then, supplemented from:
 *      https://developer.arm.com/downloads/-/mali-drivers/utgard-kernel
 *    In particular:
 *      mali_utgard.h
 *      mali_l2_cache.c
 *      mali_pmu.h
 *      mali_mmu.h and mali_mmu.c
 *      mali_gp_regs.h
 *      mali_200_regs.h
 *
 * -- Finally, with missing pieces gathered from:
 *      https://docs.xilinx.com/r/en-US/ug1087-zynq-ultrascale-registers/GPU-Module
 *
 * MALI-400 contains:
 *  L2_CACHE: 1
 *  PMU: 1
 *  GP: 1 -- each geometry processor contains:
 *    _MMU:  1 -- same as a PP_MMU
 *    _CORE: 1 -- processor-level control / status / perf-counters
 *  PP: 1, 2, 3, or 4 -- each pixel processor contains:
 *    _MMU:  1 -- same as GP_MMU
 *    _CORE: 1 -- processor-level control / status / perf-counters
 *    _REND: 1 -- tile render
 *    _WB:   3 -- control / status of write-back buffers for rendered tiles
 *
 * Each of all above sub-components has its own address sub-range.
 *
 * PMU, *_MMU, and *_CORE have their own IRQ output, which can be combined
 * or individualized in any way, depending on the hardware design.
 */
enum {
    MALI400_OFFSET_GP_CORE  = 0x00000,
    MALI400_OFFSET_L2C      = 0x01000,
    MALI400_OFFSET_PMU      = 0x02000,
    MALI400_OFFSET_GP_MMU   = 0x03000,
    MALI400_OFFSET_PP0_MMU  = 0x04000,
    MALI400_OFFSET_PP1_MMU  = 0x05000,
    MALI400_OFFSET_PP2_MMU  = 0x06000,
    MALI400_OFFSET_PP3_MMU  = 0x07000,

    MALI400_OFFSET_PP0_REND = 0x08000,
    MALI400_OFFSET_PP0_WB0  = 0x08100,
    MALI400_OFFSET_PP0_WB1  = 0x08200,
    MALI400_OFFSET_PP0_WB2  = 0x08300,
    MALI400_OFFSET_PP0_CORE = 0x09000,

    MALI400_OFFSET_PP1_REND = 0x0A000,
    MALI400_OFFSET_PP1_WB0  = 0x0A100,
    MALI400_OFFSET_PP1_WB1  = 0x0A200,
    MALI400_OFFSET_PP1_WB2  = 0x0A300,
    MALI400_OFFSET_PP1_CORE = 0x0B000,

    MALI400_OFFSET_PP2_REND = 0x0C000,
    MALI400_OFFSET_PP2_WB0  = 0x0C100,
    MALI400_OFFSET_PP2_WB1  = 0x0C200,
    MALI400_OFFSET_PP2_WB2  = 0x0C300,
    MALI400_OFFSET_PP2_CORE = 0x0D000,

    MALI400_OFFSET_PP3_REND = 0x0E000,
    MALI400_OFFSET_PP3_WB0  = 0x0E100,
    MALI400_OFFSET_PP3_WB1  = 0x0E200,
    MALI400_OFFSET_PP3_WB2  = 0x0E300,
    MALI400_OFFSET_PP3_CORE = 0x0F000,

    /* Assign IDs to each irq source */
    MALI400_IRQ_PP0 = 0,
    MALI400_IRQ_PP1,
    MALI400_IRQ_PP2,
    MALI400_IRQ_PP3,
    MALI400_IRQ_PP0_MMU,
    MALI400_IRQ_PP1_MMU,
    MALI400_IRQ_PP2_MMU,
    MALI400_IRQ_PP3_MMU,
    MALI400_IRQ_GP,
    MALI400_IRQ_GP_MMU,
    MALI400_IRQ_PMU,
    MALI400_IRQ_TOTAL,
    MALI400_IRQ_BAD,
};

/* Make sure register storage in device struct and offsets are consistent */
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.gp.core)
                  > (MALI400_OFFSET_L2C - MALI400_OFFSET_GP_CORE));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.l2c)
                  > (MALI400_OFFSET_PMU - MALI400_OFFSET_L2C));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pmu)
                  > (MALI400_OFFSET_GP_MMU - MALI400_OFFSET_PMU));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.gp.mmu)
                  > (MALI400_OFFSET_PP0_MMU - MALI400_OFFSET_GP_MMU));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pp[0].mmu)
                  > (MALI400_OFFSET_PP1_MMU - MALI400_OFFSET_PP0_MMU));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pp[3].mmu)
                  > (MALI400_OFFSET_PP0_REND - MALI400_OFFSET_PP3_MMU));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pp[0].rend)
                  > (MALI400_OFFSET_PP0_WB0 - MALI400_OFFSET_PP0_REND));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pp[0].wb[0])
                  > (MALI400_OFFSET_PP0_WB1 - MALI400_OFFSET_PP0_WB0));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pp[0].wb[2])
                  > (MALI400_OFFSET_PP0_CORE - MALI400_OFFSET_PP0_WB2));
QEMU_BUILD_BUG_ON(sizeof_field(ArmMali400, regs.pp[0].core)
                  > (MALI400_OFFSET_PP1_REND - MALI400_OFFSET_PP0_CORE));

/* L2-cache sub-part */
REG32(L2_CACHE_VERSION, 0x0000)
    enum {
        L2_CACHE_VERSION_MALI400 = (0xcac2 << 16) | 0, /* ug1087 */
    };
REG32(L2_CACHE_SIZE, 0x0004)
REG32(L2_CACHE_STATUS, 0x0008)
    FIELD(L2_CACHE_STATUS, COMMAND_BUSY, 0, 1)
    FIELD(L2_CACHE_STATUS, DATA_BUSY, 1, 1)
REG32(L2_CACHE_COMMAND, 0x0010)
    FIELD(L2_CACHE_COMMAND, CLEAR_ALL, 0, 1)
REG32(L2_CACHE_CLEAR_PAGE, 0x0014)
REG32(L2_CACHE_MAX_READS, 0x0018)
REG32(L2_CACHE_ENABLE, 0x001c)
    FIELD(L2_CACHE_ENABLE, ACCESS, 0, 1)
    FIELD(L2_CACHE_ENABLE, READ_ALLOCATE, 1, 1)
REG32(L2_CACHE_PERFCNT_SRC0, 0x0020)
REG32(L2_CACHE_PERFCNT_VAL0, 0x0024)
REG32(L2_CACHE_PERFCNT_SRC1, 0x0028)
REG32(L2_CACHE_PERFCNT_VAL1, 0x002c)

/* PMU sub-part */
REG32(PMU_POWER_UP, 0x0000)
REG32(PMU_POWER_DOWN, 0x0004)
    SHARED_FIELD(PMU_POWER_GP0, 0, 1)
    SHARED_FIELD(PMU_POWER_L2C, 1, 1)
    SHARED_FIELD(PMU_POWER_PP0, 2, 1)
    SHARED_FIELD(PMU_POWER_PP1, 3, 1)
    SHARED_FIELD(PMU_POWER_PP2, 4, 1)
    SHARED_FIELD(PMU_POWER_PP3, 5, 1)
REG32(PMU_STATUS, 0x0008)
REG32(PMU_INT_MASK, 0x000c)
REG32(PMU_INT_RAWSTAT, 0x0010)
REG32(PMU_INT_CLEAR, 0x0018)
    SHARED_FIELD(PMU_IRQ, 0, 1)
REG32(PMU_SW_DELAY, 0x001c)

/* MMU sub-part, same for GP and PP */
REG32(MMU_DTE_ADDR, 0x0000)
REG32(MMU_STATUS, 0x0004)
    FIELD(MMU_STATUS, PAGING_ENABLED, 0, 1)
    FIELD(MMU_STATUS, PAGE_FAULT_ACTIVE, 1, 1)
    FIELD(MMU_STATUS, STALL_ACTIVE, 2, 1)
    FIELD(MMU_STATUS, IDLE, 3, 1)
    FIELD(MMU_STATUS, REPLAY_BUFFER_EMPTY, 4, 1)
    FIELD(MMU_STATUS, PAGE_FAULT_IS_WRITE, 5, 1)
    FIELD(MMU_STATUS, STALL_NOT_ACTIVE, 31, 1)
REG32(MMU_COMMAND, 0x0008)
    enum { /* lima_regs.h */
        MMU_COMMAND_ENABLE_PAGING   = 0x00,
        MMU_COMMAND_DISABLE_PAGING  = 0x01,
        MMU_COMMAND_ENABLE_STALL    = 0x02,
        MMU_COMMAND_DISABLE_STALL   = 0x03,
        MMU_COMMAND_ZAP_CACHE       = 0x04,
        MMU_COMMAND_PAGE_FAULT_DONE = 0x05,
        MMU_COMMAND_HARD_RESET      = 0x06,
    };
REG32(MMU_PAGE_FAULT_ADDR, 0x000c)
REG32(MMU_ZAP_ONE_LINE, 0x0010)
REG32(MMU_INT_RAWSTAT, 0x0014)
REG32(MMU_INT_CLEAR, 0x0018)
REG32(MMU_INT_MASK, 0x001c)
REG32(MMU_INT_STATUS, 0x0020)
    SHARED_FIELD(MMU_IRQ_PAGE_FAULT, 0, 1)
    SHARED_FIELD(MMU_IRQ_READ_BUS_ERROR, 1, 1)

/*
 * GP-core sub-part
 *
 * GP-job: first 6 registers; see lima_drm.h, LIMA_GP_FRAME_REG_NUM
 */
REG32(GP_VSCL_START_ADDR, 0x0000)
REG32(GP_VSCL_END_ADDR, 0x0004)
REG32(GP_PLBUCL_START_ADDR, 0x0008)
REG32(GP_PLBUCL_END_ADDR, 0x000c)
REG32(GP_PLBU_ALLOC_START_ADDR, 0x0010)
REG32(GP_PLBU_ALLOC_END_ADDR, 0x0014)

REG32(GP_CMD, 0x0020)
    FIELD(GP_CMD, START_VS, 0, 1)
    FIELD(GP_CMD, START_PLBU, 1, 1)
    FIELD(GP_CMD, UPDATE_PLBU_ALLOC, 4, 1)
    FIELD(GP_CMD, RESET, 5, 1)
    FIELD(GP_CMD, FORCE_HANG, 6, 1)
    FIELD(GP_CMD, STOP_BUS, 9, 1)
    FIELD(GP_CMD, SOFT_RESET, 10, 1)
REG32(GP_INT_RAWSTAT, 0x0024)
REG32(GP_INT_CLEAR, 0x0028)
REG32(GP_INT_MASK, 0x002c)
REG32(GP_INT_STAT, 0x0030)
    SHARED_FIELD(GP_IRQ_VS_END_CMD_LST, 0, 1)
    SHARED_FIELD(GP_IRQ_PLBU_END_CMD_LST, 1, 1)
    SHARED_FIELD(GP_IRQ_PLBU_OUT_OF_MEM, 2, 1)
    SHARED_FIELD(GP_IRQ_VS_SEM_IRQ, 3, 1)
    SHARED_FIELD(GP_IRQ_PLBU_SEM_IRQ, 4, 1)
    SHARED_FIELD(GP_IRQ_HANG, 5, 1)
    SHARED_FIELD(GP_IRQ_FORCE_HANG, 6, 1)
    SHARED_FIELD(GP_IRQ_PERF_CNT_0_LIMIT, 7, 1)
    SHARED_FIELD(GP_IRQ_PERF_CNT_1_LIMIT, 8, 1)
    SHARED_FIELD(GP_IRQ_WRITE_BOUND_ERR, 9, 1)
    SHARED_FIELD(GP_IRQ_SYNC_ERROR, 10, 1)
    SHARED_FIELD(GP_IRQ_AXI_BUS_ERROR, 11, 1)
    SHARED_FIELD(GP_IRQ_AXI_BUS_STOPPED, 12, 1)
    SHARED_FIELD(GP_IRQ_VS_INVALID_CMD, 13, 1)
    SHARED_FIELD(GP_IRQ_PLB_INVALID_CMD, 14, 1)
    SHARED_FIELD(GP_IRQ_RESET_COMPLETED, 19, 1)
    SHARED_FIELD(GP_IRQ_SEMAPHORE_UNDERFLOW, 20, 1)
    SHARED_FIELD(GP_IRQ_SEMAPHORE_OVERFLOW, 21, 1)
    SHARED_FIELD(GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS, 22, 1)
REG32(GP_WRITE_BOUND_LOW, 0x0034)
REG32(GP_WRITE_BOUND_HIGH, 0x0038)
REG32(GP_PERF_CNT_0_ENABLE, 0x003c)
REG32(GP_PERF_CNT_1_ENABLE, 0x0040)
REG32(GP_PERF_CNT_0_SRC, 0x0044)
REG32(GP_PERF_CNT_1_SRC, 0x0048)
REG32(GP_PERF_CNT_0_VALUE, 0x004c)
REG32(GP_PERF_CNT_1_VALUE, 0x0050)
REG32(GP_PERF_CNT_0_LIMIT, 0x0054)
REG32(GP_PERF_CNT_1_LIMIT, 0x0058)
REG32(GP_STATUS, 0x0068)
    FIELD(GP_STATUS, VS_ACTIVE, 1, 1)
    FIELD(GP_STATUS, BUS_STOPPED, 2, 1)
    FIELD(GP_STATUS, PLBU_ACTIVE, 3, 1)
    FIELD(GP_STATUS, BUS_ERROR, 6, 1)
    FIELD(GP_STATUS, WRITE_BOUND_ERR, 8, 1)
REG32(GP_VERSION, 0x006c)
    enum {
        GP_VERSION_MALI400 = (0x0b07 << 16) + 0x0101,
                              /* lima_gp.c, lima_gp_print_version */
    };
REG32(GP_VSCL_START_ADDR_READ, 0x0080)
REG32(GP_PLBCL_START_ADDR_READ, 0x0084)
REG32(GP_CONTR_AXI_BUS_ERROR_STAT, 0x0094)

/* PP-core sub-part */
REG32(PP_VERSION, 0x0000)
    enum {
        PP_VERSION_MALI400 = (0xcd07 << 16) + 0x0101,
                              /* lima_pp.c, lima_pp_print_version */
    };
REG32(PP_CURRENT_REND_LIST_ADDR, 0x0004)
REG32(PP_STATUS, 0x0008)
    FIELD(PP_STATUS, RENDERING_ACTIVE, 0, 1)
    FIELD(PP_STATUS, BUS_STOPPED, 4, 1)
REG32(PP_CTRL, 0x000c)
    FIELD(PP_CTRL, STOP_BUS, 0, 1)
    FIELD(PP_CTRL, FLUSH_CACHES, 3, 1)
    FIELD(PP_CTRL, FORCE_RESET, 5, 1)
    FIELD(PP_CTRL, START_RENDERING, 6, 1)
    FIELD(PP_CTRL, SOFT_RESET, 7, 1)
REG32(PP_INT_RAWSTAT, 0x0020)
REG32(PP_INT_CLEAR, 0x0024)
REG32(PP_INT_MASK, 0x0028)
REG32(PP_INT_STATUS, 0x002c)
    SHARED_FIELD(PP_IRQ_END_OF_FRAME, 0, 1)
    SHARED_FIELD(PP_IRQ_END_OF_TILE, 1, 1)
    SHARED_FIELD(PP_IRQ_HANG, 2, 1)
    SHARED_FIELD(PP_IRQ_FORCE_HANG, 3, 1)
    SHARED_FIELD(PP_IRQ_BUS_ERROR, 4, 1)
    SHARED_FIELD(PP_IRQ_BUS_STOP, 5, 1)
    SHARED_FIELD(PP_IRQ_CNT_0_LIMIT, 6, 1)
    SHARED_FIELD(PP_IRQ_CNT_1_LIMIT, 7, 1)
    SHARED_FIELD(PP_IRQ_WRITE_BOUNDARY_ERROR, 8, 1)
    SHARED_FIELD(PP_IRQ_INVALID_PLIST_COMMAND, 9, 1)
    SHARED_FIELD(PP_IRQ_CALL_STACK_UNDERFLOW, 10, 1)
    SHARED_FIELD(PP_IRQ_CALL_STACK_OVERFLOW, 11, 1)
    SHARED_FIELD(PP_IRQ_RESET_COMPLETED, 12, 1)
REG32(PP_WRITE_BOUNDARY_ENABLE, 0x0040)
REG32(PP_WRITE_BOUNDARY_LOW, 0x0044)
REG32(PP_WRITE_BOUNDARY_HIGH, 0x0048)
REG32(PP_WRITE_BOUNDARY_ADDR, 0x004C)
REG32(PP_BUS_ERROR_STATUS, 0x0050)
REG32(PP_PERF_CNT_0_ENABLE, 0x0080)
REG32(PP_PERF_CNT_0_SRC, 0x0084)
REG32(PP_PERF_CNT_0_LIMIT, 0x0088)
REG32(PP_PERF_CNT_0_VALUE, 0x008c)
REG32(PP_PERF_CNT_1_ENABLE, 0x00a0)
REG32(PP_PERF_CNT_1_SRC, 0x00a4)
REG32(PP_PERF_CNT_1_LIMIT, 0x00a8)
REG32(PP_PERF_CNT_1_VALUE, 0x00ac)
REG32(PP_PERFMON_CONTR, 0x00b0)
REG32(PP_PERFMON_BASE, 0x00b4)

/*
 * PP-render sub-part
 *
 * PP-job: 23 registers; see
 * -- lima_drm.h, LIMA_PP_FRAME_REG_NUM.
 * -- lima_gpu.h, struct lima_pp_frame_reg.
 */
REG32(PP_FRAME, 0x0000)
REG32(PP_RSW, 0x0004)
REG32(PP_VERTEX, 0x0008)
REG32(PP_REND_FLAGS, 0x000c)
    FIELD(PP_REND_FLAGS, FP_TILEBUF_ENABLE, 0, 1)
    FIELD(PP_REND_FLAGS, EARLYZ_ENABLE, 1, 1)
    FIELD(PP_REND_FLAGS, EARLYZ_DISABLE2, 4, 1)
    FIELD(PP_REND_FLAGS, EARLYZ_DISABLE1, 3, 1)
    FIELD(PP_REND_FLAGS, ORIGIN_LOWER_LEFT, 5, 1)
    FIELD(PP_REND_FLAGS, SUMMATE_QUAD_COVER, 6, 1)
REG32(PP_CLEAR_VALUE_DEPTH, 0x0010)
REG32(PP_CLEAR_VALUE_STENCIL, 0x0014)
REG32(PP_CLEAR_VALUE_COLOR, 0x0018)
REG32(PP_CLEAR_VALUE_COLOR_1, 0x001c)
REG32(PP_CLEAR_VALUE_COLOR_2, 0x0020)
REG32(PP_CLEAR_VALUE_COLOR_3, 0x0024)
    SHARED_FIELD(PP_CLEAR_VALUE_COLOR_RED,    0, 8)
    SHARED_FIELD(PP_CLEAR_VALUE_COLOR_GREEN,  8, 8)
    SHARED_FIELD(PP_CLEAR_VALUE_COLOR_BLUE,  16, 8)
    SHARED_FIELD(PP_CLEAR_VALUE_COLOR_ALPHA, 24, 8)
REG32(PP_WIDTH, 0x0028)
    FIELD(PP_WIDTH, BOX_RIGHT, 0, 14)
    FIELD(PP_WIDTH, BOX_LEFT, 16, 4)
REG32(PP_HEIGHT, 0x002c)
    FIELD(PP_HEIGHT, BOX_BOTTOM, 0, 14)
REG32(PP_STACK, 0x0030)
REG32(PP_STACK_SIZE, 0x0034)
    FIELD(PP_STACK_SIZE, SIZE, 0, 16)
    FIELD(PP_STACK_SIZE, OFFSET, 16, 16)
REG32(PP_ORIGIN_OFFSET_X, 0x0040)
REG32(PP_ORIGIN_OFFSET_Y, 0x0044)
REG32(PP_SUBPIXEL_SPECIFIER, 0x0048)
REG32(PP_ONSCREEN, 0x004c)
REG32(PP_BLOCKING, 0x0050)
    FIELD(PP_BLOCKING, SHIFT_W, 0, 6)
    FIELD(PP_BLOCKING, SHIFT_H, 16, 6)
    FIELD(PP_BLOCKING, SHIFT_MIN, 28, 2)
REG32(PP_SCALING, 0x0054)
    FIELD(PP_SCALING, POINT_AND_LINE_SCALE_ENABLE, 0, 1)
    FIELD(PP_SCALING, DITHERING_SCALE_ENABLE, 1, 1)
    FIELD(PP_SCALING, FRAGCOORD_SCALE_ENABLE, 2, 1)
    FIELD(PP_SCALING, DERIVATIVE_SCALE_ENABLE, 3, 1)
    FIELD(PP_SCALING, FLIP_POINT_SPRITES, 8, 1)
    FIELD(PP_SCALING, FLIP_DITHERING_MATRIX, 9, 1)
    FIELD(PP_SCALING, FLIP_FRAGCOORD, 10, 1)
    FIELD(PP_SCALING, FLIP_DERIVATIVE_Y, 11, 1)
    FIELD(PP_SCALING, SCALE_X, 16, 3)
    FIELD(PP_SCALING, SCALE_Y, 20, 3)
REG32(PP_CHANNEL_LAYOUT, 0x0058)
    FIELD(PP_CHANNEL_LAYOUT, RED,    0, 4)
    FIELD(PP_CHANNEL_LAYOUT, GREEN,  4, 4)
    FIELD(PP_CHANNEL_LAYOUT, BLUE,   8, 4)
    FIELD(PP_CHANNEL_LAYOUT, ALPHA, 12, 4)

/*
 * PP-wb sub-part
 *
 * PP-job: 12 registers; see:
 * -- lima_drm.h, LIMA_PP_WB_REG_NUM
 * -- lima_gpu.h, struct lima_pp_wb_reg.
 */
REG32(PP_WB_TYPE, 0x0000)
REG32(PP_WB_ADDRESS, 0x0004)
REG32(PP_WB_PIXEL_FORMAT, 0x0008)
REG32(PP_WB_DOWNSAMPLE_FACTOR, 0x000c)
    FIELD(PP_WB_DOWNSAMPLE_FACTOR, X, 8, 2)
    FIELD(PP_WB_DOWNSAMPLE_FACTOR, Y, 12, 3)
REG32(PP_WB_PIXEL_LAYOUT, 0x0010)
REG32(PP_WB_PITCH, 0x0014)
REG32(PP_WB_FLAGS, 0x0018)
    FIELD(PP_WB_FLAGS, DIRTY_BIT_ENABLE, 0, 1)
    FIELD(PP_WB_FLAGS, BOUNDING_BOX_ENABLE, 1, 1)
    FIELD(PP_WB_FLAGS, SWAP_RED_BLUE_ENABLE, 2, 1)
    FIELD(PP_WB_FLAGS, INV_COMPONENT_ORDER_ENABLE, 3, 1)
    FIELD(PP_WB_FLAGS, DITHER_ENABLE, 4, 1)
    FIELD(PP_WB_FLAGS, BIG_ENDIAN, 5, 1)
REG32(PP_WB_MRT_BITS, 0x001c)
REG32(PP_WB_MRT_PITCH, 0x0020)
REG32(PP_WB_UNUSED0, 0x0024)
REG32(PP_WB_UNUSED1, 0x0028)
REG32(PP_WB_UNUSED2, 0x002c)

#define MALI400_RESET_REGS(S, B) \
    mali400_reset_regs((S), &(S)->regs_info.B[0], ARRAY_SIZE((S)->regs_info.B))

static void mali400_reset_regs(ArmMali400 *s, RegisterInfo *ri_array, size_t n)
{
    size_t i;
    bool resetting = s->resetting;

    s->resetting = true;

    for (i = 0; i < n; i++) {
        register_reset(&ri_array[i]);
    }

    s->resetting = resetting;
}

static ArmMali400GP_Reg *mali400_gp_baseof(RegisterInfo *ri, ArmMali400 *s)
{
    void *dp = ri->data;
    void *gp;

    if (!s) {
        s = ARM_MALI400(ri->opaque);
    }

    gp = &s->regs.gp;
    if (dp >= gp) {
        size_t gn = (dp - gp) / sizeof(s->regs.gp);

        if (!gn) {
            return &s->regs.gp;
        }
    }

    return NULL;
}

static ArmMali400PP_Reg *mali400_pp_of(void *reg, ArmMali400 *s)
{
    void *pp;

    if (!reg) {
        return NULL;
    }

    pp = &s->regs.pp[0];
    if (reg >= pp) {
        size_t pn = (reg - pp) / sizeof(s->regs.pp[0]);

        if (pn < ARRAY_SIZE(s->regs.pp)) {
            return &s->regs.pp[pn];
        }
    }

    return NULL;
}

static ArmMali400PP_Reg *mali400_pp_baseof(RegisterInfo *ri, ArmMali400 *s)
{
    if (!s) {
        s = ARM_MALI400(ri->opaque);
    }

    return mali400_pp_of(ri->data, s);
}

static unsigned mali400_pp_index(ArmMali400 *s, ArmMali400PP_Reg *pp)
{
    unsigned pn;

    g_assert(pp >= s->regs.pp);

    pn = pp - s->regs.pp;
    g_assert(pn < ARRAY_SIZE(s->regs.pp));

    return pn;
}

static unsigned mali400_pp_irq(ArmMali400 *s, ArmMali400PP_Reg *pp)
{
    unsigned pn = mali400_pp_index(s, pp);

    static const unsigned irq_id[] = {
        [0] = MALI400_IRQ_PP0,
        [1] = MALI400_IRQ_PP1,
        [2] = MALI400_IRQ_PP2,
        [3] = MALI400_IRQ_PP3,
    };

    g_assert(pn < ARRAY_SIZE(irq_id));
    return irq_id[pn];
}

static unsigned mali400_pp_mmu_irq(ArmMali400 *s, ArmMali400PP_Reg *pp)
{
    unsigned pn = mali400_pp_index(s, pp);

    static const unsigned irq_id[] = {
        [0] = MALI400_IRQ_PP0_MMU,
        [1] = MALI400_IRQ_PP1_MMU,
        [2] = MALI400_IRQ_PP2_MMU,
        [3] = MALI400_IRQ_PP3_MMU,
    };

    g_assert(pn < ARRAY_SIZE(irq_id));
    return irq_id[pn];
}

static uint32_t *mali400_mmu_baseof(RegisterInfo *ri, ArmMali400 *s)
{
    ArmMali400GP_Reg *gp;
    ArmMali400PP_Reg *pp;

    if (!s) {
        s = ARM_MALI400(ri->opaque);
    }

    gp = mali400_gp_baseof(ri, s);
    if (gp) {
        return s->regs.gp.mmu;
    }

    pp = mali400_pp_baseof(ri, s);
    if (pp) {
        unsigned pn = mali400_pp_index(s, pp);

        return s->regs.pp[pn].mmu;
    }

    return NULL;
}

static unsigned mali400_irq_src(RegisterInfo *ri, ArmMali400 *s)
{
    uint32_t *dp = ri->data;
    ArmMali400GP_Reg *gp;
    ArmMali400PP_Reg *pp;

    if (!s) {
        s = ARM_MALI400(ri->opaque);
    }

    if (s->regs.pmu <= dp && dp <= &s->regs.pmu[ARM_MALI400_PMU_R_MAX - 1]) {
        return MALI400_IRQ_PMU;
    }

    gp = mali400_gp_baseof(ri, s);
    if (gp) {
        if (dp >= s->regs.gp.core) {
            return MALI400_IRQ_GP;
        } else {
            return MALI400_IRQ_GP_MMU;
        }
    }

    pp = mali400_pp_baseof(ri, s);
    if (pp) {
        unsigned pn = mali400_pp_index(s, pp);

        if (dp >= s->regs.pp[pn].core) {
            return mali400_pp_irq(s, pp);
        } else {
            return mali400_pp_mmu_irq(s, pp);
        }
    }

    return MALI400_IRQ_BAD;
}

static void mali400_update_irq(ArmMali400 *s, unsigned id, uint32_t *r32)
{
    uint32_t *raw, *mask = NULL, *masked = NULL, *clr = NULL;
    uint32_t id_bit, pending;

    /* Block false irq from register reset */
    if (s->resetting) {
        return;
    }

    switch (id) {
    case MALI400_IRQ_PP0:
        raw = &s->regs.pp[0].core[R_PP_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP1:
        raw = &s->regs.pp[1].core[R_PP_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP2:
        raw = &s->regs.pp[2].core[R_PP_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP3:
        raw = &s->regs.pp[3].core[R_PP_INT_RAWSTAT];
        break;
    case MALI400_IRQ_GP:
        raw = &s->regs.gp.core[R_GP_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP0_MMU:
        raw = &s->regs.pp[0].mmu[R_MMU_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP1_MMU:
        raw = &s->regs.pp[1].mmu[R_MMU_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP2_MMU:
        raw = &s->regs.pp[2].mmu[R_MMU_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PP3_MMU:
        raw = &s->regs.pp[3].mmu[R_MMU_INT_RAWSTAT];
        break;
    case MALI400_IRQ_GP_MMU:
        raw = &s->regs.gp.mmu[R_MMU_INT_RAWSTAT];
        break;
    case MALI400_IRQ_PMU:
        clr = &s->regs.pmu[R_PMU_INT_CLEAR];
        raw = &s->regs.pmu[R_PMU_INT_RAWSTAT];
        mask = &s->regs.pmu[R_PMU_INT_MASK];
        break;
    default:
        return;
    }

    if (!mask) {
        /* Common format */
        clr = raw + (R_MMU_INT_CLEAR - R_MMU_INT_RAWSTAT);
        mask = raw + (R_MMU_INT_MASK - R_MMU_INT_RAWSTAT);
        masked = raw + (R_MMU_INT_STATUS - R_MMU_INT_RAWSTAT);
    }

    if (clr == r32) {
        *raw &= ~(*clr);
        *clr = 0;
    }

    /* Need to propagate masked events into *masked */
    pending = *raw & *mask;
    if (masked) {
        *masked = pending;
    }

    id_bit = (uint32_t)1 << id;
    if (pending) {
        s->irq_pending |= id_bit;
    } else {
        s->irq_pending &= ~id_bit;
    }

    qemu_set_irq(s->irq, !!s->irq_pending);
}

static void mali400_int_reg_postw(RegisterInfo *reg, uint64_t val64)
{
    ArmMali400 *s = ARM_MALI400(reg->opaque);
    unsigned src = mali400_irq_src(reg, s);

    mali400_update_irq(s, src, reg->data);
}

static void mali400_mmu_irq_update(ArmMali400 *s, uint32_t *base)
{
    unsigned src;

    if (base == s->regs.gp.mmu) {
        src = MALI400_IRQ_GP_MMU;
    } else {
        ArmMali400PP_Reg *pp = mali400_pp_of(base, s);

        if (!pp) {
            return;
        }

        src = mali400_pp_mmu_irq(s, pp);
    }

    mali400_update_irq(s, src, NULL);
}

static void mali400_mmu_reset(ArmMali400 *s, uint32_t *base)
{
    ArmMali400PP_Reg *pp;
    unsigned pn;

    if (base == s->regs.gp.mmu) {
        MALI400_RESET_REGS(s, gp.mmu);
        return;
    }

    pp = mali400_pp_of(base, s);
    if (!pp) {
        return;
    }

    pn = mali400_pp_index(s, pp);
    MALI400_RESET_REGS(s, pp[pn].mmu);

    mali400_mmu_irq_update(s, base);
}

static void mali400_mmu_enable_paging(ArmMali400 *s, uint32_t *base, bool yes)
{
    ARRAY_FIELD_DP32(base, MMU_STATUS, PAGING_ENABLED, yes);
}

static void mali400_mmu_stall(ArmMali400 *s, uint32_t *base, bool yes)
{
    ARRAY_FIELD_DP32(base, MMU_STATUS, STALL_ACTIVE, yes);
    ARRAY_FIELD_DP32(base, MMU_STATUS, STALL_NOT_ACTIVE, !yes);
}

static void mali400_mmu_command_postw(RegisterInfo *reg, uint64_t val64)
{
    ArmMali400 *s = ARM_MALI400(reg->opaque);
    uint32_t *base = mali400_mmu_baseof(reg, s);
    unsigned cmd = val64;

    switch (cmd) {
    case MMU_COMMAND_HARD_RESET:
        mali400_mmu_reset(s, base);
        break;
    case MMU_COMMAND_ENABLE_PAGING:
        mali400_mmu_enable_paging(s, base, true);
        break;
    case MMU_COMMAND_DISABLE_PAGING:
        mali400_mmu_enable_paging(s, base, false);
        break;
    case MMU_COMMAND_ENABLE_STALL:
        mali400_mmu_stall(s, base, true);
        break;
    case MMU_COMMAND_DISABLE_STALL:
        mali400_mmu_stall(s, base, false);
        break;
    }
}

#define mali400_gp_irq_raise(S, FLD) do { \
        SHARED_ARRAY_FIELD_DP32((S)->regs.gp.core, R_GP_INT_RAWSTAT, FLD, 1); \
        mali400_update_irq(s, MALI400_IRQ_GP, NULL);                    \
    } while (0)

static void mali400_gp_reset(ArmMali400 *s)
{
    MALI400_RESET_REGS(s, gp.core);

    mali400_gp_irq_raise(s, GP_IRQ_RESET_COMPLETED);
}

static void mali400_gp_cmd_postw(RegisterInfo *reg, uint64_t val64)
{
    ArmMali400 *s = ARM_MALI400(reg->opaque);

    if (FIELD_EX32(val64, GP_CMD, SOFT_RESET) ||
        FIELD_EX32(val64, GP_CMD, RESET)) {
        mali400_gp_reset(s);
    }
}

static void mali400_pp_irq_update(ArmMali400 *s, ArmMali400PP_Reg *pp)
{
    unsigned src = mali400_pp_irq(s, pp);

    mali400_update_irq(s, src, NULL);
}

#define mali400_pp_irq_raise(S, PP, FLD) do { \
        SHARED_ARRAY_FIELD_DP32((PP)->core, R_PP_INT_RAWSTAT, FLD, 1);  \
        mali400_pp_irq_update((S), (PP));                               \
    } while (0)

static void mali400_pp_reset(ArmMali400 *s, ArmMali400PP_Reg *pp)
{
    unsigned pn = mali400_pp_index(s, pp);
    unsigned i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info.pp[pn].wb); i++) {
        MALI400_RESET_REGS(s, pp[pn].wb[i]);
    }
    MALI400_RESET_REGS(s, pp[pn].rend);
    MALI400_RESET_REGS(s, pp[pn].core);

    mali400_pp_irq_raise(s, pp, PP_IRQ_RESET_COMPLETED);
}

static void mali400_pp_core_ctrl_postw(RegisterInfo *reg, uint64_t val64)
{
    ArmMali400 *s = ARM_MALI400(reg->opaque);
    ArmMali400PP_Reg *pp = mali400_pp_baseof(reg, s);

    if (!pp) {
        return;
    }

    if (FIELD_EX32(val64, PP_CTRL, SOFT_RESET) ||
        FIELD_EX32(val64, PP_CTRL, FORCE_RESET)) {
        mali400_pp_reset(s, pp);
    }
}

static const RegisterAccessInfo mali400_regs_access_l2c[] = {
    {   .name = "L2_CACHE_VERSION",  .addr = A_L2_CACHE_VERSION,
        .reset = L2_CACHE_VERSION_MALI400,
        .ro = 0xffffffff,
    },{ .name = "L2_CACHE_SIZE",  .addr = A_L2_CACHE_SIZE,
        .ro = 0xffffffff,
    },{ .name = "L2_CACHE_STATUS",  .addr = A_L2_CACHE_STATUS,
        .rsvd = 0xfffffffc,
        .ro = 0xffffffff,
    },{ .name = "L2_CACHE_COMMAND",  .addr = A_L2_CACHE_COMMAND,
        .rsvd = 0xfffffff8,
    },{ .name = "L2_CACHE_CLEAR_PAGE",  .addr = A_L2_CACHE_CLEAR_PAGE,
    },{ .name = "L2_CACHE_MAX_READS",  .addr = A_L2_CACHE_MAX_READS,
        .reset = 0x1c,
        .rsvd = 0xffffffe0,
    },{ .name = "L2_CACHE_ENABLE",  .addr = A_L2_CACHE_ENABLE,
        .rsvd = 0xfffffffc,
    },{ .name = "L2_CACHE_PERFCNT_SRC0",  .addr = A_L2_CACHE_PERFCNT_SRC0,
        .rsvd = 0xffffff80,
    },{ .name = "L2_CACHE_PERFCNT_VAL0",  .addr = A_L2_CACHE_PERFCNT_VAL0,
    },{ .name = "L2_CACHE_PERFCNT_SRC1",  .addr = A_L2_CACHE_PERFCNT_SRC1,
        .rsvd = 0xffffff80,
    },{ .name = "L2_CACHE_PERFCNT_VAL1",  .addr = A_L2_CACHE_PERFCNT_VAL1,
    },
};

static const RegisterAccessInfo mali400_regs_access_pmu[] = {
    {   .name = "PMU_POWER_UP",  .addr = A_PMU_POWER_UP,
        .rsvd = 0xffffffc0,
    },{ .name = "PMU_POWER_DOWN",  .addr = A_PMU_POWER_DOWN,
        .rsvd = 0xffffffc0,
    },{ .name = "PMU_STATUS",  .addr = A_PMU_STATUS,
        .rsvd = 0xffffffc0,
        .ro = 0xffffffff,
    },{ .name = "PMU_INT_MASK",  .addr = A_PMU_INT_MASK,
        .reset = 0x1,
        .post_write = mali400_int_reg_postw,
    },{ .name = "PMU_INT_RAWSTAT",  .addr = A_PMU_INT_RAWSTAT,
        .post_write = mali400_int_reg_postw,
    },{ .name = "PMU_INT_CLEAR",  .addr = A_PMU_INT_CLEAR,
        .post_write = mali400_int_reg_postw,
    },{ .name = "PMU_SW_DELAY",  .addr = A_PMU_SW_DELAY,
        .reset = 0xff,
        .rsvd = 0xffff0000,
    },
};

static const RegisterAccessInfo mali400_regs_access_mmu[] = {
    {   .name = "MMU_DTE_ADDR",  .addr = A_MMU_DTE_ADDR,
    },{ .name = "MMU_STATUS",  .addr = A_MMU_STATUS,
        .reset = 0x18,
        .rsvd = 0xfffff800,
        .ro = 0xffffffff,
    },{ .name = "MMU_COMMAND",  .addr = A_MMU_COMMAND,
        .rsvd = 0xfffffff8,
        .post_write = mali400_mmu_command_postw,
    },{ .name = "MMU_PAGE_FAULT_ADDR",  .addr = A_MMU_PAGE_FAULT_ADDR,
        .ro = 0xffffffff,
    },{ .name = "MMU_ZAP_ONE_LINE",  .addr = A_MMU_ZAP_ONE_LINE,
    },{ .name = "MMU_INT_RAWSTAT",  .addr = A_MMU_INT_RAWSTAT,
        .rsvd = 0xfffffffc,
        .post_write = mali400_int_reg_postw,
    },{ .name = "MMU_INT_CLEAR",  .addr = A_MMU_INT_CLEAR,
        .rsvd = 0xfffffffc,
        .post_write = mali400_int_reg_postw,
    },{ .name = "MMU_INT_MASK",  .addr = A_MMU_INT_MASK,
        .rsvd = 0xfffffffc,
        .post_write = mali400_int_reg_postw,
    },{ .name = "MMU_INT_STATUS",  .addr = A_MMU_INT_STATUS,
        .rsvd = 0xfffffffc,
        .ro = 0xffffffff,
    },
};

static const RegisterAccessInfo mali400_regs_access_gp_core[] = {
    {   .name = "GP_VSCL_START_ADDR",  .addr = A_GP_VSCL_START_ADDR,
        .rsvd = 0x7,
    },{ .name = "GP_VSCL_END_ADDR",  .addr = A_GP_VSCL_END_ADDR,
        .rsvd = 0x7,
    },{ .name = "GP_PLBUCL_START_ADDR",  .addr = A_GP_PLBUCL_START_ADDR,
        .rsvd = 0x7,
    },{ .name = "GP_PLBUCL_END_ADDR",  .addr = A_GP_PLBUCL_END_ADDR,
        .rsvd = 0x7,
    },{ .name = "GP_PLBU_ALLOC_START_ADDR",  .addr = A_GP_PLBU_ALLOC_START_ADDR,
        .rsvd = 0x7f,
    },{ .name = "GP_PLBU_ALLOC_END_ADDR",  .addr = A_GP_PLBU_ALLOC_END_ADDR,
        .rsvd = 0x7f,
    },{ .name = "GP_CMD",  .addr = A_GP_CMD,
        .rsvd = 0xfffff08c,
        .post_write = mali400_gp_cmd_postw,
    },{ .name = "GP_INT_RAWSTAT",  .addr = A_GP_INT_RAWSTAT,
        .reset = 0x80000,
        .rsvd = 0xff878400,
        .post_write = mali400_int_reg_postw,
    },{ .name = "GP_INT_CLEAR",  .addr = A_GP_INT_CLEAR,
        .reset = 0x707bff,
        .rsvd = 0xff878400,
        .post_write = mali400_int_reg_postw,
    },{ .name = "GP_INT_MASK",  .addr = A_GP_INT_MASK,
        .rsvd = 0xff800400,
        .post_write = mali400_int_reg_postw,
    },{ .name = "GP_INT_STAT",  .addr = A_GP_INT_STAT,
        .reset = 0x80000,
        .rsvd = 0xff878400,
        .ro = 0xffffffff,
    },{ .name = "GP_WRITE_BOUND_LOW",  .addr = A_GP_WRITE_BOUND_LOW,
        .rsvd = 0xff,
    },{ .name = "GP_WRITE_BOUND_HIGH",  .addr = A_GP_WRITE_BOUND_HIGH,
        .rsvd = 0xff,
    },{ .name = "GP_PERF_CNT_0_ENABLE",  .addr = A_GP_PERF_CNT_0_ENABLE,
        .rsvd = 0xfffffffe,
    },{ .name = "GP_PERF_CNT_1_ENABLE",  .addr = A_GP_PERF_CNT_1_ENABLE,
        .rsvd = 0xfffffffe,
    },{ .name = "GP_PERF_CNT_0_SRC",  .addr = A_GP_PERF_CNT_0_SRC,
        .rsvd = 0xfffffffe,
    },{ .name = "GP_PERF_CNT_1_SRC",  .addr = A_GP_PERF_CNT_1_SRC,
        .rsvd = 0xfffffffe,
    },{ .name = "GP_PERF_CNT_0_VALUE",  .addr = A_GP_PERF_CNT_0_VALUE,
        .ro = 0xffffffff,
    },{ .name = "GP_PERF_CNT_1_VALUE",  .addr = A_GP_PERF_CNT_1_VALUE,
        .ro = 0xffffffff,
    },{ .name = "GP_PERF_CNT_0_LIMIT",  .addr = A_GP_PERF_CNT_0_LIMIT,
    },{ .name = "GP_PERF_CNT_1_LIMIT",  .addr = A_GP_PERF_CNT_1_LIMIT,
    },{ .name = "GP_STATUS",  .addr = A_GP_STATUS,
        .rsvd = 0xfffffc10,
        .ro = 0xffffffff,
    },{ .name = "GP_VERSION",  .addr = A_GP_VERSION,
        .reset = GP_VERSION_MALI400,
        .ro = 0xffffffff,
    },{ .name = "GP_VSCL_START_ADDR_READ",  .addr = A_GP_VSCL_START_ADDR_READ,
        .rsvd = 0x7,
        .ro = 0xffffffff,
    },{ .name = "GP_PLBCL_START_ADDR_READ",  .addr = A_GP_PLBCL_START_ADDR_READ,
        .rsvd = 0x7,
        .ro = 0xffffffff,
    },{ .name = "GP_CONTR_AXI_BUS_ERROR_STAT",
        .addr = A_GP_CONTR_AXI_BUS_ERROR_STAT,
        .rsvd = 0xfffffc00,
        .ro = 0xffffffff,
    },
};

static const RegisterAccessInfo mali400_regs_access_pp_core[] = {
    {   .name = "PP_VERSION",  .addr = A_PP_VERSION,
        .reset = PP_VERSION_MALI400,
        .ro = 0xffffffff,
    },{ .name = "PP_CURRENT_REND_LIST_ADDR",
        .addr = A_PP_CURRENT_REND_LIST_ADDR,
        .rsvd = 0x1f,
    },{ .name = "PP_STATUS",  .addr = A_PP_STATUS,
        .rsvd = 0xffffff00,
    },{ .name = "PP_CTRL",  .addr = A_PP_CTRL,
        .post_write = mali400_pp_core_ctrl_postw,
    },{ .name = "PP_INT_RAWSTAT",  .addr = A_PP_INT_RAWSTAT,
        .reset = 0x1000,
        .rsvd = 0xffffe000,
        .post_write = mali400_int_reg_postw,
    },{ .name = "PP_INT_CLEAR",  .addr = A_PP_INT_CLEAR,
        .rsvd = 0xffffe000,
        .post_write = mali400_int_reg_postw,
    },{ .name = "PP_INT_MASK",  .addr = A_PP_INT_MASK,
        .reset = 0xfff,
        .rsvd = 0xffffe000,
        .post_write = mali400_int_reg_postw,
    },{ .name = "PP_INT_STATUS",  .addr = A_PP_INT_STATUS,
        .reset = 0x1000,
    },{ .name = "PP_WRITE_BOUNDARY_ENABLE", .addr = A_PP_WRITE_BOUNDARY_ENABLE,
        .rsvd = 0xfffffffe,
    },{ .name = "PP_WRITE_BOUNDARY_LOW",  .addr = A_PP_WRITE_BOUNDARY_LOW,
        .rsvd = 0xff,
    },{ .name = "PP_WRITE_BOUNDARY_HIGH",  .addr = A_PP_WRITE_BOUNDARY_HIGH,
        .rsvd = 0xff,
    },{ .name = "PP_WRITE_BOUNDARY_ADDR", .addr = A_PP_WRITE_BOUNDARY_ADDR,
        .rsvd = 0x3,
    },{ .name = "PP_BUS_ERROR_STATUS",  .addr = A_PP_BUS_ERROR_STATUS,
        .rsvd = 0xfffffc00,
    },{ .name = "PP_PERF_CNT_0_ENABLE",  .addr = A_PP_PERF_CNT_0_ENABLE,
        .rsvd = 0xfffffffc,
    },{ .name = "PP_PERF_CNT_0_SRC",  .addr = A_PP_PERF_CNT_0_SRC,
        .rsvd = 0xffffffc0,
    },{ .name = "PP_PERF_CNT_0_LIMIT",  .addr = A_PP_PERF_CNT_0_LIMIT,
        .reset = 0xc01a0000, /* expected by mali_pp.c */
    },{ .name = "PP_PERF_CNT_0_VALUE",  .addr = A_PP_PERF_CNT_0_VALUE,
    },{ .name = "PP_PERF_CNT_1_ENABLE",  .addr = A_PP_PERF_CNT_1_ENABLE,
        .rsvd = 0xfffffffc,
    },{ .name = "PP_PERF_CNT_1_SRC",  .addr = A_PP_PERF_CNT_1_SRC,
        .rsvd = 0xffffffc0,
    },{ .name = "PP_PERF_CNT_1_VALUE",  .addr = A_PP_PERF_CNT_1_VALUE,
    },{ .name = "PP_PERFMON_CONTR",  .addr = A_PP_PERFMON_CONTR,
        .rsvd = 0xfc00fffe,
    },{ .name = "PP_PERFMON_BASE",  .addr = A_PP_PERFMON_BASE,
        .rsvd = 0x7,
    },
};

static const RegisterAccessInfo mali400_regs_access_pp_rend[] = {
    {   .name = "PP_FRAME",  .addr = A_PP_FRAME,
        .rsvd = 0x7,
    },{ .name = "PP_RSW",  .addr = A_PP_RSW,
        .rsvd = 0x3f,
    },{ .name = "PP_VERTEX",  .addr = A_PP_VERTEX,
        .rsvd = 0x3f,
    },{ .name = "PP_REND_FLAGS",  .addr = A_PP_REND_FLAGS,
        .reset = 0x2,
        .rsvd = 0xffffff80,
    },{ .name = "PP_CLEAR_VALUE_DEPTH",  .addr = A_PP_CLEAR_VALUE_DEPTH,
        .rsvd = 0xff000000,
    },{ .name = "PP_CLEAR_VALUE_STENCIL",  .addr = A_PP_CLEAR_VALUE_STENCIL,
        .rsvd = 0xffffff00,
    },{ .name = "PP_CLEAR_VALUE_COLOR",  .addr = A_PP_CLEAR_VALUE_COLOR,
    },{ .name = "PP_CLEAR_VALUE_COLOR_1",  .addr = A_PP_CLEAR_VALUE_COLOR_1,
    },{ .name = "PP_CLEAR_VALUE_COLOR_2",  .addr = A_PP_CLEAR_VALUE_COLOR_2,
    },{ .name = "PP_CLEAR_VALUE_COLOR_3",  .addr = A_PP_CLEAR_VALUE_COLOR_3,
    },{ .name = "PP_WIDTH",  .addr = A_PP_WIDTH,
        .rsvd = 0xfff0c000,
    },{ .name = "PP_HEIGHT",  .addr = A_PP_HEIGHT,
        .rsvd = 0xffffc000,
    },{ .name = "PP_STACK",  .addr = A_PP_STACK,
        .rsvd = 0x3f,
    },{ .name = "PP_STACK_SIZE",  .addr = A_PP_STACK_SIZE,
    },{ .name = "PP_ORIGIN_OFFSET_X",  .addr = A_PP_ORIGIN_OFFSET_X,
    },{ .name = "PP_ORIGIN_OFFSET_Y",  .addr = A_PP_ORIGIN_OFFSET_Y,
    },{ .name = "PP_SUBPIXEL_SPECIFIER",  .addr = A_PP_SUBPIXEL_SPECIFIER,
        .reset = 0x75,
    },{ .name = "PP_ONSCREEN",  .addr = A_PP_ONSCREEN,
        .rsvd = 0xfffffff8,
    },{ .name = "PP_BLOCKING",  .addr = A_PP_BLOCKING,
        .rsvd = 0xc000ffc0,
    },{ .name = "PP_SCALING",  .addr = A_PP_SCALING,
        .rsvd = 0xff88f000,
    },{ .name = "PP_CHANNEL_LAYOUT",  .addr = A_PP_CHANNEL_LAYOUT,
        .rsvd = 0xffff0000,
    },
};

static const RegisterAccessInfo mali400_regs_access_pp_wb[] = {
    {   .name = "PP_WB_TYPE",  .addr = A_PP_WB_TYPE,
    },{ .name = "PP_WB_ADDRESS",  .addr = A_PP_WB_ADDRESS,
    },{ .name = "PP_WB_PIXEL_FORMAT",  .addr = A_PP_WB_PIXEL_FORMAT,
    },{ .name = "PP_WB_DOWNSAMPLE_FACTOR",  .addr = A_PP_WB_DOWNSAMPLE_FACTOR,
        .rsvd = 0xffff8cf8,
    },{ .name = "PP_WB_PIXEL_LAYOUT",  .addr = A_PP_WB_PIXEL_LAYOUT,
        .rsvd = 0xfffffffc,
    },{ .name = "PP_WB_PITCH",  .addr = A_PP_WB_PITCH,
    },{ .name = "PP_WB_FLAGS",  .addr = A_PP_WB_FLAGS,
    },{ .name = "PP_WB_MRT_BITS",  .addr = A_PP_WB_MRT_BITS,
        .rsvd = 0xfffffff0,
    },{ .name = "PP_WB_MRT_PITCH",  .addr = A_PP_WB_MRT_PITCH,
    },{ .name = "PP_WB_UNUSED0",  .addr = A_PP_WB_UNUSED0,
    },{ .name = "PP_WB_UNUSED1",  .addr = A_PP_WB_UNUSED1,
        .rsvd = 0xffff0000,
    },{ .name = "PP_WB_UNUSED2",  .addr = A_PP_WB_UNUSED2,
        .rsvd = 0xfffffff8,
   },
};

static RegisterInfo *mali400_reg_info_base(RegisterInfoArray *reg_array)
{
    size_t n;

    if (!reg_array || !reg_array->num_elements || !reg_array->r) {
        return NULL;
    }

    /* Return this block's RI[0] in the device state */
    for (n = 0; n < reg_array->num_elements; n++) {
        RegisterInfo *ri = reg_array->r[n];

        if (ri && ri->access) {
            return ri - (ri->access->addr / 4);
        }
    }

    return NULL;
}

static const char *mali400_reg_name(RegisterInfoArray *reg_array,
                                    hwaddr dev_addr)
{
    RegisterInfo *ri0 = mali400_reg_info_base(reg_array);
    const RegisterAccessInfo *ac;
    hwaddr a0, n;

    if (!ri0) {
        return NULL;
    }

    a0 = reg_array->mem.addr;
    if (dev_addr < a0) {
        return NULL;
    }

    n = dev_addr - a0;
    if (n >= memory_region_size(&reg_array->mem)) {
        return NULL;
    }

    ac = ri0[n / 4].access;
    return ac ? ac->name : NULL;
}

static void mali400_reg_trace_summary(ArmMali400 *s)
{
    g_autofree char *by_addr = NULL;
    const char *reg_name = NULL;
    const char *rgn_name = NULL;

    if (s->reg_trc.count < 2) {
        return;
    }

    /* Find the name of the register */
    if (s->reg_trc.block) {
        rgn_name = s->reg_trc.block->prefix;
        reg_name = mali400_reg_name(s->reg_trc.block, s->reg_trc.addr);
    }

    if (!reg_name) {
        by_addr = g_strdup_printf("[0x%" PRIx64 "]", s->reg_trc.addr);
        reg_name = by_addr;
    }

    if (!rgn_name) {
        rgn_name = TYPE_ARM_MALI400;
    }

    qemu_log("%s:%s: read repeated %" PRIu64 " times;"
             " last value = 0x%" PRIx64 "\n",
             rgn_name, reg_name, s->reg_trc.count, s->reg_trc.data);
}

static void mali400_reg_trace_reset(ArmMali400 *s)
{
    s->reg_trc.addr = UINT64_MAX;
    s->reg_trc.block = NULL;
    s->reg_trc.count = 0;
}

static void mali400_reg_trace_update(ArmMali400 *s,
                                     RegisterInfoArray *reg_array, hwaddr addr)
{
    hwaddr dev_addr = reg_array->mem.addr + addr;

    if (dev_addr == s->reg_trc.addr) {
        /* Suppress tracing repeated reads from same address */
        reg_array->debug = false;
        s->reg_trc.count++;
        return;
    }

    /* Reset for new read */
    s->reg_trc.addr = dev_addr;
    s->reg_trc.block = reg_array;
    s->reg_trc.count = 1;
}

static uint64_t mali400_reg_access(RegisterInfoArray *reg_array,
                                   hwaddr addr, uint64_t val,
                                   unsigned size, bool wr)
{
    ArmMali400 *s = ARM_MALI400(reg_array->r[0]->opaque);

    /* Fast path for non-tracing */
    if (!reg_array->debug) {
        if (!wr) {
            return register_read_memory(reg_array, addr, size);
        }

        register_write_memory(reg_array, addr, val, size);
        return val;
    }

    /* A write flushes out a tally of repeated reads  */
    if (wr) {
        mali400_reg_trace_summary(s);
        mali400_reg_trace_reset(s);

        register_write_memory(reg_array, addr, val, size);
        return val;
    }

    mali400_reg_trace_update(s, reg_array, addr);
    s->reg_trc.data = register_read_memory(reg_array, addr, size);

    reg_array->debug = true;
    return s->reg_trc.data;
}

static uint64_t mali400_reg_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    return mali400_reg_access(opaque, addr, 0, size, false);
}

static void mali400_reg_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    mali400_reg_access(opaque, addr, value, size, true);
}

static const MemoryRegionOps mali400_ops = {
    .read = mali400_reg_read,
    .write = mali400_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void mali400_reset_enter(Object *obj, ResetType type)
{
    ArmMali400 *s = ARM_MALI400(obj);
    bool resetting = s->resetting;
    unsigned int i;

    s->resetting = true;

    MALI400_RESET_REGS(s, l2c);
    MALI400_RESET_REGS(s, pmu);
    MALI400_RESET_REGS(s, gp.mmu);
    MALI400_RESET_REGS(s, gp.core);

    for (i = 0; i < ARRAY_SIZE(s->regs.pp); ++i) {
        MALI400_RESET_REGS(s, pp[i].mmu);
        mali400_pp_reset(s, &s->regs.pp[i]);
    }

    mali400_reg_trace_reset(s);
    s->resetting = resetting;
}

/* Register block descriptor for memory-region construction */
typedef struct mali400_reg_block {
    const char *suffix;
    hwaddr offset;
    hwaddr size;
    uint32_t *regs;
    RegisterInfo *info;
    const RegisterAccessInfo *access;
    size_t ac_cnt;
    RegisterInfoArray *regs_array;
} mali400_reg_block;

static void mali400_init_reg_block(ArmMali400 *s, hwaddr *mr_end,
                                   mali400_reg_block *rb)
{
    MemoryRegion *mr;
    Object *mr_owner;
    uint64_t mr_size;
    void *mr_opaque;
    const void *mr_ops;
    g_autofree char *name = NULL;

    rb->regs_array = register_init_block32(DEVICE(s), rb->access, rb->ac_cnt,
                                           rb->info, rb->regs, &mali400_ops,
                                           s->reg_trc.enable, rb->size);

    /* Rename the MR with suffix by recreating it */
    mr = &rb->regs_array->mem;
    mr_ops = mr->ops;
    mr_size = memory_region_size(mr);
    mr_owner = memory_region_owner(mr);
    mr_opaque = mr->opaque;

    name = g_strjoin(NULL, memory_region_name(mr), "-", rb->suffix, NULL);

    object_unparent(OBJECT(mr));
    memory_region_init_io(mr, mr_owner, mr_ops, mr_opaque, name, mr_size);

    mr_size = memory_region_size(mr);
    mr_size = MAX(mr_size, rb->size);
    *mr_end = MAX(*mr_end, (rb->offset + mr_size));

    rb->regs_array->prefix = memory_region_name(mr);
}

static void mali400_init_reg_regions(ArmMali400 *s)
{
    hwaddr mr_end = 0;
    unsigned mr;
    mali400_reg_block *rb;

    mali400_reg_block blocks_basic[] = {
        {   .suffix = "l2c",
            .offset = MALI400_OFFSET_L2C,
            .size   = sizeof(s->regs.l2c),
            .regs   = s->regs.l2c,
            .info   = s->regs_info.l2c,
            .access = mali400_regs_access_l2c,
            .ac_cnt = ARRAY_SIZE(mali400_regs_access_l2c),
        },{ .suffix = "pmu",
            .offset = MALI400_OFFSET_PMU,
            .size   = sizeof(s->regs.pmu),
            .regs   = s->regs.pmu,
            .info   = s->regs_info.pmu,
            .access = mali400_regs_access_pmu,
            .ac_cnt = ARRAY_SIZE(mali400_regs_access_pmu),
        },{ .suffix = "gp_mmu",
            .offset = MALI400_OFFSET_GP_MMU,
            .size   = sizeof(s->regs.gp.mmu),
            .regs   = s->regs.gp.mmu,
            .info   = s->regs_info.gp.mmu,
            .access = mali400_regs_access_mmu,
            .ac_cnt = ARRAY_SIZE(mali400_regs_access_mmu),
        },{ .suffix = "gp_core",
            .offset = MALI400_OFFSET_GP_CORE,
            .size   = sizeof(s->regs.gp.core),
            .regs   = s->regs.gp.core,
            .info   = s->regs_info.gp.core,
            .access = mali400_regs_access_gp_core,
            .ac_cnt = ARRAY_SIZE(mali400_regs_access_gp_core),
        },
    };

    mali400_reg_block blocks_pp[] = {
        {   .suffix = "pp0_mmu",  .offset = MALI400_OFFSET_PP0_MMU,
        },{ .suffix = "pp0_core", .offset = MALI400_OFFSET_PP0_CORE,
        },{ .suffix = "pp0_rend", .offset = MALI400_OFFSET_PP0_REND,
        },{ .suffix = "pp0_wb0",  .offset = MALI400_OFFSET_PP0_WB0,
        },{ .suffix = "pp0_wb1",  .offset = MALI400_OFFSET_PP0_WB1,
        },{ .suffix = "pp0_wb2",  .offset = MALI400_OFFSET_PP0_WB2,
        },{ .suffix = "pp1_mmu",  .offset = MALI400_OFFSET_PP1_MMU,
        },{ .suffix = "pp1_core", .offset = MALI400_OFFSET_PP1_CORE,
        },{ .suffix = "pp1_rend", .offset = MALI400_OFFSET_PP1_REND,
        },{ .suffix = "pp1_wb0",  .offset = MALI400_OFFSET_PP1_WB0,
        },{ .suffix = "pp1_wb1",  .offset = MALI400_OFFSET_PP1_WB1,
        },{ .suffix = "pp1_wb2",  .offset = MALI400_OFFSET_PP1_WB2,
        },{ .suffix = "pp2_mmu",  .offset = MALI400_OFFSET_PP2_MMU,
        },{ .suffix = "pp2_core", .offset = MALI400_OFFSET_PP2_CORE,
        },{ .suffix = "pp2_rend", .offset = MALI400_OFFSET_PP2_REND,
        },{ .suffix = "pp2_wb0",  .offset = MALI400_OFFSET_PP2_WB0,
        },{ .suffix = "pp2_wb1",  .offset = MALI400_OFFSET_PP2_WB1,
        },{ .suffix = "pp2_wb2",  .offset = MALI400_OFFSET_PP2_WB2,
        },{ .suffix = "pp3_mmu",  .offset = MALI400_OFFSET_PP3_MMU,
        },{ .suffix = "pp3_core", .offset = MALI400_OFFSET_PP3_CORE,
        },{ .suffix = "pp3_rend", .offset = MALI400_OFFSET_PP3_REND,
        },{ .suffix = "pp3_wb0",  .offset = MALI400_OFFSET_PP3_WB0,
        },{ .suffix = "pp3_wb1",  .offset = MALI400_OFFSET_PP3_WB1,
        },{ .suffix = "pp3_wb2",  .offset = MALI400_OFFSET_PP3_WB2,
        },
    };

    /* Fill in common values in PP block descriptors */
    mr = s->num_pp;
    mr = MIN(mr, ARRAY_SIZE(s->regs.pp));
    mr = MAX(mr, 1);
    s->num_pp = mr;
    while (mr--) {
        unsigned n0 = 6 * mr;
        unsigned nb;

        rb = &blocks_pp[n0];
        rb->size = sizeof(s->regs.pp[mr].mmu);
        rb->regs = s->regs.pp[mr].mmu;
        rb->info = s->regs_info.pp[mr].mmu;
        rb->access = mali400_regs_access_mmu;
        rb->ac_cnt = ARRAY_SIZE(mali400_regs_access_mmu);

        rb++;
        rb->size = sizeof(s->regs.pp[mr].core);
        rb->regs = s->regs.pp[mr].core;
        rb->info = s->regs_info.pp[mr].core;
        rb->access = mali400_regs_access_pp_core;
        rb->ac_cnt = ARRAY_SIZE(mali400_regs_access_pp_core);

        rb++;
        rb->size = sizeof(s->regs.pp[mr].rend);
        rb->regs = s->regs.pp[mr].rend;
        rb->info = s->regs_info.pp[mr].rend;
        rb->access = mali400_regs_access_pp_rend;
        rb->ac_cnt = ARRAY_SIZE(mali400_regs_access_pp_rend);

        for (nb = 0; nb < ARRAY_SIZE(s->regs.pp[mr].wb); nb++) {
            rb++;
            rb->size = sizeof(s->regs.pp[mr].wb[nb]);
            rb->regs = s->regs.pp[mr].wb[nb];
            rb->info = s->regs_info.pp[mr].wb[nb];
            rb->access = mali400_regs_access_pp_wb;
            rb->ac_cnt = ARRAY_SIZE(mali400_regs_access_pp_wb);
        }
    }

    /* Construct all sub-regions */
    mr_end = 0;
    for (mr = 0; mr < ARRAY_SIZE(blocks_basic); mr++) {
        mali400_init_reg_block(s, &mr_end, &blocks_basic[mr]);
    }
    for (mr = 0; mr < ARRAY_SIZE(blocks_pp); mr++) {
        if (!blocks_pp[mr].regs) {
            break;
        }
        mali400_init_reg_block(s, &mr_end, &blocks_pp[mr]);
    }

    /*
     * Construct the container region with the proper size;
     * then, attach all sub-regions
     */
    memory_region_init(&s->iomem, OBJECT(s), TYPE_ARM_MALI400, mr_end);

    for (mr = 0; mr < ARRAY_SIZE(blocks_basic); mr++) {
        rb = &blocks_basic[mr];
        memory_region_add_subregion(&s->iomem,
                                    rb->offset, &rb->regs_array->mem);
    }

    for (mr = 0; mr < ARRAY_SIZE(blocks_pp); mr++) {
        rb = &blocks_pp[mr];
        if (!rb->regs) {
            break;
        }
        memory_region_add_subregion(&s->iomem,
                                    rb->offset, &rb->regs_array->mem);
    }
}
static void mali400_realize(DeviceState *dev, Error **errp)
{
    ArmMali400 *s = ARM_MALI400(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    mali400_init_reg_regions(s);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void mali400_init(Object *obj)
{
    /* Further construction depends on post-init setting of properties */
}

static const VMStateDescription vmstate_mali400 = {
    .name = TYPE_ARM_MALI400,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irq_pending, ArmMali400),
        VMSTATE_UINT32_ARRAY(regs.l2c, ArmMali400, ARM_MALI400_L2C_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pmu, ArmMali400, ARM_MALI400_PMU_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.gp.mmu,    ArmMali400, ARM_MALI400_MMU_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[0].mmu, ArmMali400, ARM_MALI400_MMU_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[1].mmu, ArmMali400, ARM_MALI400_MMU_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[2].mmu, ArmMali400, ARM_MALI400_MMU_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[3].mmu, ArmMali400, ARM_MALI400_MMU_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.gp.core,    ArmMali400,
                                              ARM_MALI400_GP_CORE_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[0].core, ArmMali400,
                                              ARM_MALI400_PP_CORE_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[1].core, ArmMali400,
                                              ARM_MALI400_PP_CORE_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[2].core, ArmMali400,
                                              ARM_MALI400_PP_CORE_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[3].core, ArmMali400,
                                              ARM_MALI400_PP_CORE_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[0].rend, ArmMali400,
                                              ARM_MALI400_PP_REND_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[1].rend, ArmMali400,
                                              ARM_MALI400_PP_REND_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[2].rend, ArmMali400,
                                              ARM_MALI400_PP_REND_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[3].rend, ArmMali400,
                                              ARM_MALI400_PP_REND_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[0].wb[0], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[0].wb[1], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[0].wb[2], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[1].wb[0], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[1].wb[1], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[1].wb[2], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[2].wb[0], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[2].wb[1], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[2].wb[2], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[3].wb[0], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[3].wb[1], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_UINT32_ARRAY(regs.pp[3].wb[2], ArmMali400,
                                               ARM_MALI400_PP_WB_R_MAX),
        VMSTATE_END_OF_LIST(),
    },
};

static Property mali400_properties[] = {
    DEFINE_PROP_BOOL("reg-trace", ArmMali400, reg_trc.enable, false),
    DEFINE_PROP_UINT32("l2c-version", ArmMali400, l2c_version,
                       ((0xcac2 << 16) | (0x01 << 7) | 0x01)),
    DEFINE_PROP_UINT32("l2c-size", ArmMali400, l2c_size,
                       ((7 << 24) | (16 << 16) | (2 << 8) | 6)),
    DEFINE_PROP_UINT32("num-pp", ArmMali400, num_pp, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void mali400_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mali400_realize;
    dc->vmsd = &vmstate_mali400;
    device_class_set_props(dc, mali400_properties);

    rc->phases.enter = mali400_reset_enter;
}

static const TypeInfo mali400_info = {
    .name          = TYPE_ARM_MALI400,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ArmMali400),
    .class_init    = mali400_class_init,
    .instance_init = mali400_init,
};

static void mali400_register_types(void)
{
    type_register_static(&mali400_info);
}

type_init(mali400_register_types)
