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

static aDataBaseStorage_t s_storage;
static aBool_t s_bound;

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

aStatus_t aDataBaseBindStorage(const aDataBaseStorage_t *storage)
{
    if (storage == NULL || storage->read == NULL || storage->write == NULL ||
        storage->erase == NULL || storage->capacity !=
            ADATABASE_FLASH_CAPACITY_BYTES ||
        storage->erase_block_size != ADATABASE_FLASH_BLOCK_SIZE) {
        return A_STATUS_INVALID_PARAM;
    }
    if (s_bound) return A_STATUS_BUSY;
    s_storage = *storage;
    s_flash_device.len = storage->capacity;
    s_bound = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDataBaseUnbindStorage(void)
{
    if (!s_bound) return A_STATUS_NOT_READY;
    s_bound = A_FALSE;
    memset(&s_storage, 0, sizeof(s_storage));
    s_flash_device.len = 0U;
    return A_STATUS_OK;
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
    /* Binding is explicit and owned by the application. */
}

const struct fal_partition *fal_partition_find(const char *name)
{
    if ((name == NULL) || (!s_bound)) return NULL;
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
        (!s_bound)) {
        return NULL;
    }
    return &s_flash_device;
}

int fal_partition_read(const struct fal_partition *part, uint32_t address,
                       uint8_t *buffer, size_t size)
{
    if ((!s_bound) || (buffer == NULL) ||
        !partition_range_is_valid(part, address, size) ||
        (size > UINT32_MAX)) {
        return -1;
    }
    return s_storage.read(s_storage.context, part->offset + address, buffer,
                            (uint32_t)size, FLASH_OPERATION_TIMEOUT) ==
                   A_STATUS_OK
               ? 0
               : -1;
}

int fal_partition_write(const struct fal_partition *part, uint32_t address,
                        const uint8_t *buffer, size_t size)
{
    if ((!s_bound) || (buffer == NULL) ||
        !partition_range_is_valid(part, address, size) ||
        (size > UINT32_MAX)) {
        return -1;
    }
    return s_storage.write(s_storage.context, part->offset + address, buffer,
                             (uint32_t)size,
                             FLASH_OPERATION_TIMEOUT) == A_STATUS_OK ? 0 : -1;
}

int fal_partition_erase(const struct fal_partition *part, uint32_t address,
                        size_t size)
{
    if ((!s_bound) || !partition_range_is_valid(part, address, size) ||
        (size > UINT32_MAX)) {
        return -1;
    }
    return s_storage.erase(s_storage.context, part->offset + address,
                             (uint32_t)size,
                             FLASH_OPERATION_TIMEOUT) == A_STATUS_OK ? 0 : -1;
}
