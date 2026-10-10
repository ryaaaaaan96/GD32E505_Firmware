/* FreeRTOS 后端：将 aOS 的任务、等待、定时器和锁接口映射到内核 API。
 * 公共调用契约见 aOS.h；本文件说明资源归属、执行时序与同步方式。
 * 除明确支持 ISR 的接口外，只能从启动阶段或任务上下文调用。
 * 对象销毁前由调用方停止所有使用者，临界区不替代生命周期管理。 */
#include "aOS.h"
#include "aOS_config.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include <stdint.h>
#include <stdatomic.h>

/* 当前时基约定为 32 位、1 ms 每 tick，与超时计算及回绕处理保持一致。 */
_Static_assert(sizeof(TickType_t) == 4U && configTICK_RATE_HZ == 1000U,
               "aOS requires 32-bit ticks at 1000 Hz");

/* TLS 槽 0 保存任务错误码；与下方任务通知槽独立，业务不可占用此槽。 */
#define AOS_ERRNO_TLS_INDEX 0

/* 等待对象与工作队列分别使用通知槽；通知只负责唤醒，不保存业务数据。 */
#define AOS_WAIT_NOTIFICATION_INDEX 1U
#define AOS_WORK_NOTIFICATION_INDEX 2U

#if (configUSE_TASK_NOTIFICATIONS != 1)
#error "aOS wait objects require FreeRTOS task notifications"
#endif

#if (configTASK_NOTIFICATION_ARRAY_ENTRIES <= AOS_WORK_NOTIFICATION_INDEX)
#error "aOS requires notification indexes 1 (wait) and 2 (work queue)"
#endif

/* 单等待者、可合并通知；先通知后等待时由 pending 保留一次待处理状态。
 * volatile 不提供互斥，waiting_task 与 pending 的配合由临界区保护。 */
typedef struct {
    TaskHandle_t waiting_task; /**< 当前等待任务；NULL 表示未登记等待者。 */
    volatile aBool_t pending; /**< 是否有尚未消费的通知，多次通知合并。 */
} aOSPrivateWaitObject_t;

/* 软件定时器包装：内核计时对象与业务回调状态分开管理。
 * 停止和重启可能遇到已排队的到期处理，因此还需状态与时长校验。 */
typedef struct {
    TimerHandle_t timer; /**< FreeRTOS 一次性软件定时器。 */
    aOSTimerCallback_t callback; /**< 在定时服务任务中执行的业务函数。 */
    void *argument; /**< 借用参数，销毁完成前须保持有效。 */
    SemaphoreHandle_t quiesced; /**< 定时服务确认删除完成后释放此信号量。 */
    TickType_t started; /**< 最近一次成功提交启动命令时的 tick。 */
    TickType_t duration; /**< 当前计时长度，用于过滤旧的提前到期处理。 */
    aBool_t armed; /**< 是否允许当前计时触发一次回调。 */
    aBool_t running; /**< 是否已进入回调，尚未完成退出处理。 */
    aBool_t deleting; /**< 删除命令已提交，不再接受重新启动。 */
} aOSPrivateTimer_t;

/* 启动前没有运行中的任务，错误码暂存全局；启动后使用各任务的 TLS。 */
static aErrno_t s_pre_scheduler_errno;
/* 供调试器读取最近故障；不是日志队列，也不持久化。 */
aOSFaultRecord_t g_aOSFaultRecord;
#if AOS_WORKQUEUE_ENABLE
static aOSTaskHandle_t s_work_task;
static aOSWorkItem_t *s_work_head;
static aOSWorkItem_t *s_work_tail;
/* 由队列临界区保护：仅在工作任务准备等待时发送一次唤醒。 */
static aBool_t s_work_waiting = A_TRUE;

/* 调用方已进入任务或 ISR 临界区；链表直接使用工作项，不额外分配内存。 */
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
    /* 平台共享延迟执行服务：任务或 ISR 投递，回调在此任务串行执行。
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
                /* 与入队共用临界区，避免检查空队列到睡眠之间丢失唤醒。 */
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

            /* 下一次循环才清除运行标志，此前工作项仍不可释放。
             * 主动让出调度机会，但不保证低优先级任务一定获得 CPU。 */
            completed = item;
            taskYIELD();
        }
    }
}

#endif /* 共享工作队列 */

