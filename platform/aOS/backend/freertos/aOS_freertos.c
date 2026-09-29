#include "aOS.h"
#include "aOS_config.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include <stdint.h>
#include <stdatomic.h>

_Static_assert(sizeof(TickType_t) == 4U && configTICK_RATE_HZ == 1000U,
               "aOS requires 32-bit ticks at 1000 Hz");

#define AOS_ERRNO_TLS_INDEX 0
#define AOS_START_TLS_INDEX 1

/* TLS owns the bootstrap until entry, including deletion before first run. */
typedef struct {
    aOSTaskFunction_t function;
    void *argument;
} aOSTaskStart_t;

static void os_task_entry(void *argument)
{
    aOSTaskStart_t *start = argument;
    /* Deletion must not interleave between clearing TLS ownership and free. */
    taskENTER_CRITICAL();
    const aOSTaskStart_t entry = *start;
    vTaskSetThreadLocalStoragePointer(NULL, AOS_START_TLS_INDEX, NULL);
    vPortFree(start);
    taskEXIT_CRITICAL();
    entry.function(entry.argument);
    aOSTaskExit();
}
#define AOS_WAIT_NOTIFICATION_INDEX 1U
#define AOS_WORK_NOTIFICATION_INDEX 2U

#if (configUSE_TASK_NOTIFICATIONS != 1)
#error "aOS wait objects require FreeRTOS task notifications"
#endif

#if (configTASK_NOTIFICATION_ARRAY_ENTRIES <= AOS_WORK_NOTIFICATION_INDEX)
#error "aOS requires notification indexes 1 (wait) and 2 (work queue)"
#endif

typedef struct {
    TaskHandle_t waiting_task;
    volatile aBool_t pending;
} aOSPrivateWaitObject_t;

typedef struct {
    TimerHandle_t timer;
    aOSTimerCallback_t callback;
    void *argument;
    SemaphoreHandle_t quiesced;
    TickType_t started;
    TickType_t duration;
    aBool_t armed;
    aBool_t running;
    aBool_t deleting;
} aOSPrivateTimer_t;

static aErrno_t s_pre_scheduler_errno;
aOSFaultRecord_t g_aOSFaultRecord;
#if AOS_WORKQUEUE_ENABLE
static aOSTaskHandle_t s_work_task;
static aOSWorkItem_t *s_work_head;
static aOSWorkItem_t *s_work_tail;
/* 由队列临界区保护：仅在 worker 将要等待时发送一次唤醒。 */
static aBool_t s_work_waiting = A_TRUE;

static void work_append_locked(aOSWorkItem_t *item)
{
    item->next = NULL;
    item->queued = A_TRUE;
    if (s_work_tail == NULL) {
        s_work_head = item;
    } else {
        s_work_tail->next = item;
    }
    s_work_tail = item;
}

static void work_task(void *argument)
{
    /* 平台共享延迟执行服务：ISR 只投递，业务回调在此任务串行执行。
     * 空队列时阻塞等待通知；回调不得阻塞，否则会拖延所有设备的回调。 */
    (void)argument;
    for (;;) {
        (void)ulTaskNotifyTakeIndexed(AOS_WORK_NOTIFICATION_INDEX, pdTRUE,
                                      portMAX_DELAY);
        aOSWorkItem_t *completed = NULL;
        for (;;) {
            aOSWorkItem_t *item;
            aOSWorkFunction_t function;
            void *function_argument;

            taskENTER_CRITICAL();
            /* 上一项完成与下一项出队共用一次临界区；回调始终在区外。 */
            if (completed != NULL) {
                completed->running = A_FALSE;
                completed->canceling = A_FALSE;
            }
            item = s_work_head;
            if (item == NULL) {
                /* 与入队共用锁，避免检查空队列到睡眠之间丢失唤醒。 */
                s_work_waiting = A_TRUE;
                taskEXIT_CRITICAL();
                break;
            }
            s_work_head = item->next;
            if (s_work_head == NULL) {
                s_work_tail = NULL;
            }
            item->next = NULL;
            item->queued = A_FALSE;
            item->running = A_TRUE;
            function = item->function;
            function_argument = item->argument;
            taskEXIT_CRITICAL();

            if (function != NULL) {
                function(function_argument);
            }

            completed = item;
            taskYIELD();
        }
    }
}

