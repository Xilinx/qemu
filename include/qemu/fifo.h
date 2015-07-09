#ifndef FIFO_H
#define FIFO_H

#include "migration/vmstate.h"

typedef struct {
    /* All fields are private */
    int width; /* byte width of each element */
    uint32_t capacity; /* number of elements */

    uint8_t *data;
    uint32_t buffer_size;

    uint32_t head;
    uint32_t num;
} Fifo;

/**
 * fifo_create8:
 * @fifo: struct Fifo to initialise with new FIFO
 * @capacity: capacity of the newly created FIFO
 *
 * Create a byte FIFO of the specified size. Clients should call fifo_destroy()
 * when finished using the fifo. The FIFO is initially empty.
 */

void fifo_create8(Fifo *fifo, uint32_t capacity);
void fifo_create16(Fifo *fifo, uint32_t capacity);
void fifo_create32(Fifo *fifo, uint32_t capacity);
void fifo_create64(Fifo *fifo, uint32_t capacity);

/**
 * fifo_destroy:
 * @fifo: FIFO to cleanup
 *
 * Cleanup a FIFO created with fifo_create(). Frees memory created for FIFO
  *storage. The FIFO is no longer usable after this has been called.
 */

void fifo_destroy(Fifo *fifo);

/**
 * fifo_pushX:
 * @fifo: FIFO to push to
 * @data: data value to push
 *
 * Push a data value to the FIFO. Behaviour is undefined if the FIFO is full.
 * Clients are responsible for checking for fullness using fifo_is_full().
 *
 * 8, 16, 32 and 64 bit variants are available. Behaviour is undefined if a
 * variant mismatched to the FIFO width used (e.g. you cannot use fifo_push8
 * with a FIFO created with width == 16).
 */

void fifo_push8(Fifo *fifo, uint8_t data);
void fifo_push16(Fifo *fifo, uint16_t data);
void fifo_push32(Fifo *fifo, uint32_t data);
void fifo_push64(Fifo *fifo, uint64_t data);

/**
 * fifo_push_all:
 * @fifo: FIFO to push to
 * @data: data to push
 * @size: number of entries to push
 *
 * Push a buffer to the FIFO. Behaviour is undefined if the FIFO is full.
 * Clients are responsible for checking the space left in the FIFO using
 * fifo_num_free().
 */

void fifo_push_all(Fifo *fifo, const void *data, uint32_t num);

/**
 * fifo_popX:
 * @fifo: fifo to pop from
 *
 * Pop a data value from the FIFO. Behaviour is undefined if the FIFO is empty.
 * Clients are responsible for checking for emptyness using fifo_is_empty().
 *
 * 8, 16, 32 and 64 bit variants are available. Behaviour is undefined if a
 * variant mismatched to the FIFO width is used (e.g. you cannot use fifo_pop8
 * with a FIFO created with width == 16).
 *
 * Returns: The popped data value.
 */

uint8_t fifo_pop8(Fifo *fifo);
uint16_t fifo_pop16(Fifo *fifo);
uint32_t fifo_pop32(Fifo *fifo);
uint64_t fifo_pop64(Fifo *fifo);

/**
 * fifo_pop_buf:
 * @fifo: FIFO to pop from
 * @max: maximum number of elements to pop
 * @num: actual number of returned elements
 *
 * Pop a number of elements from the FIFO up to a maximum of max. The buffer
 * containing the popped data is returned. This buffer points directly into
 * the FIFO backing store and data is invalidated once any of the fifo_* APIs
 * are called on the FIFO.
 *
 * The function may return fewer elements than requested when the data wraps
 * around in the ring buffer; in this case only a contiguous part of the data
 * is returned.
 *
 * The number of valid elements returned is populated in *num; will always
 * return at least 1 element. max must not be 0 or greater than the number of
 * elements in the FIFO.
 *
 * Clients are responsible for checking the availability of requested data
 * using fifo_num_used().
 *
 * Returns: A pointer to popped data.
 */

const void *fifo_pop_buf(Fifo *fifo, uint32_t max, uint32_t *num);

/**
 * fifo_reset:
 * @fifo: FIFO to reset
 *
 * Reset a FIFO. All data is discarded and the FIFO is emptied.
 */

void fifo_reset(Fifo *fifo);

/**
 * fifo_is_empty:
 * @fifo: FIFO to check
 *
 * Check if a FIFO is empty.
 *
 * Returns: True if the fifo is empty, false otherwise.
 */

bool fifo_is_empty(Fifo *fifo);

/**
 * fifo_is_full:
 * @fifo: FIFO to check
 *
 * Check if a FIFO is full.
 *
 * Returns: True if the fifo is full, false otherwise.
 */

bool fifo_is_full(Fifo *fifo);

/**
 * fifo_num_free:
 * @fifo: FIFO to check
 *
 * Return the number of free elements in the FIFO.
 *
 * Returns: Number of free elements.
 */

uint32_t fifo_num_free(Fifo *fifo);

/**
 * fifo_num_used:
 * @fifo: FIFO to check
 *
 * Return the number of used elements in the FIFO.
 *
 * Returns: Number of used elements.
 */

uint32_t fifo_num_used(Fifo *fifo);

extern const VMStateDescription vmstate_fifo;

#define VMSTATE_FIFO(_field, _state) {                               \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(Fifo),                                      \
    .vmsd       = &vmstate_fifo,                                     \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, Fifo),        \
}

#endif /* FIFO_H */
