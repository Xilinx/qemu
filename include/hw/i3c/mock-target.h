#ifndef MOCK_TARGET_H_
#define MOCK_TARGET_H_

/*
 * Mock I3C Device
 *
 * Copyright (c) 2023 Google LLC
 *
 * The mock I3C device can be thought of as a simple EEPROM. It has a buffer,
 * and the pointer in the buffer is reset to 0 on an I3C STOP.
 * To write to the buffer, issue a private write and send data.
 * To read from the buffer, issue a private read.
 *
 * The mock target also supports sending target interrupt IBIs.
 * To issue an IBI, set the 'ibi-magic-num' property to a non-zero number, and
 * send that number in a private transaction. The mock target will issue an IBI
 * after 1 second.
 *
 * It also supports a handful of CCCs that are typically used when probing I3C
 * devices.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/i3c/i3c.h"

#define TYPE_MOCK_TARGET "mock-target"
OBJECT_DECLARE_SIMPLE_TYPE(MockTargetState, MOCK_TARGET)

struct MockTargetState {
    I3CTarget i3c;

    /* General device state */
    bool can_ibi;
    QEMUTimer qtimer;
    size_t p_buf;
    uint8_t *buf;

    /* For Handing CCCs. */
    bool in_ccc;
    I3CCCC curr_ccc;
    uint8_t ccc_byte_offset;

    struct {
        uint32_t buf_size;
        uint8_t ibi_magic;
    } cfg;
};

#endif
