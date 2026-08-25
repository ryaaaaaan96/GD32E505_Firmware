#include "fal.h"
#include "aDev_flash25q.h"

#include <string.h>

#define FLASH_DEVICE_INDEX 0U
#define FLASH_DEVICE_NAME "flash25"
#define FLASH_BLOCK_SIZE 4096U

#define PART_PARAM_OFFSET 0x00100000U
#define PART_PARAM_SIZE   0x00020000U
#define PART_LOG_OFFSET   0x00120000U
#define PART_LOG_SIZE     0x00080000U

static struct fal_flash_dev s_flash_device = {
    .name = FLASH_DEVICE_NAME,
    .addr = 0U,
    .len = 0U,
    .blk_size = FLASH_BLOCK_SIZE,
};

static const struct fal_partition s_partitions[] = {
    {
        .name = "param",
        .flash_name = FLASH_DEVICE_NAME,
        .flash_dev = &s_flash_device,
        .offset = PART_PARAM_OFFSET,
        .len = PART_PARAM_SIZE,
    },
    {
        .name = "log",
        .flash_name = FLASH_DEVICE_NAME,
        .flash_dev = &s_flash_device,
        .offset = PART_LOG_OFFSET,
        .len = PART_LOG_SIZE,
    },
};

static aDevFlash25qHandle_t *flash_handle(void)
{
    aDevFlash25qHandle_t *handle =
        aDevFlash25qGetDevice(FLASH_DEVICE_INDEX);
    if ((handle == NULL) ||
        (aDevFlash25qHandleIsValid(handle) != A_STATUS_OK)) {
        return NULL;
    }
    s_flash_device.len = aDevFlash25qGetSize(handle);
    return handle;
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
                            (uint32_t)size) == A_STATUS_OK ? 0 : -1;
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
                             (uint32_t)size) == A_STATUS_OK ? 0 : -1;
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
                             (uint32_t)size) == A_STATUS_OK ? 0 : -1;
}
