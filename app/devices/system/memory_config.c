#include "memory_config.h"
#include "flash_device.h"
#include "aMemory_layout.h"

static aStatus_t memory_read(void *context,
                             const aMemoryReadRequest_t *request)
{
    aDevFlash25qReadRequest_t flash_request;

    (void)context;
    if (request->address > UINT32_MAX) return A_STATUS_INVALID_PARAM;
    aDevFlash25qReadRequestStructInit(&flash_request);
    flash_request.address = (uint32_t)request->address;
    flash_request.data = request->data;
    flash_request.size = request->size;
    flash_request.timeout = request->timeout;
    return appSystemFlashRead(&flash_request);
}

static aStatus_t memory_write(void *context,
                              const aMemoryWriteRequest_t *request)
{
    aDevFlash25qWriteRequest_t flash_request;

    (void)context;
    if (request->address > UINT32_MAX) return A_STATUS_INVALID_PARAM;
    aDevFlash25qWriteRequestStructInit(&flash_request);
    flash_request.address = (uint32_t)request->address;
    flash_request.data = request->data;
    flash_request.size = request->size;
    flash_request.timeout = request->timeout;
    return appSystemFlashWrite(&flash_request);
}

static aStatus_t memory_erase(void *context,
                              const aMemoryEraseRequest_t *request)
{
    aDevFlash25qEraseRequest_t flash_request;

    (void)context;
    if (request->address > UINT32_MAX) return A_STATUS_INVALID_PARAM;
    aDevFlash25qEraseRequestStructInit(&flash_request);
    flash_request.address = (uint32_t)request->address;
    flash_request.size = request->size;
    flash_request.timeout = request->timeout;
    return appSystemFlashErase(&flash_request);
}

static const aMemoryOps_t flash_ops = {
    .read = memory_read, .write = memory_write, .erase = memory_erase
};

AMEMORY_DEVICE_DEFINE(flash_device,
    .name = AMEMORY_FLASH_DEVICE_NAME,
    .context = NULL,
    .ops = &flash_ops,
    .geometry = {
        .capacity = AMEMORY_FLASH_CAPACITY_BYTES,
        .read_granularity = 1U, .write_granularity = 1U,
        .erase_granularity = AMEMORY_FLASH_BLOCK_SIZE,
        .program_bits = 1U, .erased_value = 0xFFU
    }
);

AMEMORY_PARTITION_DEFINE(parameters, AMEMORY_PART_PARAM_NAME, flash_device,
    AMEMORY_PART_PARAM_OFFSET, AMEMORY_PART_PARAM_SIZE, AMEMORY_ACCESS_ALL);
AMEMORY_PARTITION_DEFINE(records, AMEMORY_PART_LOG_NAME, flash_device,
    AMEMORY_PART_LOG_OFFSET, AMEMORY_PART_LOG_SIZE, AMEMORY_ACCESS_ALL);

static const aMemoryDevice_t *const devices[] = { &flash_device };
static const aMemoryPartition_t *const partitions[] = {
    &parameters, &records
};

aStatus_t appSystemMemoryInit(void)
{
    aDevFlash25qInfo_t info;
    aMemoryConfig_t config;
    aStatus_t status;

    status = appSystemFlashGetInfo(&info);
    if (status != A_STATUS_OK) return status;
    if (info.capacity != flash_device.geometry.capacity ||
        info.erase_size != flash_device.geometry.erase_granularity)
        return A_STATUS_INVALID_PARAM;
    aMemoryConfigStructInit(&config);
    config.devices = devices;
    config.device_count = sizeof(devices) / sizeof(devices[0]);
    config.partitions = partitions;
    config.partition_count = sizeof(partitions) / sizeof(partitions[0]);
    return aMemoryInit(&config);
}