#endif /* AOS_WORKQUEUE_ENABLE */

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

/* Only blocking waits interpret portMAX_DELAY as infinity. Timers/delays
 * retain their full finite range. CMake enforces 32-bit ticks at 1000 Hz. */
static TickType_t finite_wait_ticks(uint32_t milliseconds)
{
    const TickType_t ticks = milliseconds_to_ticks(milliseconds);
    return ticks == portMAX_DELAY ? portMAX_DELAY - 1U : ticks;
}

aStatus_t aOSInit(void)
{
    static aBool_t initialized;
#if AOS_WORKQUEUE_ENABLE
    aOSTaskConfig_t config = AOS_TASK_CONFIG_DEFAULT;
    aStatus_t status;
#endif

    if (initialized) return A_STATUS_OK;
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return A_STATUS_NOT_READY;
#if AOS_WORKQUEUE_ENABLE
    config.name = "aOSWork";
    config.function = work_task;
    config.stack_bytes = AOS_WORKER_STACK_BYTES;
    config.priority = AOS_WORKER_PRIORITY;
    status = aOSCreateTask(&config, &s_work_task);
    if (status != A_STATUS_OK) return status;
#endif
    initialized = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aOSValidateIsrPriority(uint32_t priority)
{
    if ((priority > configLIBRARY_LOWEST_INTERRUPT_PRIORITY) ||
        (priority < configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY)) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}

void aOSDeleteTask(aOSTaskHandle_t handle)
{
    if (handle != NULL) {
        taskENTER_CRITICAL();
        void *start = pvTaskGetThreadLocalStoragePointer(
            (TaskHandle_t)handle, AOS_START_TLS_INDEX);
        vTaskSetThreadLocalStoragePointer(
            (TaskHandle_t)handle, AOS_START_TLS_INDEX, NULL);
        vPortFree(start);
        vTaskDelete((TaskHandle_t)handle);
        taskEXIT_CRITICAL();
    }
}

void aOSTaskExit(void)
{
    /* NULL 的“删除当前任务”语义仅封装在后端，不暴露给应用层。 */
    vTaskDelete(NULL);
    /* 正常情况下不会继续执行，防止任务入口意外返回。 */
    for (;;) {
    }
}

void *aOSAlloc(size_t size)
{
    return size == 0U ? NULL : pvPortMalloc(size);
}

void aOSFree(void *memory)
{
    if (memory != NULL) {
        vPortFree(memory);
    }
}

void aOSRecordFault(aOSFaultCode_t code, aStatus_t status,
                    const char *context)
{
    g_aOSFaultRecord.status = (int32_t)status;
    g_aOSFaultRecord.context = context;
    atomic_thread_fence(memory_order_release);
    g_aOSFaultRecord.code = (uint32_t)code;
}

aStatus_t aOSCreateTask(const aOSTaskConfig_t *config, aOSTaskHandle_t *handle)
{
    if (handle != NULL) *handle = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    const aOSTaskFunction_t function = config->function;
    const char *name = config->name;
    const size_t stack_bytes = config->stack_bytes;
    void *argument = config->argument;
    const uint32_t priority = config->priority;
    if (function == NULL || name == NULL ||
        priority < AOS_TASK_PRIO_LOWEST || priority > AOS_TASK_PRIO_REALTIME ||
        priority >= configMAX_PRIORITIES ||
        stack_bytes > (size_t)UINT16_MAX * sizeof(StackType_t)) {
        return A_STATUS_INVALID_PARAM;
    }
    const size_t words = stack_bytes == 0U ? configMINIMAL_STACK_SIZE
        : (stack_bytes + sizeof(StackType_t) - 1U) / sizeof(StackType_t);
    aOSTaskStart_t *start = pvPortMalloc(sizeof(*start));
    if (start == NULL) return A_STATUS_NO_MEMORY;
    *start = (aOSTaskStart_t){ function, argument };
    TaskHandle_t task = NULL;
    /* Publish bootstrap ownership before the new task can execute. */
    taskENTER_CRITICAL();
    const BaseType_t result = xTaskCreate(os_task_entry, name, (uint16_t)words,
                                         start, (UBaseType_t)priority, &task);
    if (result == pdPASS) {
        vTaskSetThreadLocalStoragePointer(task, AOS_START_TLS_INDEX, start);
        if (handle != NULL) *handle = task;
    }
    taskEXIT_CRITICAL();
    if (result != pdPASS) vPortFree(start);
    return result == pdPASS ? A_STATUS_OK : A_STATUS_NO_MEMORY;
}

void aOSRun(void)
{
    vTaskStartScheduler();
    aOSRecordFault(AOS_FAULT_SCHEDULER_RETURNED, A_STATUS_ERROR,
                   "vTaskStartScheduler returned");
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
                    : finite_wait_ticks(remaining.milliseconds);
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
    if (wait_object->pending) {
        taskEXIT_CRITICAL();
        return;
    }
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
    if (wait_object->pending) {
        taskEXIT_CRITICAL_FROM_ISR(interrupt_mask);
        return;
    }
    wait_object->pending = A_TRUE;
    if (wait_object->waiting_task != NULL) {
        vTaskNotifyGiveIndexedFromISR(
            wait_object->waiting_task, AOS_WAIT_NOTIFICATION_INDEX,
            &higher_priority_task_woken);
    }
    taskEXIT_CRITICAL_FROM_ISR(interrupt_mask);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

#if AOS_WORKQUEUE_ENABLE
void aOSWorkItemInit(aOSWorkItem_t *item,
                      aOSWorkFunction_t function, void *argument)
{
    if (item == NULL) return;
    item->next = NULL;
    item->function = function;
    item->argument = argument;
    item->canceling = A_FALSE;
    item->queued = A_FALSE;
    item->running = A_FALSE;
}

static aStatus_t work_submit_task(aOSWorkItem_t *item)
{
    aBool_t notify;
    if ((item == NULL) || (item->function == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (s_work_task == NULL) {
        return A_STATUS_NOT_READY;
    }

    taskENTER_CRITICAL();
    if (item->canceling || item->queued) {
        aStatus_t status = item->canceling ? A_STATUS_BUSY : A_STATUS_OK;
        taskEXIT_CRITICAL();
        return status;
    }
    work_append_locked(item);
    notify = s_work_waiting;
    if (notify) {
        s_work_waiting = A_FALSE;
    }
    taskEXIT_CRITICAL();
    /* worker 生命周期固定；解锁后通知，不延长队列的中断屏蔽窗口。 */
    if (notify) {
        (void)xTaskNotifyGiveIndexed((TaskHandle_t)s_work_task,
                                     AOS_WORK_NOTIFICATION_INDEX);
    }
    return A_STATUS_OK;
}

static aStatus_t work_submit_isr(aOSWorkItem_t *item)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    UBaseType_t interrupt_mask;
    aBool_t notify;

    if ((item == NULL) || (item->function == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (s_work_task == NULL) {
        return A_STATUS_NOT_READY;
    }

    interrupt_mask = taskENTER_CRITICAL_FROM_ISR();
    if (item->canceling || item->queued) {
        aStatus_t status = item->canceling ? A_STATUS_BUSY : A_STATUS_OK;
        taskEXIT_CRITICAL_FROM_ISR(interrupt_mask);
        return status;
    }
    work_append_locked(item);
    notify = s_work_waiting;
    if (notify) {
        s_work_waiting = A_FALSE;
    }
    taskEXIT_CRITICAL_FROM_ISR(interrupt_mask);
    if (notify) {
        vTaskNotifyGiveIndexedFromISR((TaskHandle_t)s_work_task,
                                      AOS_WORK_NOTIFICATION_INDEX,
                                      &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
    return A_STATUS_OK;
}

aStatus_t aOSWorkSubmit(aOSWorkItem_t *item)
{
    return xPortIsInsideInterrupt() ? work_submit_isr(item) :
        work_submit_task(item);
}

aStatus_t aOSWorkCancel(aOSWorkItem_t *item)
{
    if (item == NULL) return A_STATUS_INVALID_PARAM;
    const aBool_t isr = xPortIsInsideInterrupt() ? A_TRUE : A_FALSE;
    UBaseType_t key = 0U;
    if (isr) key = taskENTER_CRITICAL_FROM_ISR();
    else taskENTER_CRITICAL();
    aOSWorkItem_t *previous = NULL;
    for (aOSWorkItem_t *cursor = s_work_head; cursor; cursor = cursor->next) {
        if (cursor == item) {
            if (previous) previous->next = cursor->next;
            else s_work_head = cursor->next;
            if (s_work_tail == cursor) s_work_tail = previous;
            item->next = NULL;
            item->queued = A_FALSE;
            break;
        }
        previous = cursor;
    }
    item->canceling = item->running;
    const aStatus_t status = item->running ? A_STATUS_BUSY : A_STATUS_OK;
    if (isr) taskEXIT_CRITICAL_FROM_ISR(key);
    else taskEXIT_CRITICAL();
    return status;
}

aStatus_t aOSWorkCancelSync(aOSWorkItem_t *item, aTimeout_t timeout)
{
    if (item == NULL ||
        !aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    if (xPortIsInsideInterrupt() || aOSIsWorkContext()) return A_STATUS_BUSY;
    (void)aOSWorkCancel(item);
    return aOSWorkWaitIdle(item, timeout);
}

aBool_t aOSIsWorkContext(void)
{
    return s_work_task != NULL &&
           xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
           xTaskGetCurrentTaskHandle() == (TaskHandle_t)s_work_task;
}

aStatus_t aOSWorkWaitIdle(aOSWorkItem_t *item, aTimeout_t timeout)
{
    aTimepoint_t deadline;

    if ((item == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (xPortIsInsideInterrupt()) return A_STATUS_BUSY;
    deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    for (;;) {
        aBool_t busy;

        taskENTER_CRITICAL();
        busy = (item->queued || item->running) ? A_TRUE : A_FALSE;
        taskEXIT_CRITICAL();
        if (!busy) {
            return A_STATUS_OK;
        }
        if (aOSIsWorkContext() ||
            xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
            return A_STATUS_BUSY;
        }
        if (aOSPollWaitExpired(&deadline)) {
            return A_STATUS_TIMEOUT;
        }
        aOSDelayMs(1U);
    }
}

#endif /* AOS_WORKQUEUE_ENABLE */

void aOSNotifyGive(aOSWaitObject_t object)
{
    if (xPortIsInsideInterrupt()) {
        aOSWaitObjectNotifyFromISR(object);
    } else {
        aOSWaitObjectNotify(object);
    }
}

static void os_timer_dispatch(TimerHandle_t timer_handle)
{
    aOSPrivateTimer_t *timer = pvTimerGetTimerID(timer_handle);
    aOSTimerCallback_t callback = NULL;
    void *argument = NULL;

    /* Stop suppresses queued expiry; a restart rejects an old early expiry.
     * The user callback is never called with interrupts masked. */
    taskENTER_CRITICAL();
    if (timer->armed && !timer->deleting &&
        (TickType_t)(xTaskGetTickCount() - timer->started) >= timer->duration) {
        timer->armed = A_FALSE;
        timer->running = A_TRUE;
        callback = timer->callback;
        argument = timer->argument;
    }
    taskEXIT_CRITICAL();
    if (callback != NULL) callback(argument);
    taskENTER_CRITICAL();
    timer->running = A_FALSE;
    taskEXIT_CRITICAL();
}

static void os_timer_quiesced(void *argument, uint32_t unused)
{
    (void)unused;
    /* FIFO after DELETE: no timer callback can access its wrapper again. */
    (void)xSemaphoreGive((SemaphoreHandle_t)argument);
}

aStatus_t aOSTimerCreate(aOSTimer_t *timer_out,
                         aOSTimerCallback_t callback, void *argument)
{
    aOSPrivateTimer_t *timer;

    if ((timer_out == NULL) || (callback == NULL) || (*timer_out != NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (xPortIsInsideInterrupt()) return A_STATUS_NOT_READY;
    timer = aOSAlloc(sizeof(*timer));
    if (timer == NULL) {
        return A_STATUS_NO_MEMORY;
    }
    timer->callback = callback;
    timer->argument = argument;
    timer->armed = A_FALSE;
    timer->running = A_FALSE;
    timer->deleting = A_FALSE;
    timer->started = 0U;
    timer->duration = 0U;
    timer->quiesced = xSemaphoreCreateBinary();
    if (timer->quiesced == NULL) {
        aOSFree(timer);
        return A_STATUS_NO_MEMORY;
    }
    timer->timer = xTimerCreate("aOSTimer", 1U, pdFALSE, timer,
                                os_timer_dispatch);
    if (timer->timer == NULL) {
        vSemaphoreDelete(timer->quiesced);
        aOSFree(timer);
        return A_STATUS_NO_MEMORY;
    }
    *timer_out = timer;
    return A_STATUS_OK;
}

aStatus_t aOSTimerStart(aOSTimer_t timer_object, uint32_t milliseconds)
{
    aOSPrivateTimer_t *timer = timer_object;
    TickType_t ticks;

    if ((timer == NULL) || (milliseconds == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (xPortIsInsideInterrupt()) return A_STATUS_NOT_READY;
    ticks = milliseconds_to_ticks(milliseconds);
    taskENTER_CRITICAL();
    if (timer->deleting || (timer->running &&
        xTaskGetCurrentTaskHandle() != xTimerGetTimerDaemonTaskHandle())) {
        taskEXIT_CRITICAL();
        return A_STATUS_BUSY;
    }
    BaseType_t accepted = xTimerChangePeriod(timer->timer, ticks, 0U);
    if (accepted == pdPASS) {
        timer->started = xTaskGetTickCount();
        timer->duration = ticks;
        timer->armed = A_TRUE;
    }
    taskEXIT_CRITICAL();
    return accepted == pdPASS ? A_STATUS_OK : A_STATUS_BUSY;
}

aStatus_t aOSTimerStop(aOSTimer_t timer_object)
{
    aOSPrivateTimer_t *timer = timer_object;

    if (timer == NULL) return A_STATUS_OK;
    if (xPortIsInsideInterrupt()) return A_STATUS_NOT_READY;
    taskENTER_CRITICAL();
    timer->armed = A_FALSE;
    BaseType_t accepted = timer->deleting ? pdPASS
        : xTimerStop(timer->timer, 0U);
    taskEXIT_CRITICAL();
    return accepted == pdPASS ? A_STATUS_OK : A_STATUS_BUSY;
}

aStatus_t aOSTimerDestroy(aOSTimer_t *timer_object)
{
    aOSPrivateTimer_t *timer;

    if ((timer_object == NULL) || (*timer_object == NULL)) {
        return A_STATUS_OK;
    }
    if (xPortIsInsideInterrupt() ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return A_STATUS_NOT_READY;
    }
    if (xTaskGetCurrentTaskHandle() == xTimerGetTimerDaemonTaskHandle()) {
        return A_STATUS_BUSY;
    }
    timer = *timer_object;
    (void)aOSTimerStop(timer);
    if (!timer->deleting) {
        if (xTimerDelete(timer->timer, portMAX_DELAY) != pdPASS) {
            return A_STATUS_BUSY;
        }
        taskENTER_CRITICAL();
        timer->deleting = A_TRUE;
        taskEXIT_CRITICAL();
    }
    if (xTimerPendFunctionCall(os_timer_quiesced, timer->quiesced,
                               0U, portMAX_DELAY) != pdPASS) {
        return A_STATUS_BUSY; /* Retain wrapper; a later Destroy may retry. */
    }
    (void)xSemaphoreTake(timer->quiesced, portMAX_DELAY);
    vSemaphoreDelete(timer->quiesced);
    *timer_object = NULL;
    aOSFree(timer);
    return A_STATUS_OK;
}

void aOSCriticalEnter(void) { taskENTER_CRITICAL(); }
void aOSCriticalExit(void) { taskEXIT_CRITICAL(); }
aOSCriticalState_t aOSCriticalEnterFromISR(void)
{ return (aOSCriticalState_t)taskENTER_CRITICAL_FROM_ISR(); }
void aOSCriticalExitFromISR(aOSCriticalState_t state)
{ taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)state); }

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

    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    aTimeout_t remaining = timeout;
    for (;;) {
        ticks = remaining.type == A_TIMEOUT_TYPE_FOREVER
            ? portMAX_DELAY : finite_wait_ticks(remaining.milliseconds);
        if (xSemaphoreTake((SemaphoreHandle_t)mutex, ticks) == pdTRUE)
            return A_STATUS_OK;
        if (timeout.type == A_TIMEOUT_TYPE_RELATIVE && timeout.milliseconds ==
            0U)
            return A_STATUS_BUSY;
        remaining = aTimepointRemaining(&end, aOSGetUptimeMs());
        if (remaining.type == A_TIMEOUT_TYPE_RELATIVE &&
            remaining.milliseconds == 0U)
            return A_STATUS_TIMEOUT;
        /* Recompute after a bounded chunk or a backend early wake. */
    }
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

aStatus_t aOSRecursiveMutexCreate(aOSRecursiveMutex_t *mutex)
{
    SemaphoreHandle_t handle;

    if ((mutex == NULL) || (*mutex != NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    handle = xSemaphoreCreateRecursiveMutex();
    if (handle == NULL) {
        return A_STATUS_NO_MEMORY;
    }
    *mutex = (aOSRecursiveMutex_t)handle;
    return A_STATUS_OK;
}

void aOSRecursiveMutexDestroy(aOSRecursiveMutex_t *mutex)
{
    if ((mutex != NULL) && (*mutex != NULL)) {
        vSemaphoreDelete((SemaphoreHandle_t)*mutex);
        *mutex = NULL;
    }
}

aStatus_t aOSRecursiveMutexLock(aOSRecursiveMutex_t mutex,
                               aTimeout_t timeout)
{
    TickType_t ticks;

    if ((mutex == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) &&
        ((timeout.type == A_TIMEOUT_TYPE_FOREVER) ||
         (timeout.milliseconds != 0U))) {
        return A_STATUS_NOT_READY;
    }
    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    aTimeout_t remaining = timeout;
    for (;;) {
        ticks = remaining.type == A_TIMEOUT_TYPE_FOREVER
            ? portMAX_DELAY : finite_wait_ticks(remaining.milliseconds);
        if (xSemaphoreTakeRecursive((SemaphoreHandle_t)mutex, ticks) == pdTRUE)
            return A_STATUS_OK;
        if (timeout.type == A_TIMEOUT_TYPE_RELATIVE && timeout.milliseconds ==
            0U)
            return A_STATUS_BUSY;
        remaining = aTimepointRemaining(&end, aOSGetUptimeMs());
        if (remaining.type == A_TIMEOUT_TYPE_RELATIVE &&
            remaining.milliseconds == 0U)
            return A_STATUS_TIMEOUT;
        /* Recompute after a bounded chunk or a backend early wake. */
    }
}

aStatus_t aOSRecursiveMutexUnlock(aOSRecursiveMutex_t mutex)
{
    if (mutex == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    return xSemaphoreGiveRecursive((SemaphoreHandle_t)mutex) == pdTRUE
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
    aOSRecordFault(AOS_FAULT_MALLOC_FAILED, A_STATUS_NO_MEMORY,
                   "FreeRTOS malloc failed");
    /* Allocation failure is recoverable; the caller owns the fatal policy. */
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    aOSRecordFault(AOS_FAULT_STACK_OVERFLOW, A_STATUS_ERROR, task_name);
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
