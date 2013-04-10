/*
 * Register Definition API
 *
 * Copyright (c) 2013 Xilinx Inc.
 * Copyright (c) 2013 Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef REGISTER_H
#define REGISTER_H

#include <stdint.h>
#include <stdbool.h>
#include "exec/memory.h"

typedef struct RegisterInfo RegisterInfo;
typedef struct RegisterAccessInfo RegisterAccessInfo;

/**
 * A register access error message
 * @mask: Bits in the register the error applies to
 * @reason: Reason why this access is an error
 */

typedef struct RegisterAccessError {
    uint64_t mask;
    const char *reason;
} RegisterAccessError;

/**
 * Access description for a register that is part of guest accessible device
 * state.
 *
 * @name: String name of the register
 * @ro: whether or not the bit is read-only
 * @wo: Bits that are write only (read as reset value)
 * @w1c: bits with the common write 1 to clear semantic.
 * @nw0: bits that can't be written with a 0 by the guest (sticky 1)
 * @nw1: bits that can't be written with a 1 by the guest (sticky 0)
 * @reset: reset value.
 * @cor: Bits that are clear on read
 *
 * @ge1: Bits that when written 1 indicate a guest error
 * @ge0: Bits that when written 0 indicate a guest error
 * @ui1: Bits that when written 1 indicate use of an unimplemented feature
 * @ui0: Bits that when written 0 indicate use of an unimplemented feature
 *
 * @pre_write: Pre write callback. Passed the value that's to be written,
 * immediately before the actual write. The returned value is what is written,
 * giving the handler a chance to modify the written value.
 * @post_write: Post write callback. Passed the written value. Most write side
 * effects should be implemented here.
 *
 * @pre_read: Pre read callback.
 * @post_read: Post read callback. Passes the value that is about to be returned
 * for a read. The return value from this function is what is ultimately read,
 * allowing this function to modify the value before return to the client.
 */

struct RegisterAccessInfo {
    const char *name;
    uint64_t ro;
    uint64_t wo;
    uint64_t w1c;
    uint64_t nw0;
    uint64_t nw1;
    uint64_t reset;
    uint64_t cor;

    const RegisterAccessError *ge0;
    const RegisterAccessError *ge1;
    const RegisterAccessError *ui0;
    const RegisterAccessError *ui1;

    uint64_t (*pre_write)(RegisterInfo *reg, uint64_t val);
    void (*post_write)(RegisterInfo *reg, uint64_t val);

    void (*pre_read)(RegisterInfo *reg);
    uint64_t (*post_read)(RegisterInfo *reg, uint64_t val);
};

/**
 * A register that is part of guest accessible state
 * @data: pointer to the register data
 * @data_size: Size of the register in bytes
 * @data_big_endian: Define endianess of data register
 *
 * @access: Access desciption of this register
 *
 * @debug: Whether or not verbose debug is enabled
 * @prefix: String prefix for log and debug messages
 *
 * @opaque: Opaque data for the register
 *
 * @mem: optional Memory region for the register
 */

struct RegisterInfo {
    uint8_t *data;
    int data_size;
    bool data_big_endian;

    const RegisterAccessInfo *access;

    bool debug;
    const char *prefix;

    void *opaque;

    MemoryRegion mem;
};

/**
 * write a value to a register, subject to its restrictions
 * @reg: register to write to
 * @val: value to write
 * @we: write enable mask
 */

void register_write(RegisterInfo *reg, uint64_t val, uint64_t we);

/**
 * read a value from a register, subject to its restrictions
 * @reg: register to read from
 * returns: value read
 */

uint64_t register_read(RegisterInfo *reg);

/**
 * reset a register
 * @reg: register to reset
 */

void register_reset(RegisterInfo *reg);

void register_write_memory_be(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size);
void register_write_memory_le(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size);

uint64_t register_read_memory_be(void *opaque, hwaddr addr, unsigned size);
uint64_t register_read_memory_le(void *opaque, hwaddr addr, unsigned size);

#endif