/* 使用 64 位中间值避免乘法溢出，毫秒向上折算为 tick。
 * 超出后端容量时饱和；这里仅换算数值，不解释“永久等待”语义。 */
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

/* 阻塞等待可能把 portMAX_DELAY 解释为永久等待，有限超时必须避开该值。
 * 超长有限等待分段执行，外层按剩余预算重试；定时器和延时保留完整数值范围。 */
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

    /* 成功后重复调用直接返回，不重建资源，也不清除错误码或故障记录。 */
    if (initialized) return A_STATUS_OK;
    /* 首次初始化只能在启动阶段串行执行；运行或挂起的调度器都不符合约定。
     * 这是 aOS 的生命周期约束；本函数只准备资源，不启动调度器。 */
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
    /* 所有启用的资源准备成功后才标记完成，失败时仍可在启动前重试。 */
    initialized = A_TRUE;
    return A_STATUS_OK;
}

/* 输入是未移位的 NVIC 优先级，数值越小表示中断越紧急。
 * 调用 FromISR 接口的中断必须能被内核临界区屏蔽；这里只校验，不设置 NVIC。 */
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
        /* 内核负责删除任务；业务参数、持有的锁与内存由应用先行清理。 */
        vTaskDelete((TaskHandle_t)handle);
    }
}

void aOSTaskExit(void)
{
    /* NULL 的“删除当前任务”语义仅封装在后端，不暴露给应用层。 */
    vTaskDelete(NULL);
    /* 正常情况下不会继续执行；若后端异常返回，保持本接口不返回的约定。 */
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
    /* 先写详细信息，再发布故障码，便于调试器观察。
     * context 仅借用指针；此顺序不提供多写者互斥或完整快照同步。 */
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
    /* 公共接口以字节配置栈，FreeRTOS 以 StackType_t 个数接收，需向上取整。
     * 栈容量为 0 时使用后端默认值，不能直接将字节数传给 xTaskCreate。 */
    const size_t words = stack_bytes == 0U
        ? configMINIMAL_STACK_SIZE
        : ALIB_DIV_ROUND_UP(stack_bytes, sizeof(StackType_t));
    TaskHandle_t task = NULL;
    /* 入口与参数直接交给内核；任务结束时必须显式调用 aOSTaskExit()。
     * 新任务可能在创建调用返回前运行，argument 须提前准备并保持有效。 */
    const BaseType_t result = xTaskCreate(function, 
                                        name, 
                                        (uint16_t)words,
                                        argument,
                                        (UBaseType_t)priority,
                                        &task);
    if (result == pdPASS && handle != NULL) *handle = task;
    return result == pdPASS ? A_STATUS_OK : A_STATUS_NO_MEMORY;
}

