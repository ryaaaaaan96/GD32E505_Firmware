#include "aDataBase_flash25q.h"

static aStatus_t flash_read(void *context, uint32_t address,
                            uint8_t *buffer, uint32_t size, aTimeout_t timeout)
{
    aDevFlash25qReadRequest_t request;

    aDevFlash25qReadRequestStructInit(&request);
    request.address = address;
    request.data = buffer;
    request.size = size;
    request.timeout = timeout;
    return aDevFlash25qRead(context, &request);
}

static aStatus_t flash_write(void *context, uint32_t address,
                             const uint8_t *buffer, uint32_t size,
                             aTimeout_t timeout)
{
    aDevFlash25qWriteRequest_t request;

    aDevFlash25qWriteRequestStructInit(&request);
    request.address = address;
    request.data = buffer;
    request.size = size;
    request.timeout = timeout;
    return aDevFlash25qWrite(context, &request);
}

static aStatus_t flash_erase(void *context, uint32_t address,
                             uint32_t size, aTimeout_t timeout)
{
    aDevFlash25qEraseRequest_t request;

    aDevFlash25qEraseRequestStructInit(&request);
    request.address = address;
    request.size = size;
    request.timeout = timeout;
    return aDevFlash25qErase(context, &request);
}

aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle)
{
    aDevFlash25qInfo_t info;
    aDataBaseStorage_t storage;
    aStatus_t status;

    status = aDevFlash25qGetInfo(handle, &info);
    if (status != A_STATUS_OK) return status;
    aDataBaseStorageStructInit(&storage);
    storage.context = handle;
    storage.capacity = info.capacity;
    storage.erase_block_size = info.erase_size;
    storage.read = flash_read;
    storage.write = flash_write;
    storage.erase = flash_erase;
    return aDataBaseBindStorage(&storage);
}
