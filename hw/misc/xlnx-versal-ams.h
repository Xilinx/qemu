/*
 * Bi-directional interface between subcomponents of Xilinx AMS.
 *
 * Copyright (c) 2021 Xilinx Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef XLNX_VERSAL_AMS_H
#define XLNX_VERSAL_AMS_H

#include "qemu/osdep.h"

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