void aOSRun(void)
{
    vTaskStartScheduler();
    /* 正常运行不返回；若启动失败等原因导致返回，记录故障并停止继续执行。 */
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
    /* 按上下文选择 tick 读取接口；返回 OS 毫秒时基，允许 uint32_t 自然回绕。
     * 这不是日期时间，调度器启动前通常也不会推进。 */
    const TickType_t ticks = xPortIsInsideInterrupt() ?
                            xTaskGetTickCountFromISR() : xTaskGetTickCount();

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

    /* 调用方须先停止等待者和通知源；这里不会取消在途等待或主动唤醒。 */
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
    /* 总等待预算只建立一次，重复唤醒不能重新获得完整超时时间。 */
    end = aTimepointCalc(timeout, aOSGetUptimeMs());

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        /* 调度器未运行时仍可消费已锁存的通知，但不能进入阻塞等待。 */
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
        /* 一个对象只登记一个等待任务，第二个任务不能抢占已有等待者。 */
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
        /* 检查 pending 与登记等待者必须连续完成。
         * 退出临界区后即使立刻收到通知，任务通知槽也会保留唤醒信息。 */
        wait_object->waiting_task = current_task;
        taskEXIT_CRITICAL();

        ticks = remaining.type == A_TIMEOUT_TYPE_FOREVER
                    ? portMAX_DELAY
                    : finite_wait_ticks(remaining.milliseconds);
        /* 通知槽仅用于唤醒；返回后重新检查本对象的 pending 和剩余时间，
         * 避免将同一任务通知槽中的残留唤醒误认成本对象的新通知。 */
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
    /* 已有通知时直接合并；等待者稍后消费 pending，不累计发生次数。 */
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

    /* ISR 使用保存/恢复屏蔽状态的临界区，不能混用任务版进入/退出接口。
     * 调用者的中断优先级必须符合 FreeRTOS 系统调用限制。 */
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
    /* 若唤醒更高优先级任务，请求在退出中断后进行任务切换。 */
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

#if AOS_WORKQUEUE_ENABLE
void aOSWorkItemInit(aOSWorkItem_t *item,
                      aOSWorkFunction_t function, void *argument)
{
    /* 仅初始化空闲工作项；排队或执行期间重置会破坏链表与取消状态。 */
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
    /* 已排队则合并，取消中则拒绝；正在执行但未排队的项可再排队一次。 */
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
    /* 工作任务生命周期固定；退出临界区后通知，缩短队列的中断屏蔽窗口。 */
    if (notify) {
        (void)xTaskNotifyGiveIndexed((TaskHandle_t)s_work_task,
                                     AOS_WORK_NOTIFICATION_INDEX);
    }
    return A_STATUS_OK;
}

/* 入队和合并规则与任务路径一致，仅临界区、通知及调度请求改用 ISR 接口。 */
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
    /* 取消需要遍历单向链表，复杂度 O(n)；正常入队和出队均为 O(1)。 */
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
    /* 无法中断已进入的回调，只能拒绝重新提交并等待其退出。
     * 返回 BUSY 时工作任务仍持有此项，调用者不能释放工作项或参数。 */
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
    /* 同步取消会等待，ISR 与工作任务自身不能调用，避免阻塞或等待自己。
     * 调用前应用须停止其他提交者，取消成功并不永久禁止后续重新提交。 */
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
    /* 每次只在短临界区检查状态，在区外延时；这里不创建额外等待对象。
     * 调用方需先停止新提交，成功仅表示观察到该工作项已空闲。 */
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

#endif /* 共享工作队列 */

void aOSNotifyGive(aOSWaitObject_t object)
{
    /* 给设备事件提供统一入口，自动选上下文；中断优先级限制仍然有效。 */
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

    /* 停止后 armed 为假，抑制尚未进入的回调；重启后检查新计时时长，
     * 过滤旧计时产生的提前到期处理。无符号差值允许 tick 自然回绕。
     * 回调在定时服务任务的临界区外执行，仍须短小且不能阻塞。 */
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
    /* 回调可以自行重新启动计时，退出时只清 running，不覆盖新的 armed。 */
    taskENTER_CRITICAL();
    timer->running = A_FALSE;
    taskEXIT_CRITICAL();
}

static void os_timer_quiesced(void *argument, uint32_t unused)
{
    (void)unused;
    /* 此确认与删除命令在定时服务队列中按 FIFO 顺序处理。
     * 执行至此时删除已完成，定时器回调不再访问包装对象，通知销毁方回收。 */
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
    /* 创建时一次性准备删除确认信号量，Start/Stop 不再额外分配内存。 */
    timer->quiesced = xSemaphoreCreateBinary();
    if (timer->quiesced == NULL) {
        aOSFree(timer);
        return A_STATUS_NO_MEMORY;
    }
    /* pdFALSE 表示一次性计时；初始周期只用于创建，实际周期由 Start 设置。 */
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
    /* 删除中不允许启动；回调正在执行时只允许其自身重新计时，
     * 其他任务须等回调退出，避免旧回调与外部重新启动交错。 */
    if (timer->deleting || (timer->running &&
        xTaskGetCurrentTaskHandle() != xTimerGetTimerDaemonTaskHandle())) {
        taskEXIT_CRITICAL();
        return A_STATUS_BUSY;
    }
    /* 成功只表示命令入队。与状态更新置于同一临界区，且入队不阻塞；
     * 队列满时保留原有计时状态，不把未接受的新周期记为已生效。 */
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
    /* 先关闭回调入口，即使停止命令因队列满失败，也抑制后续到期回调。
     * 已进入的回调不会被打断，彻底回收资源需等待 Destroy 成功。 */
    timer->armed = A_FALSE;
    BaseType_t accepted = timer->deleting ? pdPASS
        : xTimerStop(timer->timer, 0U);
    taskEXIT_CRITICAL();
    return accepted == pdPASS ? A_STATUS_OK : A_STATUS_BUSY;
}

/* 销毁分两步：提交删除，再等待定时服务确认；命令入队不等于资源已停用。
 * 调用方须串行化生命周期操作，等待期间不能持有回调所需的锁。
 * 这里只等待定时器回调结束，不等待该回调投递到其他模块的工作。 */
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
    /* 定时服务任务不能等待自己处理后续确认命令，否则会死锁。 */
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
        /* 保留包装与删除状态，后续可重试销毁；此时不能释放业务参数。 */
        return A_STATUS_BUSY;
    }
    /* 收到队列确认后才释放包装和信号量，并清空调用方句柄。 */
    (void)xSemaphoreTake(timer->quiesced, portMAX_DELAY);
    vSemaphoreDelete(timer->quiesced);
    *timer_object = NULL;
    aOSFree(timer);
    return A_STATUS_OK;
}

