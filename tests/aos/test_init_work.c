/* Exercise the real backend with a deterministic, single-core RTOS mock.
 * Including the implementation permits inspecting queue invariants without
 * adding test-only entry points to the public aOS API. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../platform/aOS/backend/freertos/aOS_freertos.c"

static int scheduler, depth, creates, notifications, callbacks;
static int critical_entries;
static int allocation_fails, takes;
static int mock_isr;
BaseType_t xPortIsInsideInterrupt(void) { return mock_isr; }
static jmp_buf idle;
static aOSWorkItem_t first, second, boundary;
static void callback(void *argument);

void mock_enter(void) { ++depth; ++critical_entries; }
void mock_exit(void) { assert(depth > 0); --depth; }
BaseType_t xTaskGetSchedulerState(void) { return scheduler; }
BaseType_t xTaskCreate(void (*function)(void *), const char *name,
                      uint16_t stack, void *argument, UBaseType_t priority,
                      TaskHandle_t *out)
{
    (void)argument;
    assert(function == os_task_entry && name != NULL);
    assert(((aOSTaskStart_t *)argument)->function == work_task);
    assert(stack == 512 && priority == 4);
    ++creates;
    if (allocation_fails) return pdFALSE;
    *out = (void *)1;
    return pdPASS;
}
BaseType_t xTaskNotifyGiveIndexed(TaskHandle_t task, UBaseType_t index)
{
    assert(task != NULL && (index == 1 || index == 2));
    if (index == 2) assert(depth == 0); /* Notify outside queue critical section. */
    ++notifications;
    return pdPASS;
}
void vTaskNotifyGiveIndexedFromISR(TaskHandle_t task, UBaseType_t index,
                                 BaseType_t *woken)
{
    (void)xTaskNotifyGiveIndexed(task, index);
    *woken = pdFALSE;
}
uint32_t ulTaskNotifyTakeIndexed(UBaseType_t index, BaseType_t clear,
                                TickType_t timeout)
{
    assert(index == 2 && clear == pdTRUE && timeout == portMAX_DELAY);
    assert(depth == 0);
    ++takes;
    if (takes == 2) {
        /* ISR arrives after empty check, immediately before sleeping. */
        assert(s_work_waiting);
        assert(aOSWorkSubmit(&boundary) == A_STATUS_OK);
    }
    if (takes == 3) longjmp(idle, 1);
    return 1;
}
static void callback(void *argument)
{
    assert(depth == 0);
    ++callbacks;
    if (callbacks == 1) {
        (void)argument;
        /* A running item may be queued once for another pass. */
        assert(aOSWorkSubmit(&first) == A_STATUS_OK);
        assert(aOSWorkSubmit(&first) == A_STATUS_OK);
    }
}
static void *bootstrap;
void *pvPortMalloc(size_t size) { return malloc(size); }
void vPortFree(void *p) { free(p); }
void vTaskSetThreadLocalStoragePointer(TaskHandle_t task, BaseType_t index, void *p)
{ (void)task; assert(index == AOS_START_TLS_INDEX); bootstrap = p; }
void vTaskDelete(TaskHandle_t task) { (void)task; abort(); }
int main(void)
{
    aOSWorkItemInit(&first, callback, NULL);
    aOSWorkItemInit(&second, callback, NULL);
    aOSWorkItemInit(&boundary, callback, NULL);
    assert(aOSWorkSubmit(&first) == A_STATUS_NOT_READY);
    assert(aOSWorkSubmit(&first) == A_STATUS_NOT_READY);
    scheduler = taskSCHEDULER_RUNNING;
    assert(aOSInit() == A_STATUS_NOT_READY && creates == 0);
    scheduler = taskSCHEDULER_NOT_STARTED;
    allocation_fails = 1;
    assert(aOSInit() == A_STATUS_NO_MEMORY && s_work_task == NULL);
    allocation_fails = 0;
    assert(aOSInit() == A_STATUS_OK && creates == 2);
    s_pre_scheduler_errno = A_EAGAIN;
    aOSRecordFault(AOS_FAULT_APP_INIT, A_STATUS_ERROR, "preserve");
    scheduler = taskSCHEDULER_RUNNING;
    assert(aOSInit() == A_STATUS_OK && creates == 2);
    assert(s_pre_scheduler_errno == A_EAGAIN);
    assert(g_aOSFaultRecord.code == AOS_FAULT_APP_INIT);
    assert(aOSWorkSubmit(&first) == A_STATUS_OK);
    mock_isr = 1;
    assert(aOSWorkSubmit(&second) == A_STATUS_OK);
    mock_isr = 0;
    assert(notifications == 1); /* Whole initial burst needs one wake. */
    const int before = critical_entries;
    if (setjmp(idle) == 0) work_task(NULL);
    /* 4 dispatches + 2 empty checks + 3 submissions (including duplicate). */
    assert(critical_entries - before == 9);
    assert(callbacks == 4 && notifications == 2 && takes == 3);
    assert(s_work_head == NULL && s_work_tail == NULL && s_work_waiting);
    assert(!first.queued && !first.running && depth == 0);
    aOSPrivateWaitObject_t wait = { .waiting_task = (void *)2 };
    notifications = 0;
    aOSWaitObjectNotify(&wait);
    aOSWaitObjectNotifyFromISR(&wait);
    aOSWaitObjectNotify(&wait);
    assert(notifications == 1 && wait.pending);
    wait.pending = A_FALSE; /* Waiter consumes its pending latch. */
    aOSWaitObjectNotifyFromISR(&wait);
    assert(notifications == 2 && depth == 0);
    assert(aOSWorkSubmit(&first) == A_STATUS_OK);
    assert(aOSWorkSubmit(&second) == A_STATUS_OK);
    assert(aOSWorkCancel(&first) == A_STATUS_OK);
    assert(s_work_head == &second && s_work_tail == &second);
    mock_isr = 1;
    assert(aOSWorkCancel(&second) == A_STATUS_OK);
    mock_isr = 0;
    assert(s_work_head == NULL && s_work_tail == NULL);
    first.running = A_TRUE; /* Model an in-flight handler at cancellation. */
    assert(aOSWorkCancel(&first) == A_STATUS_BUSY);
    assert(aOSWorkSubmit(&first) == A_STATUS_BUSY);
    first.running = A_FALSE;
    assert(aOSWorkCancel(&first) == A_STATUS_OK);
    free(bootstrap);
    puts("aOS init/idempotency/coalescing/requeue/sleep-boundary tests passed");
    return 0;
}
