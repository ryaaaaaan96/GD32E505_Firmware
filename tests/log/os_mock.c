#include "os_mock.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

static aBool_t fail_create;
static atomic_uint mutex_count;
static atomic_uint uptime;
static atomic_int next_lock_status;
static _Thread_local aErrno_t last_errno;

void testFailMutexCreate(aBool_t fail) { fail_create = fail; }
void testFailNextLock(aStatus_t status)
{
    atomic_store(&next_lock_status, (int)status);
}
unsigned testMutexCount(void) { return atomic_load(&mutex_count); }
void testSetUptime(uint32_t value) { atomic_store(&uptime, value); }
uint32_t aOSGetUptimeMs(void) { return atomic_load(&uptime); }
aErrno_t aOSGetErrno(void) { return last_errno; }
aSSize_t aOSFailWithStatus(aStatus_t status)
{
    last_errno = aStatusToErrno(status);
    return -1;
}

aStatus_t aOSMutexCreate(aOSMutex_t *mutex)
{
    pthread_mutex_t *created;

    assert(mutex != NULL && *mutex == NULL);
    if (fail_create) return A_STATUS_NO_MEMORY;
    created = malloc(sizeof(*created));
    assert(created != NULL && pthread_mutex_init(created, NULL) == 0);
    *mutex = created;
    (void)atomic_fetch_add(&mutex_count, 1U);
    return A_STATUS_OK;
}

void aOSMutexDestroy(aOSMutex_t *mutex)
{
    if (*mutex == NULL) return;
    assert(pthread_mutex_destroy(*mutex) == 0);
    free(*mutex);
    *mutex = NULL;
    (void)atomic_fetch_sub(&mutex_count, 1U);
}

aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    int injected = atomic_exchange(&next_lock_status, A_STATUS_OK);
    int result;

    assert(mutex != NULL && aTimeoutIsValid(timeout));
    if (injected != A_STATUS_OK) return (aStatus_t)injected;
    result = timeout.type == A_TIMEOUT_TYPE_FOREVER
             ? pthread_mutex_lock(mutex) : pthread_mutex_trylock(mutex);
    if (result == EBUSY)
        return timeout.milliseconds == 0U
               ? A_STATUS_BUSY : A_STATUS_TIMEOUT;
    assert(result == 0);
    return A_STATUS_OK;
}

aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    assert(mutex != NULL && pthread_mutex_unlock(mutex) == 0);
    return A_STATUS_OK;
}
