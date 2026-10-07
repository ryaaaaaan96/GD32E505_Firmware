#include "aDataBase_internal.h"
#include "aDatabase_flash_layout.h"
#include <limits.h>
#include <string.h>

/* 布局必须在编译时满足边界、对齐和不重叠要求。 */
_Static_assert(ADATABASE_FLASH_BLOCK_SIZE > 0U,
               "擦除块大小必须大于零");
_Static_assert((ADATABASE_FLASH_BLOCK_SIZE &
               (ADATABASE_FLASH_BLOCK_SIZE - 1U)) == 0U,
               "擦除块必须是二的整数次幂");
_Static_assert(ADATABASE_FLASH_CAPACITY_BYTES <= INT32_MAX,
               "FAL 地址必须能用有符号三十二位整数表示");
_Static_assert(ADATABASE_PART_PARAM_OFFSET <= ADATABASE_PART_LOG_OFFSET &&
               ADATABASE_PART_PARAM_SIZE <=
               ADATABASE_PART_LOG_OFFSET - ADATABASE_PART_PARAM_OFFSET,
               "参数分区和日志分区不得重叠");
_Static_assert(ADATABASE_PART_LOG_OFFSET <= ADATABASE_FLASH_CAPACITY_BYTES &&
               ADATABASE_PART_LOG_SIZE <= ADATABASE_FLASH_CAPACITY_BYTES -
               ADATABASE_PART_LOG_OFFSET, "日志分区超过介质容量");
_Static_assert(ADATABASE_PART_PARAM_OFFSET %
               ADATABASE_FLASH_BLOCK_SIZE == 0U &&
               ADATABASE_PART_PARAM_SIZE %
               ADATABASE_FLASH_BLOCK_SIZE == 0U &&
               ADATABASE_PART_LOG_OFFSET %
               ADATABASE_FLASH_BLOCK_SIZE == 0U &&
               ADATABASE_PART_LOG_SIZE %
               ADATABASE_FLASH_BLOCK_SIZE == 0U,
               "分区地址和长度必须与擦除块对齐");
_Static_assert(ADATABASE_PART_PARAM_SIZE /
               ADATABASE_FLASH_BLOCK_SIZE >= 2U &&
               ADATABASE_PART_LOG_SIZE /
               ADATABASE_FLASH_BLOCK_SIZE >= 2U,
               "每个数据库至少需要两个擦除块");

static aDataBaseStorage_t storage;
static aOSMutex_t module_mutex;
static aDataBaseInstance_t *instances;
static aTimepoint_t deadline;
static aStatus_t storage_error;
static aBool_t bound;
static aBool_t operating;

void aDataBaseStorageStructInit(aDataBaseStorage_t *config)
{
    if (config == NULL) return;
    *config = (aDataBaseStorage_t){
        .context = NULL, .capacity = 0U, .erase_block_size = 0U,
        .read = NULL, .write = NULL, .erase = NULL
    };
}

aStatus_t aDataBaseStorageError(void)
{
    if (storage_error == A_STATUS_OK &&
        aTimepointExpired(&deadline, aOSGetUptimeMs()))
        storage_error = A_STATUS_TIMEOUT;
    return storage_error;
}

void aDataBaseStorageFail(aStatus_t status)
{
    if (storage_error == A_STATUS_OK) storage_error = status;
}

