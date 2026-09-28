/* Deterministic timer-service FIFO model; exercises the actual aOS wrapper. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static int depth, in_isr, full, calls, scheduler = taskSCHEDULER_RUNNING;
static int task_notifies, isr_notifies, live_allocations;
static TickType_t now;
static TaskHandle_t current = (void *)1;
static aOSTimer_t object;
static aBool_t self_destroy, self_rearm;
typedef struct { void *id; void (*callback)(TimerHandle_t); } MockTimer;
typedef struct { int kind; void *arg; void (*function)(void *, uint32_t); } Command;
static Command commands[32];
static unsigned count;

void mock_enter(void) { ++depth; }
void mock_exit(void) { assert(depth > 0); --depth; }
BaseType_t xPortIsInsideInterrupt(void) { return in_isr; }
BaseType_t xTaskGetSchedulerState(void) { return scheduler; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
TaskHandle_t xTimerGetTimerDaemonTaskHandle(void) { return (void *)2; }
TickType_t xTaskGetTickCount(void) { return now; }
void *pvPortMalloc(size_t size) { ++live_allocations; return malloc(size); }
void vPortFree(void *ptr) { --live_allocations; free(ptr); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return calloc(1, sizeof(int)); }
void vSemaphoreDelete(SemaphoreHandle_t sem) { free(sem); }
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) { *(int *)sem = 1; return pdPASS; }
static void drain(void)
{
    TaskHandle_t saved = current;
    current = (void *)2;
    for (unsigned i = 0; i < count; ++i) {
        if (commands[i].kind == 1) free(commands[i].arg);
        else if (commands[i].kind == 2)
            commands[i].function(commands[i].arg, 0);
    }
    count = 0;
    current = saved;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeout)
{
    assert(timeout == portMAX_DELAY && depth == 0);
    drain();
    assert(*(int *)sem == 1);
    *(int *)sem = 0;
    return pdPASS;
}
TimerHandle_t xTimerCreate(const char *name, TickType_t ticks, BaseType_t reload,
                          void *id, void (*callback)(TimerHandle_t))
{
    (void)name; (void)ticks; (void)reload;
    MockTimer *timer = malloc(sizeof(*timer));
    *timer = (MockTimer){ id, callback };
    return timer;
}
void *pvTimerGetTimerID(TimerHandle_t handle) { return ((MockTimer *)handle)->id; }
BaseType_t xTimerChangePeriod(TimerHandle_t h, TickType_t ticks, TickType_t wait)
{ (void)h; (void)ticks; assert(wait == 0); return full ? pdFALSE : pdPASS; }
BaseType_t xTimerStop(TimerHandle_t h, TickType_t wait)
{ (void)h; assert(wait == 0); return full ? pdFALSE : pdPASS; }
BaseType_t xTimerDelete(TimerHandle_t h, TickType_t wait)
{
    assert(wait == portMAX_DELAY);
    if (full == 1) return pdFALSE;
    commands[count++] = (Command){ .kind = 1, .arg = h };
    return pdPASS;
}
BaseType_t xTimerPendFunctionCall(void (*fn)(void *, uint32_t), void *arg,
                                uint32_t unused, TickType_t wait)
{
    (void)unused; assert(wait == portMAX_DELAY);
    if (full == 2) return pdFALSE;
    commands[count++] = (Command){ .kind = 2, .arg = arg, .function = fn };
    return pdPASS;
}
BaseType_t xTaskNotifyGiveIndexed(TaskHandle_t task, UBaseType_t index)
{ assert(task && index == 1); ++task_notifies; return pdPASS; }
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task, UBaseType_t index, BaseType_t *woken)
{ assert(task && index == 1); ++isr_notifies; *woken = pdFALSE; }

static void expired(void *arg)
{
    assert(arg == &calls && depth == 0);
    ++calls;
    if (self_destroy) assert(aOSTimerDestroy(&object) == A_STATUS_BUSY);
    if (self_rearm) assert(aOSTimerStart(object, 5) == A_STATUS_OK);
}
static void fire(void)
{
    TaskHandle_t saved = current;
    current = (void *)2;
    os_timer_dispatch(((aOSPrivateTimer_t *)object)->timer);
    current = saved;
}
int main(void)
{
    aOSPrivateWaitObject_t waiter = { .waiting_task = (void *)3 };
    aOSNotifyGive(&waiter);
    aOSNotifyGive(&waiter);
    assert(task_notifies == 1 && isr_notifies == 0);
    waiter.pending = A_FALSE;
    in_isr = 1;
    aOSNotifyGive(&waiter);
    assert(isr_notifies == 1);
    assert(aOSTimerCreate(&object, expired, &calls) == A_STATUS_NOT_READY);
    in_isr = 0;
    assert(aOSTimerCreate(&object, expired, &calls) == A_STATUS_OK);
    assert(aOSTimerStart(object, 10) == A_STATUS_OK);
    full = 1;
    assert(aOSTimerStart(object, 50) == A_STATUS_BUSY);
    full = 0; /* Rejected restart must preserve the 10-tick deadline. */
    ((aOSPrivateTimer_t *)object)->running = A_TRUE;
    assert(aOSTimerStart(object, 50) == A_STATUS_BUSY);
    ((aOSPrivateTimer_t *)object)->running = A_FALSE;
    now = 5;
    fire(); assert(calls == 0);
    now = 10;
    fire(); fire(); assert(calls == 1); /* One-shot terminal arbitration. */
    assert(aOSTimerStart(object, 10) == A_STATUS_OK);
    full = 1;
    assert(aOSTimerStop(object) == A_STATUS_BUSY);
    now = 20; fire(); assert(calls == 1); /* Stop gates even if queue full. */
    assert(aOSTimerDestroy(&object) == A_STATUS_BUSY && object != NULL);
    full = 0;
    assert(aOSTimerStart(object, 10) == A_STATUS_OK);
    now = 25;
    assert(aOSTimerStart(object, 10) == A_STATUS_OK);
    now = 30; fire(); assert(calls == 1); /* Expiry of previous generation. */
    now = 35; self_destroy = A_TRUE; self_rearm = A_TRUE;
    fire(); assert(calls == 2 && object != NULL);
    self_rearm = A_FALSE;
    now = 40; fire(); assert(calls == 3);
    now = UINT32_MAX - 3U;
    assert(aOSTimerStart(object, 6) == A_STATUS_OK);
    now = 1; fire(); assert(calls == 3);
    now = 2; fire(); assert(calls == 4); /* Tick rollover. */
    scheduler = taskSCHEDULER_NOT_STARTED;
    assert(aOSTimerDestroy(&object) == A_STATUS_NOT_READY && object != NULL);
    scheduler = taskSCHEDULER_RUNNING;
    full = 2;
    assert(aOSTimerDestroy(&object) == A_STATUS_BUSY && object != NULL);
    drain(); /* Kernel timer deleted, wrapper remains safe for retry. */
    assert(aOSTimerStart(object, 1) == A_STATUS_BUSY);
    full = 0;
    assert(aOSTimerDestroy(&object) == A_STATUS_OK && object == NULL);
    assert(count == 0 && live_allocations == 0 && depth == 0);
    puts("aOS timer lifetime and cross-context notification tests passed");
}
