#include "fal.h"
#include "aDataBase.h"
#include "aDatabase_flash_layout.h"

#include <string.h>

#define FLASH_OPERATION_TIMEOUT \
    A_TIMEOUT_MS(ADATABASE_FLASH_OPERATION_TIMEOUT_MS)

_Static_assert((ADATABASE_PART_PARAM_OFFSET + ADATABASE_PART_PARAM_SIZE) <=
                   ADATABASE_PART_LOG_OFFSET,
               "FAL param and log partitions must not overlap");
_Static_assert((ADATABASE_PART_LOG_OFFSET + ADATABASE_PART_LOG_SIZE) <=
                   ADATABASE_FLASH_CAPACITY_BYTES,
               "FAL partition layout exceeds configured flash capacity");
_Static_assert((ADATABASE_PART_PARAM_OFFSET % ADATABASE_FLASH_BLOCK_SIZE) == 0U,
               "FAL param partition must be block aligned");
_Static_assert((ADATABASE_PART_LOG_OFFSET % ADATABASE_FLASH_BLOCK_SIZE) == 0U,
               "FAL log partition must be block aligned");

static aDevFlash25qHandle_t *s_flash_handle;

static struct fal_flash_dev s_flash_device = {
    .name = ADATABASE_FLASH_DEVICE_NAME,
    .addr = 0U,
    .len = 0U,
    .blk_size = ADATABASE_FLASH_BLOCK_SIZE,
};

static const struct fal_partition s_partitions[] = {
    {
        .name = ADATABASE_PART_PARAM_NAME,
        .flash_name = ADATABASE_FLASH_DEVICE_NAME,
        .flash_dev = &s_flash_device,
        .offset = ADATABASE_PART_PARAM_OFFSET,
        .len = ADATABASE_PART_PARAM_SIZE,
    },
    {
        .name = ADATABASE_PART_LOG_NAME,
        .flash_name = ADATABASE_FLASH_DEVICE_NAME,
        .flash_dev = &s_flash_device,
        .offset = ADATABASE_PART_LOG_OFFSET,
        .len = ADATABASE_PART_LOG_SIZE,
    },
};

aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle)
{
    if ((handle == NULL) ||
        (aDevFlash25qHandleIsValid(handle) != A_STATUS_OK) ||
        (aDevFlash25qGetSize(handle) != ADATABASE_FLASH_CAPACITY_BYTES)) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((s_flash_handle != NULL) && (s_flash_handle != handle)) {
        return A_STATUS_BUSY;
    }
    s_flash_handle = handle;
    s_flash_device.len = aDevFlash25qGetSize(handle);
    return A_STATUS_OK;
}

aStatus_t aDataBaseUnbindFlash25q(aDevFlash25qHandle_t *handle)
{
    if ((handle == NULL) || (s_flash_handle != handle)) {
        return A_STATUS_INVALID_PARAM;
    }
    s_flash_handle = NULL;
    s_flash_device.len = 0U;
    return A_STATUS_OK;
}

static aDevFlash25qHandle_t *flash_handle(void)
{
    if ((s_flash_handle == NULL) ||
        (aDevFlash25qHandleIsValid(s_flash_handle) != A_STATUS_OK) ||
        (aDevFlash25qGetSize(s_flash_handle) !=
         ADATABASE_FLASH_CAPACITY_BYTES)) {
        return NULL;
    }
    return s_flash_handle;
}

static int partition_range_is_valid(const struct fal_partition *part,
                                    uint32_t address, size_t size)
{
    if ((part == NULL) || (part->flash_dev != &s_flash_device) ||
        (address > part->len) || (size > (part->len - address)) ||
        (part->offset > s_flash_device.len) ||
        (part->len > (s_flash_device.len - part->offset))) {
        return 0;
    }
    return 1;
}

void fal_init(void)
{
    (void)flash_handle();
}

const struct fal_partition *fal_partition_find(const char *name)
{
    if ((name == NULL) || (flash_handle() == NULL)) return NULL;
    for (size_t i = 0U; i < sizeof(s_partitions) / sizeof(s_partitions[0]);
         ++i) {
        if ((strcmp(name, s_partitions[i].name) == 0) &&
            partition_range_is_valid(&s_partitions[i], 0U, 0U)) {
            return &s_partitions[i];
        }
    }
    return NULL;
}

const struct fal_flash_dev *fal_flash_device_find(const char *name)
{
    if ((name == NULL) || (strcmp(name, s_flash_device.name) != 0) ||
        (flash_handle() == NULL)) {
        return NULL;
    }
    return &s_flash_device;
}

int fal_partition_read(const struct fal_partition *part, uint32_t address,
                       uint8_t *buffer, size_t size)
{
    aDevFlash25qHandle_t *handle = flash_handle();
    if ((handle == NULL) || (buffer == NULL) ||
        !partition_range_is_valid(part, address, size) ||
        (size > UINT32_MAX)) {
        return -1;
    }
    return aDevFlash25qRead(handle, part->offset + address, buffer,
                            (uint32_t)size, FLASH_OPERATION_TIMEOUT) ==
                   A_STATUS_OK
               ? 0
               : -1;
}

int fal_partition_write(const struct fal_partition *part, uint32_t address,
                        const uint8_t *buffer, size_t size)
{
    aDevFlash25qHandle_t *handle = flash_handle();
    if ((handle == NULL) || (buffer == NULL) ||
        !partition_range_is_valid(part, address, size) ||
        (size > UINT32_MAX)) {
        return -1;
    }
    return aDevFlash25qWrite(handle, part->offset + address, buffer,
                             (uint32_t)size,
                             FLASH_OPERATION_TIMEOUT) == A_STATUS_OK ? 0 : -1;
}

int fal_partition_erase(const struct fal_partition *part, uint32_t address,
                        size_t size)
{
    aDevFlash25qHandle_t *handle = flash_handle();
    if ((handle == NULL) || !partition_range_is_valid(part, address, size) ||
        (size > UINT32_MAX)) {
        return -1;
    }
    return aDevFlash25qErase(handle, part->offset + address,
                             (uint32_t)size,
                             FLASH_OPERATION_TIMEOUT) == A_STATUS_OK ? 0 : -1;
}
