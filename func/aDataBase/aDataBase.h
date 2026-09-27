#ifndef ADATABASE_H
#define ADATABASE_H

#include "aLib.h"

typedef struct {
    void *context;
    size_t capacity;
    size_t erase_block_size;
    aStatus_t (*read)(void *context, uint32_t address, uint8_t *buffer,
                      uint32_t size, aTimeout_t timeout);
    aStatus_t (*write)(void *context, uint32_t address, const uint8_t *buffer,
                       uint32_t size, aTimeout_t timeout);
    aStatus_t (*erase)(void *context, uint32_t address, uint32_t size,
                       aTimeout_t timeout);
} aDataBaseStorage_t;

/* Copy the storage operations, borrow context. Bind before FlashDB init;
 * close all databases and quiesce all users before unbinding. Startup/lifecycle
 * calls are externally serialized. Geometry must match the FAL layout. */
aStatus_t aDataBaseBindStorage(const aDataBaseStorage_t *storage);
aStatus_t aDataBaseUnbindStorage(void);

#endif
