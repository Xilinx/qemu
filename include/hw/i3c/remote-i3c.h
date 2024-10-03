/*
 * Remote I3C Device
 *
 * Copyright (c) 2023 Google LLC
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

/*
 * The remote I3C protocol is as follows:
 * On an I3C private and CCC TX (controller -> target)
 * - 1-byte opcode
 * - 4-byte number of bytes in the packet as a LE uint32
 * - n-byte payload
 *
 * On an I3C private and CCC RX (target -> controller)
 * Controller to target:
 * - 1-byte opcode
 * - 4-byte number of bytes to read as a LE uint32
 * Remote target response:
 * - 4-byte number of bytes in the packet as a LE uint32
 * - n-byte payload
 *
 * IBI (target -> controller, initiated by target)
 * - 1-byte opcode
 * - 1-byte IBI address
 * - 1-byte RnW boolean
 * - 4-byte length of IBI payload from target as a LE uint32 (can be 0)
 * - n-byte IBI payload
 */

#ifndef REMOTE_I3C_H_
#define REMOTE_I3C_H_

#define TYPE_REMOTE_I3C "remote-i3c"
#define REMOTE_I3C(obj) OBJECT_CHECK(RemoteI3C, (obj), TYPE_REMOTE_I3C)

/* 1-byte IBI addr, 1-byte is recv, 4-byte data len. */
#define REMOTE_I3C_IBI_HDR_LEN 6

/* Stored in a uint8_t */
typedef enum {
    /* Sent from us to remote target. */
    REMOTE_I3C_START_RECV = 1,
    REMOTE_I3C_START_SEND = 2,
    REMOTE_I3C_STOP = 3,
    REMOTE_I3C_NACK = 4,
    REMOTE_I3C_RECV = 5,
    REMOTE_I3C_SEND = 6,
    REMOTE_I3C_HANDLE_CCC_WRITE = 7,
    REMOTE_I3C_HANDLE_CCC_READ = 8,
    REMOTE_I3C_IBI = 9,
    /* Sent from remote target to us. */
    REMOTE_I3C_IBI_ACK = 0xc0,
    REMOTE_I3C_IBI_NACK = 0xc1,
    REMOTE_I3C_IBI_DATA_NACK = 0xc2,
} RemoteI3CCommand;

typedef enum {
    REMOTE_I3C_RX_ACK = 0,
    REMOTE_I3C_RX_NACK = 1,
} RemoteI3CRXEvent;

#endif
