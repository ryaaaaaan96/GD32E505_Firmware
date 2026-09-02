#include "aOS.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdint.h>

#define AOS_ERRNO_TLS_INDEX 0
#define AOS_WAIT_NOTIFICATION_INDEX 1U

#if (configUSE_TASK_NOTIFICATIONS != 1)
#error "aOS wait objects require FreeRTOS task notifications"
#endif

#if (configTASK_NOTIFICATION_ARRAY_ENTRIES <= AOS_WAIT_NOTIFICATION_INDEX)
#error "aOS wait objects require notification index 1"
#endif

typedef struct {
    TaskHandle_t waiting_task;
    volatile aBool_t pending;
} aOSPrivateWaitObject_t;

static aErrno_t s_pre_scheduler_errno;

static TickType_t milliseconds_to_ticks(uint32_t milliseconds)
{
    uint64_t ticks;

    ticks = ((uint64_t)milliseconds * (uint64_t)configTICK_RATE_HZ + 999ULL) /
            1000ULL;
    if ((milliseconds != 0U) && (ticks == 0ULL)) {
        ticks = 1ULL;
    }
    if (ticks > (uint64_t)portMAX_DELAY) {
        ticks = (uint64_t)portMAX_DELAY;
    }

    return (TickType_t)ticks;
}

aStatus_t aOSInit(void)
{
    s_pre_scheduler_errno = A_ERRNO_NONE;
    return A_STATUS_OK;
}

aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        uint32_t priority, aOSTaskHandle_t *handle)
{
    if ((function == NULL) || (name == NULL) || (stack_words == 0U) ||
        (priority >= configMAX_PRIORITIES)) {
        return A_STATUS_INVALID_PARAM;
    }

    return xTaskCreate(function, name, stack_words, argument,
                       (UBaseType_t)priority, (TaskHandle_t *)handle) == pdPASS
               ? A_STATUS_OK
               : A_STATUS_NO_MEMORY;
}

void aOSRun(void)
{
    vTaskStartScheduler();
    for (;;) {
    }
}

void aOSDelayMs(uint32_t milliseconds)
{
    vTaskDelay(milliseconds_to_ticks(milliseconds));
}

void aOSYield(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        taskYIELD();
    }
}

uint32_t aOSGetUptimeMs(void)
{
    const TickType_t ticks = xTaskGetTickCount();

    return (uint32_t)(((uint64_t)ticks * 1000ULL) /
                      (uint64_t)configTICK_RATE_HZ);
}

aStatus_t aOSWaitObjectCreate(aOSWaitObject_t *object)
{
    aOSPrivateWaitObject_t *wait_object;

    if ((object == NULL) || (*object != NULL)) {
        return A_STATUS_INVALID_PARAM;
    }

    wait_object = pvPortMalloc(sizeof(*wait_object));
    if (wait_object == NULL) {
        return A_STATUS_NO_MEMORY;
    }

    wait_object->waiting_task = NULL;
    wait_object->pending = A_FALSE;
    *object = (aOSWaitObject_t)wait_object;
    return A_STATUS_OK;
}

void aOSWaitObjectDestroy(aOSWaitObject_t *object)
{
    if ((object == NULL) || (*object == NULL)) {
        return;
    }

    vPortFree(*object);
    *object = NULL;
}