aStatus_t aDataBaseOperationBegin(aTimeout_t timeout)
{
    aTimepoint_t operation_deadline;
    aStatus_t status;

    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    if (timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
        timeout.milliseconds == 0U) return A_STATUS_UNSUPPORTED;
    if (!bound) return A_STATUS_NOT_READY;
    operation_deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(module_mutex,
        aTimepointRemaining(&operation_deadline, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) return status;
    deadline = operation_deadline;
    storage_error = A_STATUS_OK;
    operating = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDataBaseOperationEnd(aStatus_t status)
{
    aStatus_t error = aDataBaseStorageError();
    aStatus_t unlock_status;

    operating = A_FALSE;
    unlock_status = aOSMutexUnlock(module_mutex);
    if (error != A_STATUS_OK) return error;
    return status == A_STATUS_OK ? unlock_status : status;
}

void aDataBaseInstanceAdd(aDataBaseInstance_t *instance)
{
    instance->next = instances;
    instances = instance;
}

void aDataBaseInstanceRemove(aDataBaseInstance_t *instance)
{
    aDataBaseInstance_t **link = &instances;

    while (*link != NULL && *link != instance) link = &(*link)->next;
    if (*link != NULL) *link = instance->next;
    instance->next = NULL;
}

aBool_t aDataBasePartitionIsUsed(const struct fal_partition *partition)
{
    const aDataBaseInstance_t *instance;

    for (instance = instances; instance != NULL; instance = instance->next)
        if (instance->partition == partition) return A_TRUE;
    return A_FALSE;
}

static aBool_t range_check(long offset, size_t size)
{
    if (!bound || !operating) {
        aDataBaseStorageFail(A_STATUS_NOT_READY);
        return A_FALSE;
    }
    if (aDataBaseStorageError() != A_STATUS_OK) return A_FALSE;
    if (offset < 0 || (size_t)offset > storage.capacity ||
        size > storage.capacity - (size_t)offset || size > INT_MAX) {
        aDataBaseStorageFail(A_STATUS_INVALID_PARAM);
        return A_FALSE;
    }
    return A_TRUE;
}

static int storage_read(long offset, uint8_t *buffer, size_t size)
{
    aStatus_t status;

    if (!range_check(offset, size)) return -1;
    if (size == 0U) return 0;
    if (buffer == NULL) {
        aDataBaseStorageFail(A_STATUS_INVALID_PARAM);
        return -1;
    }
    status = storage.read(storage.context, (uint32_t)offset, buffer,
        (uint32_t)size, aTimepointRemaining(&deadline, aOSGetUptimeMs()));
    aDataBaseStorageFail(status);
    return status == A_STATUS_OK ? (int)size : -1;
}

static int storage_write(long offset, const uint8_t *buffer, size_t size)
{
    aStatus_t status;

    if (!range_check(offset, size)) return -1;
    if (size == 0U) return 0;
    if (buffer == NULL) {
        aDataBaseStorageFail(A_STATUS_INVALID_PARAM);
        return -1;
    }
    status = storage.write(storage.context, (uint32_t)offset, buffer,
        (uint32_t)size, aTimepointRemaining(&deadline, aOSGetUptimeMs()));
    aDataBaseStorageFail(status);
    return status == A_STATUS_OK ? (int)size : -1;
}

static int storage_erase(long offset, size_t size)
{
    aStatus_t status;

    if (!range_check(offset, size)) return -1;
    if (size == 0U) return 0;
    if ((size_t)offset % storage.erase_block_size != 0U ||
        size % storage.erase_block_size != 0U) {
        aDataBaseStorageFail(A_STATUS_INVALID_PARAM);
        return -1;
    }
    status = storage.erase(storage.context, (uint32_t)offset, (uint32_t)size,
        aTimepointRemaining(&deadline, aOSGetUptimeMs()));
    aDataBaseStorageFail(status);
    return status == A_STATUS_OK ? (int)size : -1;
}

/* 官方 FAL 表引用此对象；实际外设由应用提前初始化并显式绑定。 */
struct fal_flash_dev aDataBaseFalFlash = {
    .name = ADATABASE_FLASH_DEVICE_NAME,
    .addr = 0U,
    .len = ADATABASE_FLASH_CAPACITY_BYTES,
    .blk_size = ADATABASE_FLASH_BLOCK_SIZE,
    .ops = { NULL, storage_read, storage_write, storage_erase },
    .write_gran = 1U
};

aStatus_t aDataBaseBindStorage(const aDataBaseStorage_t *config)
{
    aStatus_t status;

    if (config == NULL || config->read == NULL || config->write == NULL ||
        config->erase == NULL ||
        config->capacity != ADATABASE_FLASH_CAPACITY_BYTES ||
        config->erase_block_size != ADATABASE_FLASH_BLOCK_SIZE)
        return A_STATUS_INVALID_PARAM;
    if (bound) return A_STATUS_BUSY;
    status = aOSMutexCreate(&module_mutex);
    if (status != A_STATUS_OK) return status;
    storage = *config;
    bound = A_TRUE;
    if (fal_init() <= 0) {
        bound = A_FALSE;
        aOSMutexDestroy(&module_mutex);
        return A_STATUS_ERROR;
    }
    return A_STATUS_OK;
}

aStatus_t aDataBaseUnbindStorage(void)
{
    if (!bound) return A_STATUS_NOT_READY;
    if (instances != NULL) return A_STATUS_BUSY;
    bound = A_FALSE;
    aDataBaseStorageStructInit(&storage);
    aOSMutexDestroy(&module_mutex);
    return A_STATUS_OK;
}
