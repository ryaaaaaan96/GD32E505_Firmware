#include "aOS.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

static uint32_t uptime;
static _Thread_local aErrno_t last_errno;
static unsigned allocations;
static aBool_t fail_alloc;
static aBool_t fail_unlock;

aOSFaultRecord_t g_aOSFaultRecord;

void aOSRecordFault(aOSFaultCode_t code, aStatus_t status, const char *context)
{
    g_aOSFaultRecord.status = status;
    g_aOSFaultRecord.context = context;
    g_aOSFaultRecord.code = code;
}

uint32_t aOSGetUptimeMs(void) { return uptime; }
void testAdvanceTime(uint32_t milliseconds) { uptime += milliseconds; }
void testSetTime(uint32_t milliseconds) { uptime = milliseconds; }
void testFailAllocation(aBool_t fail) { fail_alloc = fail; }
void testFailMutexUnlock(aBool_t fail) { fail_unlock = fail; }
unsigned testAllocations(void) { return allocations; }
aErrno_t aOSGetErrno(void) { return last_errno; }
aSSize_t aOSFailWithStatus(aStatus_t status)
{
    last_errno = aStatusToErrno(status);
    return -1;
}

aSSize_t aOSFailWithTimeout(aTimeout_t timeout)
{
    return aOSFailWithStatus(timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
        timeout.milliseconds == 0U ? A_STATUS_BUSY : A_STATUS_TIMEOUT);
}

void *aOSAlloc(size_t size)
{
    void *memory;
    if (fail_alloc) return NULL;
    memory = malloc(size);
    if (memory != NULL) allocations++;
    return memory;
}

void aOSFree(void *memory)
{
    if (memory != NULL) {
        assert(allocations != 0U);
        allocations--;
        free(memory);
    }
}

aStatus_t aOSMutexCreate(aOSMutex_t *mutex)
{
    pthread_mutex_t *created = malloc(sizeof(*created));
    if (created == NULL) return A_STATUS_NO_MEMORY;
    assert(pthread_mutex_init(created, NULL) == 0);
    *mutex = created;
    return A_STATUS_OK;
}

void aOSMutexDestroy(aOSMutex_t *mutex)
{
    if (*mutex != NULL) {
        assert(pthread_mutex_destroy(*mutex) == 0);
        free(*mutex);
        *mutex = NULL;
    }
}

aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    int status = pthread_mutex_trylock(mutex);
    if (status == EBUSY) {
        return timeout.milliseconds == 0U ? A_STATUS_BUSY : A_STATUS_TIMEOUT;
    }
    assert(status == 0);
    return A_STATUS_OK;
}

aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    aStatus_t status = A_STATUS_INVALID_PARAM;
    if (mutex != NULL) {
        assert(pthread_mutex_unlock(mutex) == 0);
        status = fail_unlock ? A_STATUS_ERROR : A_STATUS_OK;
    }
    if (status != A_STATUS_OK) {
        aOSRecordFault(AOS_FAULT_MUTEX_UNLOCK, status, "aOSMutexUnlock");
    }
    return status;
}
