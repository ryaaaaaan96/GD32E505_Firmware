#include "aDataBase_internal.h"
#include "aDatabase_flash_layout.h"
#include <assert.h>
#include <string.h>

extern size_t database_allocations;
static unsigned calls;
static aStatus_t storage_read(void *context, uint32_t address,
                              uint8_t *buffer, uint32_t size,
                              aTimeout_t timeout)
{
    assert(context == &calls && address == ADATABASE_PART_PARAM_OFFSET);
    assert(timeout.milliseconds == 5000U);
    ++calls;
    memset(buffer, 0x5A, size);
    return A_STATUS_OK;
}
static aStatus_t storage_write(void *context, uint32_t address,
                               const uint8_t *buffer, uint32_t size,
                               aTimeout_t timeout)
{
    (void)timeout;
    assert(context == &calls && address == ADATABASE_PART_PARAM_OFFSET);
    assert(size == 4U && buffer != NULL);
    ++calls;
    return A_STATUS_OK;
}
static aStatus_t storage_erase(void *context, uint32_t address,
                               uint32_t size, aTimeout_t timeout)
{
    (void)timeout;
    assert(context == &calls && address == ADATABASE_PART_PARAM_OFFSET);
    assert(size == 4096U);
    ++calls;
    return A_STATUS_OK;
}
int main(void)
{
    aDataBaseStorage_t config;
    const struct fal_partition *part;
    uint8_t buffer[4];

    aDataBaseStorageStructInit(&config);
    config.context = &calls;
    config.capacity = ADATABASE_FLASH_CAPACITY_BYTES;
    config.erase_block_size = ADATABASE_FLASH_BLOCK_SIZE;
    config.read = storage_read;
    config.write = storage_write;
    config.erase = storage_erase;
    assert(aDataBaseBindStorage(NULL) == A_STATUS_INVALID_PARAM);
    assert(aDataBaseBindStorage(&config) == A_STATUS_OK);
    assert(aDataBaseBindStorage(&config) == A_STATUS_BUSY);
    /* 描述复制保存，context 保持借用；FAL 返回实际处理的字节数。 */
    config.read = NULL;
    part = fal_partition_find(ADATABASE_PART_PARAM_NAME);
    assert(part != NULL);
    assert(aDataBaseOperationBegin(A_TIMEOUT_MS(5000U)) == A_STATUS_OK);
    assert(fal_partition_read(part, 0U, buffer, sizeof(buffer)) == 4);
    assert(buffer[0] == 0x5A);
    assert(fal_partition_write(part, 0U, buffer, sizeof(buffer)) == 4);
    assert(fal_partition_erase(part, 0U, 4096U) == 4096);
    assert(fal_partition_read(part, part->len, buffer, 1U) == -1);
    assert(aDataBaseOperationEnd(A_STATUS_OK) == A_STATUS_OK);
    assert(calls == 3U);
    assert(aDataBaseUnbindStorage() == A_STATUS_OK);
    assert(fal_partition_read(part, 0U, buffer, 4U) == -1);
    assert(aDataBaseUnbindStorage() == A_STATUS_NOT_READY);
    assert(aDataBaseBindStorage(&config) == A_STATUS_INVALID_PARAM);
    assert(database_allocations == 0U);
    return 0;
}
