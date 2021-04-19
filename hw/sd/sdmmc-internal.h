/*
 * SD/MMC cards common
 *
 * Copyright (c) 2018  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SDMMC_INTERNAL_H
#define SDMMC_INTERNAL_H

#define SDMMC_CMD_MAX 64

/**
 * sd_cmd_name:
 * @cmd: A SD "normal" command, up to SDMMC_CMD_MAX.
 *
 * Returns a human-readable name describing the command.
 * The return value is always a static string which does not need
 * to be freed after use.
 *
 * Returns: The command name of @cmd or "UNKNOWN_CMD".
 */
const char *sd_cmd_name(uint8_t cmd);

/**
 * sd_acmd_name:
 * @cmd: A SD "Application-Specific" command, up to SDMMC_CMD_MAX.
 *
 * Returns a human-readable name describing the application command.
 * The return value is always a static string which does not need
 * to be freed after use.
 *
 * Returns: The application command name of @cmd or "UNKNOWN_ACMD".
 */
const char *sd_acmd_name(uint8_t cmd);

/*
 * EXT_CSD fields
 */

#define EXT_CSD_CMDQ_MODE_EN    15  /* R/W */
#define EXT_CSD_FLUSH_CACHE   32      /* W */
#define EXT_CSD_CACHE_CTRL    33      /* R/W */
#define EXT_CSD_POWER_OFF_NOTIFICATION  34  /* R/W */
#define EXT_CSD_PACKED_FAILURE_INDEX  35  /* RO */
#define EXT_CSD_PACKED_CMD_STATUS 36  /* RO */
#define EXT_CSD_EXP_EVENTS_STATUS 54  /* RO, 2 bytes */
#define EXT_CSD_EXP_EVENTS_CTRL   56  /* R/W, 2 bytes */
#define EXT_CSD_DATA_SECTOR_SIZE  61  /* R */
#define EXT_CSD_GP_SIZE_MULT    143 /* R/W */
#define EXT_CSD_PARTITION_SETTING_COMPLETED 155 /* R/W */
#define EXT_CSD_PARTITION_ATTRIBUTE 156 /* R/W */
#define EXT_CSD_PARTITION_SUPPORT 160 /* RO */
#define EXT_CSD_HPI_MGMT    161 /* R/W */
#define EXT_CSD_RST_N_FUNCTION    162 /* R/W */
#define EXT_CSD_BKOPS_EN    163 /* R/W */
#define EXT_CSD_BKOPS_START   164 /* W */
#define EXT_CSD_SANITIZE_START    165     /* W */
#define EXT_CSD_WR_REL_PARAM    166 /* RO */
#define EXT_CSD_RPMB_MULT   168 /* RO */
#define EXT_CSD_FW_CONFIG   169 /* R/W */
#define EXT_CSD_BOOT_WP     173 /* R/W */
#define EXT_CSD_ERASE_GROUP_DEF   175 /* R/W */
#define EXT_CSD_PART_CONFIG   179 /* R/W */
#define EXT_CSD_ERASED_MEM_CONT   181 /* RO */
#define EXT_CSD_BUS_WIDTH   183 /* R/W */
#define EXT_CSD_STROBE_SUPPORT    184 /* RO */
#define EXT_CSD_HS_TIMING   185 /* R/W */
#define EXT_CSD_POWER_CLASS   187 /* R/W */
#define EXT_CSD_REV     192 /* RO */
#define EXT_CSD_STRUCTURE   194 /* RO */
#define EXT_CSD_CARD_TYPE   196 /* RO */
#define EXT_CSD_DRIVER_STRENGTH   197 /* RO */
#define EXT_CSD_OUT_OF_INTERRUPT_TIME 198 /* RO */
#define EXT_CSD_PART_SWITCH_TIME        199     /* RO */
#define EXT_CSD_PWR_CL_52_195   200 /* RO */
#define EXT_CSD_PWR_CL_26_195   201 /* RO */
#define EXT_CSD_PWR_CL_52_360   202 /* RO */
#define EXT_CSD_PWR_CL_26_360   203 /* RO */
#define EXT_CSD_SEC_CNT     212 /* RO, 4 bytes */
#define EXT_CSD_S_A_TIMEOUT   217 /* RO */
#define EXT_CSD_S_C_VCCQ          219     /* RO */
#define EXT_CSD_S_C_VCC                 220     /* RO */
#define EXT_CSD_REL_WR_SEC_C    222 /* RO */
#define EXT_CSD_HC_WP_GRP_SIZE    221 /* RO */
#define EXT_CSD_ERASE_TIMEOUT_MULT  223 /* RO */
#define EXT_CSD_HC_ERASE_GRP_SIZE 224 /* RO */
#define EXT_CSD_ACC_SIZE    225 /* RO */
#define EXT_CSD_BOOT_MULT   226 /* RO */
#define EXT_CSD_BOOT_INFO   228 /* RO */
#define EXT_CSD_SEC_TRIM_MULT   229 /* RO */
#define EXT_CSD_SEC_ERASE_MULT    230 /* RO */
#define EXT_CSD_SEC_FEATURE_SUPPORT 231 /* RO */
#define EXT_CSD_TRIM_MULT   232 /* RO */
#define EXT_CSD_PWR_CL_200_195    236 /* RO */
#define EXT_CSD_PWR_CL_200_360    237 /* RO */
#define EXT_CSD_PWR_CL_DDR_52_195 238 /* RO */
#define EXT_CSD_PWR_CL_DDR_52_360 239 /* RO */
#define EXT_CSD_BKOPS_STATUS    246 /* RO */
#define EXT_CSD_POWER_OFF_LONG_TIME 247 /* RO */
#define EXT_CSD_GENERIC_CMD6_TIME 248 /* RO */
#define EXT_CSD_CACHE_SIZE    249 /* RO, 4 bytes */
#define EXT_CSD_PWR_CL_DDR_200_360  253 /* RO */
#define EXT_CSD_FIRMWARE_VERSION  254 /* RO, 8 bytes */
#define EXT_CSD_PRE_EOL_INFO    267 /* RO */
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A  268 /* RO */
#define EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_B  269 /* RO */
#define EXT_CSD_CMDQ_DEPTH    307 /* RO */
#define EXT_CSD_CMDQ_SUPPORT    308 /* RO */
#define EXT_CSD_SUPPORTED_MODE    493 /* RO */
#define EXT_CSD_TAG_UNIT_SIZE   498 /* RO */
#define EXT_CSD_DATA_TAG_SUPPORT  499 /* RO */
#define EXT_CSD_MAX_PACKED_WRITES 500 /* RO */
#define EXT_CSD_MAX_PACKED_READS  501 /* RO */
#define EXT_CSD_BKOPS_SUPPORT   502 /* RO */
#define EXT_CSD_HPI_FEATURES    503 /* RO */
#define EXT_CSD_S_CMD_SET   504 /* RO */

/*
 * EXT_CSD field definitions
 */

#define EXT_CSD_WR_REL_PARAM_EN   (1 << 2)
#define EXT_CSD_WR_REL_PARAM_EN_RPMB_REL_WR (1 << 4)

#define EXT_CSD_PART_CONFIG_ACC_MASK  (0x7)
#define EXT_CSD_PART_CONFIG_ACC_DEFAULT (0x0)
#define EXT_CSD_PART_CONFIG_ACC_BOOT0 (0x1)

#define EXT_CSD_PART_CONFIG_EN_MASK (0x7 << 3)
#define EXT_CSD_PART_CONFIG_EN_BOOT0  (0x1 << 3)
#define EXT_CSD_PART_CONFIG_EN_USER (0x7 << 3)

#endif
