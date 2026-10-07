#include "aOS.h"
#include <assert.h>
#include <stdlib.h>

uint32_t database_tick;
size_t database_allocations;
int database_fail_lock;

void *aOSAlloc(size_t size)
{
    void *pointer = malloc(size);
    if (pointer != NULL) ++database_allocations;
    return pointer;
}
void aOSFree(void *pointer)
{
    if (pointer != NULL) { --database_allocations; free(pointer); }
}
aStatus_t aOSMutexCreate(aOSMutex_t *mutex)
{
    *mutex = aOSAlloc(sizeof(int));
    if (*mutex == NULL) return A_STATUS_NO_MEMORY;
    *(int *)*mutex = 0;
    return A_STATUS_OK;
}
void aOSMutexDestroy(aOSMutex_t *mutex)
{
    assert(*mutex != NULL && *(int *)*mutex == 0);
    aOSFree(*mutex);
    *mutex = NULL;
}
aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    (void)timeout;
    if (database_fail_lock) return A_STATUS_TIMEOUT;
    assert(mutex != NULL && *(int *)mutex == 0);
    *(int *)mutex = 1;
    return A_STATUS_OK;
}
aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    assert(mutex != NULL && *(int *)mutex == 1);
    *(int *)mutex = 0;
    return A_STATUS_OK;
}
uint32_t aOSGetUptimeMs(void) { return database_tick; }
