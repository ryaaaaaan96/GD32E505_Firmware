#include "aOS.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>

#define AOS_ERRNO_TLS_INDEX 0

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
    const bool no_wait =
        (timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
        (timeout.milliseconds == 0U);

    aOSSetErrno(no_wait ? A_EAGAIN : A_ETIMEDOUT);
    return -1;
}

bool aOSPollWaitExpired(const aTimepoint_t *timepoint)
{
    if (aTimepointExpired(timepoint, aOSGetUptimeMs())) {
        return true;
    }

    aOSYield();
    return false;
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
