#ifndef M24CXX__H
#define M24CXX__H
#include "hw/i2c/i2c.h"

typedef enum {
    STOPPED,
    ADDRESSING,
    READING,
    WRITING,
} M24CXXXferState;

typedef struct {
    I2CSlave i2c;
    uint16_t cur_addr;
    uint8_t state;
    uint8_t addr_count;
    uint8_t num_addr_bytes;

    BlockBackend *blk;
    uint16_t size;

    uint8_t *storage;
} M24CXXState;

#define TYPE_M24CXX "m24cxx"

#define M24CXX(obj) \
     OBJECT_CHECK(M24CXXState, (obj), TYPE_M24CXX)
#endif

