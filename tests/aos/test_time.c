/* Finite UINT32_MAX is never an infinite lock wait; wrap preserves budgets. */
#include <assert.h>
#include <stdio.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"
static TickType_t now;
static unsigned waits;
static aBool_t forever;
TickType_t xTaskGetTickCount(void) { return now; }
BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    assert(sem);
    ++waits;
    if (forever) { assert(ticks == portMAX_DELAY); return pdTRUE; }
    assert(ticks != portMAX_DELAY);
    now += ticks;
    return pdFALSE;
}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks)
{ return xSemaphoreTake(sem, ticks); }
int main(void)
{
    now = UINT32_MAX - 10U;
    assert(aOSMutexLock(&now, A_TIMEOUT_MS(20)) == A_STATUS_TIMEOUT);
    assert(now == 9U && waits == 1);
    now = 100; waits = 0;
    assert(aOSMutexLock(&now, A_TIMEOUT_MS(UINT32_MAX)) == A_STATUS_TIMEOUT);
    assert(waits == 2 && now == 99);
    waits = 0;
    assert(aOSRecursiveMutexLock(&now, A_TIMEOUT_MS(UINT32_MAX)) == A_STATUS_TIMEOUT);
    assert(waits == 2 && now == 98);
    assert(aOSMutexLock(&now, A_TIMEOUT_NO_WAIT) == A_STATUS_BUSY);
    forever = A_TRUE;
    assert(aOSMutexLock(&now, A_TIMEOUT_FOREVER) == A_STATUS_OK);
    assert(milliseconds_to_ticks(UINT32_MAX) == UINT32_MAX);
    puts("aOS finite/infinite/long-wait/wrap boundary tests passed");
}