/* 任务临界区由内核维护嵌套；ISR 临界区通过令牌恢复进入前的屏蔽状态。
 * 当前端口只屏蔽允许调用 OS 的那部分中断，不等价于屏蔽所有中断。
 * 临界区内只做短小状态操作，不能执行阻塞等待。 */
void aOSCriticalEnter(void) { taskENTER_CRITICAL(); }
void aOSCriticalExit(void) { taskEXIT_CRITICAL(); }
aOSCriticalState_t aOSCriticalEnterFromISR(void)
{ return (aOSCriticalState_t)taskENTER_CRITICAL_FROM_ISR(); }
void aOSCriticalExitFromISR(aOSCriticalState_t state)
{ taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)state); }

/* 创建普通任务互斥锁，提供内核优先级继承；输出槽必须先置为 NULL。
 * 普通锁不能由同一任务重复持有，需要嵌套获取时使用下方递归锁接口。 */
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

    /* 不主动停止持锁者或唤醒等待者，应用须先保证没有任务再使用此锁。 */
    vSemaphoreDelete((SemaphoreHandle_t)*mutex);
    *mutex = NULL;
}

/* 仅任务/启动上下文使用；调度器未运行时只接受立即尝试。
 * NO_WAIT 失败返回 BUSY，有限等待耗尽返回 TIMEOUT，FOREVER 单独映射。
 * 总等待预算保持不变，后端提前返回或超长等待分段都不会延长总超时。 */
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
        /* 分段等待结束或后端提前返回后，按剩余预算继续尝试。 */
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

/* 递归锁允许持锁任务重复获取；其他任务仍互斥，释放次数必须与获取次数一致。 */
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

/* 超时与普通互斥锁一致；递归计数由 FreeRTOS 管理，使用配套的获取/释放 API。 */
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
        /* 分段等待结束或后端提前返回后，按剩余预算继续尝试。 */
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

/* 错误码属于调用任务；启动前使用独立槽，启动后不自动迁移给某个任务。
 * 不用于 ISR，避免将中断中的错误覆盖到被打断任务的错误状态。 */
aErrno_t aOSGetErrno(void)
{
    TaskHandle_t task;

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return s_pre_scheduler_errno;
    }

    task = xTaskGetCurrentTaskHandle();
    /* TLS 槽保存错误码的整数编码，不是可解引用的地址，也不需要分配内存。 */
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

/* 供读写接口统一使用：保存任务错误码，并以 -1 表示操作失败。 */
aSSize_t aOSFailWithStatus(aStatus_t status)
{
    aOSSetErrno(aStatusToErrno(status));
    return -1;
}

aSSize_t aOSFailWithTimeout(aTimeout_t timeout)
{
    /* 零等待未完成表示暂不可用；需要等待的操作失败则记录超时。 */
    const aBool_t no_wait =
        (timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
        (timeout.milliseconds == 0U);

    aOSSetErrno(no_wait ? A_EAGAIN : A_ETIMEDOUT);
    return -1;
}

/* 供轮询循环检查超时；尚未到期时只让出调度机会，不主动延时。
 * 调用者仍需控制轮询开销，yield 不保证低优先级任务获得运行机会。 */
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
    /* 分配失败允许由上层恢复；这里只记录，调用方决定返回错误或停止系统。 */
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    aOSRecordFault(AOS_FAULT_STACK_OVERFLOW, A_STATUS_ERROR, task_name);
    /* 栈已溢出，继续运行可能破坏其他内存；记录后屏蔽内核管理范围的中断并停留。 */
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}
