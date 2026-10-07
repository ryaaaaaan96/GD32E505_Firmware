#include "aDataBase_internal.h"

static aOSMutex_t module_mutex;
static aDataBaseInstance_t *instances;
static aTimepoint_t deadline;
static aStatus_t storage_error;
static aBool_t ready;
static aBool_t operating;

aStatus_t aDataBaseStorageError(void)
{
    if (!operating) return A_STATUS_NOT_READY;
    if (storage_error == A_STATUS_OK &&
        aTimepointExpired(&deadline, aOSGetUptimeMs()))
        storage_error = A_STATUS_TIMEOUT;
    return storage_error;
}

void aDataBaseStorageFail(aStatus_t status)
{
    if (storage_error == A_STATUS_OK) storage_error = status;
}

aTimeout_t aDataBaseStorageTimeout(void)
{
    return aTimepointRemaining(&deadline, aOSGetUptimeMs());
}

aStatus_t aDataBaseOperationBegin(aTimeout_t timeout)
{
    aTimepoint_t operation_deadline;
    aStatus_t status;

    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    if (timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
        timeout.milliseconds == 0U) return A_STATUS_UNSUPPORTED;
    if (!ready) return A_STATUS_NOT_READY;
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

aBool_t aDataBasePartitionIsUsed(const aMemoryHandle_t *partition)
{
    const aDataBaseInstance_t *instance;

    for (instance = instances; instance != NULL; instance = instance->next)
        if (instance->partition == partition) return A_TRUE;
    return A_FALSE;
}

aStatus_t aDataBaseInit(void)
{
    aStatus_t status;

    if (ready) return A_STATUS_BUSY;
    status = aMemoryRetain();
    if (status != A_STATUS_OK) return status;
    status = aOSMutexCreate(&module_mutex);
    if (status != A_STATUS_OK) {
        (void)aMemoryRelease();
        return status;
    }
    ready = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDataBaseDeInit(void)
{
    if (!ready) return A_STATUS_NOT_READY;
    if (instances != NULL) return A_STATUS_BUSY;
    ready = A_FALSE;
    aOSMutexDestroy(&module_mutex);
    return aMemoryRelease();
}
