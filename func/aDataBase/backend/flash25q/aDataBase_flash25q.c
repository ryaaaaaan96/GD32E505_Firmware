#include "aDataBase_flash25q.h"

static aStatus_t read_storage(void *context, uint32_t address, uint8_t *buffer,
                              uint32_t size, aTimeout_t timeout)
{
    return aDevFlash25qRead(context, address, buffer, size, timeout);
}

static aStatus_t write_storage(void *context, uint32_t address,
                               const uint8_t *buffer, uint32_t size,
                               aTimeout_t timeout)
{
    return aDevFlash25qWrite(context, address, buffer, size, timeout);
}

static aStatus_t erase_storage(void *context, uint32_t address, uint32_t size,
                               aTimeout_t timeout)
{
    return aDevFlash25qErase(context, address, size, timeout);
}

aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle)
{
    if (aDevFlash25qHandleIsValid(handle) != A_STATUS_OK)
        return A_STATUS_INVALID_PARAM;
    const aDataBaseStorage_t storage = {
        .context = handle,
        .capacity = aDevFlash25qGetSize(handle),
        .erase_block_size = 4096U,
        .read = read_storage,
        .write = write_storage,
        .erase = erase_storage,
    };
    return aDataBaseBindStorage(&storage);
}
