#ifndef AFIFO_H
#define AFIFO_H
#include "aStatus.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Caller-owned fixed-capacity storage, value-copy elements, no allocation.
 * Caller serializes ALL operations. Pointed-to payloads are never owned here. */
typedef struct {
    unsigned char *storage;
    size_t element_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
} aFifo_t;

static inline aStatus_t aFifoInit(aFifo_t *fifo, void *storage,
                                 size_t capacity, size_t element_size)
{
    if (fifo == NULL || storage == NULL || capacity == 0U ||
        element_size == 0U || capacity > SIZE_MAX / element_size)
        return A_STATUS_INVALID_PARAM;
    *fifo = (aFifo_t) { .storage = storage, .capacity = capacity,
                       .element_size = element_size };
    return A_STATUS_OK;
}
static inline aStatus_t aFifoPush(aFifo_t *fifo, const void *element)
{
    if (fifo == NULL || element == NULL || fifo->storage == NULL)
        return A_STATUS_INVALID_PARAM;
    if (fifo->count == fifo->capacity) return A_STATUS_BUSY;
    memcpy(fifo->storage + fifo->head * fifo->element_size, element, fifo->element_size);
    fifo->head = (fifo->head + 1U) % fifo->capacity;
    ++fifo->count;
    return A_STATUS_OK;
}
static inline aStatus_t aFifoPeek(const aFifo_t *fifo, void *element)
{
    if (fifo == NULL || element == NULL || fifo->storage == NULL)
        return A_STATUS_INVALID_PARAM;
    if (fifo->count == 0U) return A_STATUS_NOT_READY;
    memcpy(element, fifo->storage + fifo->tail * fifo->element_size, fifo->element_size);
    return A_STATUS_OK;
}
static inline aStatus_t aFifoPop(aFifo_t *fifo, void *element)
{
    aStatus_t status = aFifoPeek(fifo, element);
    if (status != A_STATUS_OK) return status;
    fifo->tail = (fifo->tail + 1U) % fifo->capacity;
    --fifo->count;
    return A_STATUS_OK;
}
static inline size_t aFifoCount(const aFifo_t *fifo)
{
    return fifo != NULL ? fifo->count : 0U;
}
#endif
