/* 基准专用 POSIX 支持层：真实时钟、真实互斥锁，不冒充 aOS Linux 后端。
 * 单线程工作负载；内存计数为申请字节，不包含 malloc 自身管理开销。
 */
#define _POSIX_C_SOURCE 200809L
#include "host_port.h"
#include "aOS.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

typedef union {
    max_align_t alignment;
    size_t size;
} allocation_t;
static size_t live;
static size_t peak;
static _Thread_local aErrno_t last_errno;

static uint64_t nanoseconds(void)
{
    struct timespec time;
    int status = clock_gettime(CLOCK_MONOTONIC, &time);
    assert(status == 0);
    return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_nsec;
}
uint32_t benchClock(void) { return (uint32_t)nanoseconds(); }
uint32_t aOSGetUptimeMs(void)
{
    return (uint32_t)(nanoseconds() / 1000000ULL);
}
void aOSDelayMs(uint32_t ms)
{
    struct timespec delay = {.tv_sec = ms / 1000U,
        .tv_nsec = (long)(ms % 1000U) * 1000000L};
    while (nanosleep(&delay, &delay) != 0) assert(errno == EINTR);
}
size_t benchMemoryLive(void) { return live; }
size_t benchMemoryPeak(void) { return peak; }
void benchMemoryResetPeak(void) { peak = live; }

void *aOSAlloc(size_t size)
{
    allocation_t *memory = malloc(sizeof(*memory) + size);
    if (memory == NULL) return NULL;
    memory->size = size;
    live += size;
    if (live > peak) peak = live;
    return memory + 1;
}
void aOSFree(void *pointer)
{
    if (pointer == NULL) return;
    allocation_t *memory = (allocation_t *)pointer - 1;
    assert(live >= memory->size);
    live -= memory->size;
    free(memory);
}
aStatus_t aOSMutexCreate(aOSMutex_t *out)
{
    pthread_mutex_t *mutex = aOSAlloc(sizeof(*mutex));
    if (mutex == NULL) return A_STATUS_NO_MEMORY;
    if (pthread_mutex_init(mutex, NULL) != 0) {
        aOSFree(mutex);
        return A_STATUS_ERROR;
    }
    *out = mutex;
    return A_STATUS_OK;
}
void aOSMutexDestroy(aOSMutex_t *mutex)
{
    if (*mutex == NULL) return;
    int status = pthread_mutex_destroy(*mutex);
    assert(status == 0);
    aOSFree(*mutex);
    *mutex = NULL;
}
aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    /* 基准只请求 NO_WAIT；不使用把忙等伪装成有限等待的测试桩。 */
    assert(timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
           timeout.milliseconds == 0U);
    int status = pthread_mutex_trylock(mutex);
    return status == 0 ? A_STATUS_OK :
        status == EBUSY ? A_STATUS_BUSY : A_STATUS_ERROR;
}
aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    return pthread_mutex_unlock(mutex) == 0 ? A_STATUS_OK : A_STATUS_ERROR;
}
aErrno_t aOSGetErrno(void) { return last_errno; }
aSSize_t aOSFailWithStatus(aStatus_t status)
{
    last_errno = aStatusToErrno(status);
    return -1;
}
aSSize_t aOSFailWithTimeout(aTimeout_t timeout)
{
    return aOSFailWithStatus(timeout.milliseconds == 0U ?
        A_STATUS_BUSY : A_STATUS_TIMEOUT);
}
