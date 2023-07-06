/*
 * UFS Device
 * Based on JESD220E
 *
 * SPDX-FileCopyrightText: 2023 AMD
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef UFS_DEV_H
#define UFS_DEV_H

#include "hw/block/ufshc-if.h"
#include "hw/block/ufs-upiu.h"
#include "hw/block/ufs-scsi-if.h"
#include "hw/block/ufs-scsi-core.h"

#define TYPE_UFS_DEV "ufs-dev"
#define UFS_DEV(obj) \
        OBJECT_CHECK(UFSDev, (obj), TYPE_UFS_DEV)

#define UFS_MAX_LUN 32

#define UFS_DEV_DESC_SIZE 0x59
#define UNIT_DESC_CONFIG_OFFSET 0x16
#define UNIT_DESC_CONFIG_LENGTH 0x1A
#define UFS_DEV_CONFIG_DESC_SIZE UNIT_DESC_CONFIG_OFFSET
#define CONFIG_UNIT_OFFSET(n) \
    (UFS_DEV_CONFIG_DESC_SIZE + (n * UNIT_DESC_CONFIG_LENGTH))
#define UFS_UNIT_DESC_SIZE 0x2D
#define UFS_GOME_DESC_SIZE 0x57
#define UFS_INTRCON_DESC_SIZE 0x6
#define UFS_MAN_STR_DESC_SIZE 0x12
#define UFS_PROD_STR_DESC_SIZE 0x22
#define UFS_OEM_ID_STR_SIZE 0x4
#define UFS_SERIAL_NUM_STR_SIZE 0x4
#define UFS_PROD_REV_LVL_STR_SIZE 0xa
#define UFS_DEV_HEALTH_DESC_SIZE 0x2d
#define UFS_DEV_PWR_PARAM_DESC_SIZE 0x62

#define UFS_SEGMENT_SIZE 512
typedef struct UFSTaskQ {
    upiu_pkt pkt;
    uint32_t data_offset;
    QTAILQ_ENTRY(UFSTaskQ) link;
} UFSTaskQ;

typedef struct UFSDev {
    DeviceState parent;

    /*
     * ufs initiator interface
     */
    ufshcIF *ufs_ini;
    /*
     * scsi device interface
     */
    ufs_scsi_if *ufs_scsi_target;
    /*
     * Define luns
     */
    UFSScsiCore core;
    ufsBus *bus;
    uint8_t num_luns;
    uint8_t BootLUA;
    uint8_t BootLUB;
    uint8_t devInitDone;

    QTAILQ_HEAD(, UFSTaskQ) taskQ;
    struct {
        /*
         * Device Desc
         */
        uint8_t device[UFS_DEV_DESC_SIZE];
        /*
         * Configuration Desc
         * Access Unit Desc from config
         */
        uint8_t *config[4];
        /*
         * Geometry Desc
         */
        uint8_t geo[UFS_GOME_DESC_SIZE];
        /*
         * Unit Desc
         */
        uint8_t *unit[UFS_MAX_LUN];
        /*
         * Interconnect Desc
         */
        uint8_t interconnect[UFS_INTRCON_DESC_SIZE];
        /*
         * Manufacturer String
         */
        uint8_t manStr[UFS_MAN_STR_DESC_SIZE];
        /*
         * Product Name String
         */
        uint8_t prodStr[UFS_PROD_STR_DESC_SIZE];
        /*
         * OEM ID String
         */
        uint8_t oemIdStr[UFS_OEM_ID_STR_SIZE];
        /*
         * Serial Number String
         */
        uint8_t serialNumStr[UFS_SERIAL_NUM_STR_SIZE];
        /*
         * Product Revision Level String
         */
        uint8_t prodRevLvlStr[UFS_PROD_REV_LVL_STR_SIZE];
        /*
         * Device Health Desc
         */
        uint8_t devHealth[UFS_DEV_HEALTH_DESC_SIZE];
        /*
         * Power Parameter Desc
         */
        uint8_t pwrParam[UFS_DEV_PWR_PARAM_DESC_SIZE];
    } ufsDesc;

    struct {
        /*
         * Attributes
         */
         uint8_t BootLunEn;
         uint16_t ExceptionEventControl;
         uint16_t ExceptionEventStatus;
    } attr;

    struct {
       /*
        * Flags
        */
       uint8_t DeviceInit;
    } flag;
} UFSDev;

typedef enum UfsDevDesc {
    UFS_DEV_DEVICE = 0,
    UFS_DEV_CONFIGURATION = 1,
    UFS_DEV_UNIT = 2,
    UFS_DEV_INTERCONNECT = 4,
    UFS_DEV_STRING = 5,
    UFS_DEV_GEOMETRY = 7,
    UFS_DEV_POWER = 8,
    UFS_DEV_DEVICE_HEALTH = 9
} UfsDevDesc;
#endif