aStatus_t aOSWaitObjectWait(aOSWaitObject_t object, aTimeout_t timeout)
{
    aOSPrivateWaitObject_t *wait_object = object;
    aTimepoint_t end;
    TaskHandle_t current_task;

    if ((object == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    end = aTimepointCalc(timeout, aOSGetUptimeMs());

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        taskENTER_CRITICAL();
        if (wait_object->pending) {
            wait_object->pending = A_FALSE;
            taskEXIT_CRITICAL();
            return A_STATUS_OK;
        }
        taskEXIT_CRITICAL();
        return ((timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
                (timeout.milliseconds == 0U))
                   ? A_STATUS_BUSY
                   : A_STATUS_NOT_READY;
    }

    current_task = xTaskGetCurrentTaskHandle();
    for (;;) {
        aTimeout_t remaining;
        TickType_t ticks;

        taskENTER_CRITICAL();
        if ((wait_object->waiting_task != NULL) &&
            (wait_object->waiting_task != current_task)) {
            taskEXIT_CRITICAL();
            return A_STATUS_BUSY;
        }
        if (wait_object->pending) {
            wait_object->pending = A_FALSE;
            wait_object->waiting_task = NULL;
            taskEXIT_CRITICAL();
            return A_STATUS_OK;
        }

        remaining = aTimepointRemaining(&end, aOSGetUptimeMs());
        if ((remaining.type == A_TIMEOUT_TYPE_RELATIVE) &&
            (remaining.milliseconds == 0U)) {
            wait_object->waiting_task = NULL;
            taskEXIT_CRITICAL();
            return timeout.milliseconds == 0U ? A_STATUS_BUSY
                                              : A_STATUS_TIMEOUT;
        }
        wait_object->waiting_task = current_task;
        taskEXIT_CRITICAL();

        ticks = remaining.type == A_TIMEOUT_TYPE_FOREVER
                    ? portMAX_DELAY
                    : milliseconds_to_ticks(remaining.milliseconds);
        (void)ulTaskNotifyTakeIndexed(AOS_WAIT_NOTIFICATION_INDEX,
                                      pdTRUE, ticks);
    }
}

void aOSWaitObjectNotify(aOSWaitObject_t object)
{
    aOSPrivateWaitObject_t *wait_object = object;

    if (wait_object == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    wait_object->pending = A_TRUE;
    if (wait_object->waiting_task != NULL) {
        (void)xTaskNotifyGiveIndexed(wait_object->waiting_task,
                                     AOS_WAIT_NOTIFICATION_INDEX);
    }
    taskEXIT_CRITICAL();
}

void aOSWaitObjectNotifyFromISR(aOSWaitObject_t object)
{
    aOSPrivateWaitObject_t *wait_object = object;
    BaseType_t higher_priority_task_woken = pdFALSE;
    UBaseType_t interrupt_mask;

    if (wait_object == NULL) {
        return;
    }

    interrupt_mask = taskENTER_CRITICAL_FROM_ISR();
    wait_object->pending = A_TRUE;
    if (wait_object->waiting_task != NULL) {
        vTaskNotifyGiveIndexedFromISR(
            wait_object->waiting_task, AOS_WAIT_NOTIFICATION_INDEX,
            &higher_priority_task_woken);
    }
    taskEXIT_CRITICAL_FROM_ISR(interrupt_mask);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

aStatus_t aOSMutexCreate(aOSMutex_t *mutex)
{
    SemaphoreHandle_t handle;

    if ((mutex == NULL) || (*mutex != NULL)) {
        return A_STATUS_INVALID_PARAM;
    }

    handle = xSemaphoreCreateMutex();
    if (handle == NULL) {
        return A_STATUS_NO_MEMORY;
    }

    *mutex = (aOSMutex_t)handle;
    return A_STATUS_OK;
}

void aOSMutexDestroy(aOSMutex_t *mutex)
{
    if ((mutex == NULL) || (*mutex == NULL)) {
        return;
    }

    vSemaphoreDelete((SemaphoreHandle_t)*mutex);
    *mutex = NULL;
}

aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout)
{
    TickType_t ticks;

    if ((mutex == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) &&
        (timeout.type == A_TIMEOUT_TYPE_FOREVER ||
         timeout.milliseconds != 0U)) {
        return A_STATUS_NOT_READY;
    }

    ticks = timeout.type == A_TIMEOUT_TYPE_FOREVER
                ? portMAX_DELAY
                : milliseconds_to_ticks(timeout.milliseconds);
    if (xSemaphoreTake((SemaphoreHandle_t)mutex, ticks) == pdTRUE) {
        return A_STATUS_OK;
    }

    return ((timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
            (timeout.milliseconds == 0U))
               ? A_STATUS_BUSY
               : A_STATUS_TIMEOUT;
}

aStatus_t aOSMutexUnlock(aOSMutex_t mutex)
{
    if (mutex == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    return xSemaphoreGive((SemaphoreHandle_t)mutex) == pdTRUE
               ? A_STATUS_OK
               : A_STATUS_ERROR;
}

aErrno_t aOSGetErrno(void)
{
    TaskHandle_t task;

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return s_pre_scheduler_errno;
    }

    task = xTaskGetCurrentTaskHandle();
    return (aErrno_t)(uintptr_t)pvTaskGetThreadLocalStoragePointer(
        task, AOS_ERRNO_TLS_INDEX);
}

void aOSSetErrno(aErrno_t error)
{
    TaskHandle_t task;

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        s_pre_scheduler_errno = error;
        return;
    }

    task = xTaskGetCurrentTaskHandle();
    vTaskSetThreadLocalStoragePointer(task, AOS_ERRNO_TLS_INDEX,
                                      (void *)(uintptr_t)error);
}

aSSize_t aOSFailWithStatus(aStatus_t status)
{
    aOSSetErrno(aStatusToErrno(status));
    return -1;
}

aSSize_t aOSFailWithTimeout(aTimeout_t timeout)
{
    const aBool_t no_wait =
        (timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
        (timeout.milliseconds == 0U);

    aOSSetErrno(no_wait ? A_EAGAIN : A_ETIMEDOUT);
    return -1;
}

aBool_t aOSPollWaitExpired(const aTimepoint_t *timepoint)
{
    if (aTimepointExpired(timepoint, aOSGetUptimeMs())) {
        return A_TRUE;
    }

    aOSYield();
    return A_FALSE;
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
