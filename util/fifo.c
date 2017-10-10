/*
 * Generic FIFO component, implemented as a circular buffer.
 *
 * Copyright (c) 2012 Peter A. G. Crosthwaite
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/fifo.h"

static inline void fifo_createxx(Fifo *fifo, uint32_t capacity, int bytes)
{
    fifo->width = bytes;
    fifo->capacity = capacity;
    fifo->buffer_size = capacity * fifo->width;
    fifo->data = g_new(uint8_t, fifo->buffer_size);
    fifo->head = 0;
    fifo->num = 0;
}

void fifo_create8(Fifo *fifo, uint32_t capacity)
{
    fifo_createxx(fifo, capacity, 1);
}

void fifo_create16(Fifo *fifo, uint32_t capacity)
{
    fifo_createxx(fifo, capacity, 2);
}

void fifo_create32(Fifo *fifo, uint32_t capacity)
{
    fifo_createxx(fifo, capacity, 4);
}

void fifo_create64(Fifo *fifo, uint32_t capacity)
{
    fifo_createxx(fifo, capacity, 8);
}

void fifo_destroy(Fifo *fifo)
{
    g_free(fifo->data);
}

#define FIFO_PUSH_FN(n)                                                     \
void fifo_push ## n(Fifo *fifo, uint ## n ## _t data)                       \
{                                                                           \
    uint32_t next_idx = (fifo->head + fifo->num) % fifo->capacity;          \
                                                                            \
    assert(n == fifo->width * 8);                                           \
    if (fifo->num == fifo->capacity) {                                      \
        abort();                                                            \
    }                                                                       \
    ((uint ## n ## _t *)fifo->data)[next_idx] = data;                       \
    fifo->num++;                                                            \
}

FIFO_PUSH_FN(8)
FIFO_PUSH_FN(16)
FIFO_PUSH_FN(32)
FIFO_PUSH_FN(64)

void fifo_push_all(Fifo *fifo, const void *data, uint32_t num)
{
    uint32_t start, avail;

    if (fifo->num + num > fifo->capacity) {
        abort();
    }

    start = (fifo->head + fifo->num) % fifo->capacity;

    if (start + num <= fifo->capacity) {
        memcpy(&fifo->data[start * fifo->width], data, num * fifo->width);
    } else {
        avail = fifo->capacity - start;
        memcpy(&fifo->data[start * fifo->width], data, avail * fifo->width);
        memcpy(&fifo->data[0], data + avail * fifo->width,
               (num - avail) * fifo->width);
    }

    fifo->num += num;
}

#define FIFO_POP_FN(n)                                                      \
uint ## n ## _t fifo_pop ## n(Fifo *fifo)                                   \
{                                                                           \
    uint32_t next_idx;                                                      \
                                                                            \
    assert(n == fifo->width * 8);                                           \
    if (fifo->num == 0) {                                                   \
        abort();                                                            \
    }                                                                       \
    next_idx = fifo->head++;                                                \
    fifo->head %= fifo->capacity;                                           \
    fifo->num--;                                                            \
    return ((uint ## n ## _t *)fifo->data)[next_idx];                       \
}

FIFO_POP_FN(8)
FIFO_POP_FN(16)
FIFO_POP_FN(32)
FIFO_POP_FN(64)

const void *fifo_pop_buf(Fifo *fifo, uint32_t max, uint32_t *num)
{
    void *ret;

    if (max == 0 || max > fifo->num) {
        abort();
    }
    *num = MIN(fifo->capacity - fifo->head, max);
    ret = &fifo->data[fifo->head * fifo->width];
    fifo->head += *num;
    fifo->head %= fifo->capacity;
    fifo->num -= *num;
    return ret;
}

void fifo_reset(Fifo *fifo)
{
    fifo->num = 0;
    fifo->head = 0;
}

bool fifo_is_empty(Fifo *fifo)
{
    return (fifo->num == 0);
}

bool fifo_is_full(Fifo *fifo)
{
    return (fifo->num == fifo->capacity);
}

uint32_t fifo_num_free(Fifo *fifo)
{
    return fifo->capacity - fifo->num;
}

uint32_t fifo_num_used(Fifo *fifo)
{
    return fifo->num;
}

const VMStateDescription vmstate_fifo = {
    .name = "Fifo8",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_VBUFFER_UINT32(data, Fifo, 1, NULL, capacity),
        VMSTATE_UINT32(head, Fifo),
        VMSTATE_UINT32(num, Fifo),
        VMSTATE_END_OF_LIST()
    }
};
