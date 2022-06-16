/*
 * Bi-directional interface between subcomponents of Xilinx AMS.
 *
 * Copyright (c) 2021 Xilinx Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XLNX_VERSAL_AMS_H
#define XLNX_VERSAL_AMS_H

#include "qemu/typedefs.h"

/*
 * Tamper events are delivered to responder by setting its property
 * with bits of occurring events
 */
#define XLNX_AMS_TAMPER_PROP "tamper-events"

enum {
    /* Analog (volt and temp) device-tampering events */
    XLNX_AMS_VOLT_ALARMS_MASK = 255,       /* PMC_SYSMON.REG_ISR.ALARM7..0 */
    XLNX_AMS_VOLT_0_ALARM_MASK = 1 << 0,
    XLNX_AMS_VOLT_1_ALARM_MASK = 1 << 1,
    XLNX_AMS_VOLT_2_ALARM_MASK = 1 << 2,
    XLNX_AMS_VOLT_3_ALARM_MASK = 1 << 3,
    XLNX_AMS_VOLT_4_ALARM_MASK = 1 << 4,
    XLNX_AMS_VOLT_5_ALARM_MASK = 1 << 5,
    XLNX_AMS_VOLT_6_ALARM_MASK = 1 << 6,
    XLNX_AMS_VOLT_7_ALARM_MASK = 1 << 7,

    XLNX_AMS_TEMP_ALARM_MASK = 1 << 8,     /* PMC_SYSMON.REG_ISR.(TEMP | OT) */

    XLNX_AMS_VCCINT_GLITCHES_MASK = 3 << 9,/* PMC_ANALOG.GLITCH_DET_STATUS */
    XLNX_AMS_VCCINT_0_GLITCH_MASK = 1 << 9,
    XLNX_AMS_VCCINT_1_GLITCH_MASK = 1 << 10,

    /* Digital tampering events */
    XLNX_AMS_DBG_TAMPER_TRIG_MASK = 1 << 11,
    XLNX_AMS_MIO_TAMPER_TRIG_MASK = 1 << 12,
    XLNX_AMS_SW_TAMPER_TRIG_MASK  = 1 << 13,
};

typedef struct {
    Object *sat;
    unsigned instance;
    uint16_t meas_id;
    uint8_t meas_bipolar;
    uint8_t root_id;
    uint8_t mode;
    uint8_t amux_ctrl;
    uint8_t abus_sw1;
    uint8_t abus_sw0;
} xlnx_ams_sensor_t;

/*
 * Sentinel values for '.meas_id' field to identify types of sensors.
 */
enum {
    XLNX_AMS_SAT_MEAS_TYPE_TSENS = 1024,
    XLNX_AMS_SAT_MEAS_TYPE_VCCINT,
};

/*
 * Send confiuration-ready indication to AMS-ROOT.
 */
void xlnx_ams_root_sat_config_ready(Object *root, unsigned instance_id);

/*
 * Set instance id of given satellite.
 */
void xlnx_ams_sat_instance_set(Object *sat, unsigned instance_id, Object *root);

/*
 * Return full config for the sensor of spec{amux_ctrl, abus_sw1, abus_sw0}.
 *
 * Return false if sensor not configured into the satellite.
 */
bool xlnx_ams_sat_config_by_spec(Object *sat, xlnx_ams_sensor_t *si);

/*
 * Return full config for the sensor of spec{root_id}
 *
 * Return false if sensor not configured into the satellite.
 */
bool xlnx_ams_sat_config_by_root_id(Object *sat, xlnx_ams_sensor_t *si);

#endif
