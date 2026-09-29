/**
 * @file aOS.h
 * @brief 跨平台任务、时基、等待、工作项及任务错误状态接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 当前实现为 FreeRTOS；对外不暴露 FreeRTOS 类型。
 * 除明确以 FromISR 命名的函数外，默认仅供任务/启动上下文使用。
 * 阻塞 API 需要调度器运行；共享工作/定时器回调虽在任务上下文，仍禁止阻塞。
 * 对象创建通常分配 OS 内存；销毁前调用方须停止全部使用者，不支持并发热销毁。
 */

#ifndef AOS_H
#define AOS_H

#include "aLib.h"
#include "aStatus.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 应用任务优先级预设，数值越大优先级越高；与 NVIC 规则不同。
 * 使用前确保后端最大优先级数量覆盖所选值；REALTIME 仅是等级名，
 * 不保证硬实时截止时间。
 */
#define AOS_TASK_PRIO_LOWEST 1U
#define AOS_TASK_PRIO_LOW 2U
#define AOS_TASK_PRIO_BELOW_NORMAL 3U
#define AOS_TASK_PRIO_NORMAL 4U
#define AOS_TASK_PRIO_ABOVE_NORMAL 5U
#define AOS_TASK_PRIO_HIGH 6U
#define AOS_TASK_PRIO_REALTIME 7U

/**
 * @brief 任务入口，由后端调用；允许正常返回，后端负责结束当前任务。
 * @param[in] argument 创建任务时传入的借用参数，可以为 NULL。
 * @note 业务负责退出协议和资源清理，禁止在仍持有共享资源时删除任务。
 */
typedef void (*aOSTaskFunction_t)(void *argument);

/** @brief 借用的 OS 任务标识，删除后失效；调用者不可解引用。 */
typedef void *aOSTaskHandle_t;

/** @brief 任务创建配置；创建期间读取字段，不保存配置结构体指针。 */
typedef struct {
    const char *name; /**< 后端在创建时复制名称，可按后端长度上限截断。 */
    aOSTaskFunction_t function; /**< 必填入口，允许自然返回。 */
    void *argument; /**< 借用参数，其对象须在任务访问期间有效。 */
    size_t stack_bytes; /**< 字节数，0 使用后端默认容量；后端向上对齐。 */
    uint32_t priority; /**< AOS_TASK_PRIO_LOWEST..REALTIME。 */
} aOSTaskConfig_t;

/** @brief 静态/局部初始化默认值；使用前必须设置 function。 */
#define AOS_TASK_CONFIG_DEFAULT { "task", NULL, NULL, 0U, AOS_TASK_PRIO_NORMAL }

/** @brief 重置所有字段为默认值；config 为 NULL 时不操作。 */
static inline void aOSTaskConfigStructInit(aOSTaskConfig_t *config)
{
    if (config != NULL) {
        const aOSTaskConfig_t defaults = AOS_TASK_CONFIG_DEFAULT;
        *config = defaults;
    }
}


/**
 * @brief 可合并通知的不透明等待对象。
 * 同一时刻只允许一个等待任务；多次通知可能合并，唤醒后必须再次检查业务条件。
 * 对象由 Create 分配并由 Destroy 释放，调用方不得解引用。
 */
typedef void *aOSWaitObject_t;

/** @brief 不透明任务互斥锁，不能从 ISR 获取或释放。 */
typedef void *aOSMutex_t;

/** @brief 当前持锁任务可重复获取的递归锁，获取/释放次数必须匹配。 */
typedef void *aOSRecursiveMutex_t;

/** @brief 由 aOSTimerCreate 创建的一次性 OS 软件定时器。 */
typedef void *aOSTimer_t;

/**
 * @brief 一次性定时器服务任务回调，不是硬件 ISR。
 * @param[in] argument 创建定时器时传入的借用参数，可以为 NULL。
 * @warning 不得阻塞或执行耗时业务，否则推迟其他软件定时器的处理。
 */
typedef void (*aOSTimerCallback_t)(void *argument);

/** @brief ISR 临界区保存令牌，不应由调用者构造。 */
typedef uintptr_t aOSCriticalState_t;

typedef struct aOSWorkItem aOSWorkItem_t;

/**
 * @brief 共享工作任务回调，与其他工作项串行执行。
 * @param[in] argument 提交时传入的借用参数，可以为 NULL。
 * @warning 任务上下文不意味着可阻塞；耗时业务应转交应用自己的任务。
 */
typedef void (*aOSWorkFunction_t)(void *argument);

/** @brief 调用方存储的工作项；字段由 aOS 管理，不可在运行/排队期间改写。 */
struct aOSWorkItem {
    aOSWorkItem_t *next; /**< 内部工作队列链接。 */
    aOSWorkFunction_t function; /**< 已提交的回调。 */
    void *argument; /**< 借用的回调参数。 */
    aBool_t queued; /**< 是否处于待执行队列。 */
    aBool_t running; /**< 是否正在执行回调。 */
    aBool_t canceling; /**< 正在取消，拒绝执行中的工作项重新提交。 */
};

/** @brief 最近一次诊断事件；不代表所有错误都有对应自动处理。 */
typedef enum {
    AOS_FAULT_NONE = 0U,
    AOS_FAULT_APP_INIT = 1U,
    AOS_FAULT_SCHEDULER_RETURNED = 2U,
    AOS_FAULT_MALLOC_FAILED = 3U,
    AOS_FAULT_STACK_OVERFLOW = 4U,
} aOSFaultCode_t;

/** @brief 调试器可读取的最近故障快照，不是持久化或多条日志。 */
typedef struct {
    volatile uint32_t code; /**< aOSFaultCode_t 数值，最后发布。 */
    volatile int32_t status; /**< 关联的 aStatus_t 数值。 */
    const char *volatile context; /**< 借用字符串或 NULL，不复制文本。 */
} aOSFaultRecord_t;

/** @brief 全局调试记录；通过 aOSRecordFault 写入，context 是借用指针。 */
extern aOSFaultRecord_t g_aOSFaultRecord;

/**
 * @brief 初始化 OS 适配公共状态，仅在启用 workqueue 时创建共享工作任务。
 * @retval A_STATUS_OK 初始化成功；重复调用不会重新创建资源。
 * @retval A_STATUS_NO_MEMORY 无法创建工作任务。
 * @retval A_STATUS_NOT_READY 首次调用时调度器已经启动。
 * @note 首次初始化必须在调度器启动前串行调用，且不能从 ISR 调用。
 * 不启动调度器；重复调用不重复分配资源，也不清除 errno 或故障记录。
 */
aStatus_t aOSInit(void);

/**
 * @brief 检查 IRQ 优先级能否调用 aOS 的 FromISR 接口。
 * @param[in] priority 未移位的逻辑中断优先级。
 * @retval A_STATUS_OK 落在当前 RTOS 允许范围。
 * @retval A_STATUS_INVALID_PARAM 优先级过高或超出实现范围。
 * @note 不设置 NVIC，仅校验；数字较小不代表更低的硬件优先级。
 */
aStatus_t aOSValidateIsrPriority(uint32_t priority);

/**
 * @brief 创建任务，不负责应用模块初始化。
 * @param[in] config 必填配置，只在调用期间读取；名称由后端复制，argument 不深拷贝。
 * @param[out] handle 可选输出，NULL 表示不获取句柄；失败时清空输出。
 * @retval A_STATUS_OK 创建成功。
 * @retval A_STATUS_INVALID_PARAM 配置、入口或名称为空，栈容量超出后端范围或优先级无效。
 * @retval A_STATUS_NO_MEMORY 分配失败。
 * @note 仅启动阶段或任务上下文调用。调度器启动后，新任务可能在本函数返回前运行。
 * 使用 aOSTaskConfigStructInit 或 AOS_TASK_CONFIG_DEFAULT 设置默认值；全零配置无效。
 * 逻辑优先级由后端映射，不保证不同 OS 的调度效果一致。
 */
aStatus_t aOSCreateTask(const aOSTaskConfig_t *config, aOSTaskHandle_t *handle);

/**
 * @brief 强制删除指定任务（后端受限能力）；普通关闭优先使用协作退出。
 * @param[in] handle 有效任务句柄；NULL 不操作，不表示删除当前任务。
 * @warning 不自动释放业务资源；先停止任务访问共享资源，禁止持锁强制删除。
 */
void aOSDeleteTask(aOSTaskHandle_t handle);

/**
 * @brief 正常结束当前应用任务，不返回。
 * @note 仅在调度器运行时的应用任务上下文调用；不能从 ISR、临界区、
 *       调度器挂起区或 OS 服务回调（定时器/workqueue）中调用。
 * @warning 调用前必须释放持有的锁和业务资源，并结束对该任务的外部引用。
 *          不自动清理业务内存；FreeRTOS 后端的栈/任务控制块由空闲任务稍后回收。
 */
void aOSTaskExit(void) ALIB_NORETURN;

/**
 * @brief 从 OS 堆分配内存，不清零。
 * @param[in] size 字节数。
 * @return 内存指针；size 为 0 或内存不足返回 NULL。
 * @note 任务/启动上下文使用；必须用 aOSFree() 释放。
 */
void *aOSAlloc(size_t size);

/**
 * @brief 释放由 aOSAlloc() 返回的内存。
 * @param[in] memory 原分配指针；NULL 不操作。
 * @warning 不得重复释放、混用 C malloc 内存或释放仍被 ISR/回调访问的对象。
 */
void aOSFree(void *memory);

/**
 * @brief 启动调度器，不返回。
 * @note 启动阶段完成任务与设备初始化后调用；当前后端调度器返回则停留在循环。
 */
void aOSRun(void) ALIB_NORETURN;

/**
 * @brief 让当前任务延时。
 * @param[in] milliseconds 毫秒数，按 OS tick 向上取整；0 不提供实际睡眠时长。
 * @warning 仅调度器运行中的任务上下文，不能在 ISR 或持有临界区时调用。
 */
void aOSDelayMs(uint32_t milliseconds);

/**
 * @brief 主动请求一次任务调度。
 * @note 调度器未运行时不操作；yield 不是阻塞睡眠，不保证低优先级任务获得 CPU。
 */
void aOSYield(void);

/**
 * @brief 读取 OS 单调运行时基。
 * @return uint32_t 毫秒计数，允许自然回绕，精度由 OS tick 决定。
 * @note 不是真实日期时间；FreeRTOS 调度器未运行时 tick 通常不推进。任务上下文使用。
 */
uint32_t aOSGetUptimeMs(void);

/**
 * @brief 创建可合并通知的单等待者对象。
 * @param[in,out] object 输出槽，初始 *object 必须为 NULL。
 * @retval A_STATUS_OK 创建成功。
 * @retval A_STATUS_INVALID_PARAM 输出槽为空或已含对象。
 * @retval A_STATUS_NO_MEMORY 分配失败。
 */
aStatus_t aOSWaitObjectCreate(aOSWaitObject_t *object);

/**
 * @brief 释放等待对象并将输出槽设为 NULL。
 * @param[in,out] object 对象槽；NULL 或空对象不操作。
 * @warning 先停止全部等待者和通知源；此函数不会唤醒或取消在途等待。
 */
void aOSWaitObjectDestroy(aOSWaitObject_t *object);

/**
 * @brief 等待并消费一次通知，返回后必须重新检查业务条件。
 * @param[in] object 有效等待对象，同一时刻仅允许一个等待任务。
 * @param[in] timeout NO_WAIT、有限毫秒或 FOREVER。
 * @retval A_STATUS_OK 消费到通知，不保证业务条件仍成立。
 * @retval A_STATUS_BUSY 第二个等待者或 NO_WAIT 无通知。
 * @retval A_STATUS_TIMEOUT 有限等待到期。
 * @retval A_STATUS_NOT_READY 调度器未运行且需要等待。
 * @retval A_STATUS_INVALID_PARAM 对象或超时无效。
 * @note FreeRTOS 使用保留的任务通知索引 1；业务不能占用该槽。
 */
aStatus_t aOSWaitObjectWait(aOSWaitObject_t object, aTimeout_t timeout);

/**
 * @brief 在任务上下文置通知并唤醒等待者。
 * @param[in] object 有效对象；NULL 不操作。
 * @note 多次通知可合并，不是计数信号量；先通知后等待不会丢失 pending 标志。
 */
void aOSWaitObjectNotify(aOSWaitObject_t object);

/**
 * @brief 在合法优先级 ISR 中发通知，必要时请求切换任务。
 * @param[in] object 有效对象；NULL 不操作。
 * @note 生命周期与合并语义同 Notify；不可从不允许 OS 调用的高优先级 ISR 使用。
 */
void aOSWaitObjectNotifyFromISR(aOSWaitObject_t object);

/**
 * @brief 非阻塞条件通知，自动选择任务或 ISR 后端。
 * @param[in] object 等待对象；NULL 不操作，对象在调用期间必须有效。
 * @note 可从设备事件回调调用；重复通知合并，不存储事件或数据。
 *       ISR 优先级必须符合 OS API 限制。等待仍使用 aOSWaitObjectWait。
 */
void aOSNotifyGive(aOSWaitObject_t object);

#if AOS_WORKQUEUE_ENABLE
/**
 * @brief 查询是否在系统 workqueue 线程中，仅任务上下文调用。
 * @return 是则 A_TRUE，否则 A_FALSE。
 */
aBool_t aOSIsWorkContext(void);

/**
 * @brief 初始化工作项并固定处理函数，不提交执行。
 * @param[out] item 应用长期持有的工作项。
 * @param[in] function 非空处理函数，在线程上下文执行。
 * @param[in] argument 借用参数，保持有效至取消/排空成功。
 * @warning 只能初始化空闲项；运行期间不得修改处理函数、参数或工作项存储。
 */
void aOSWorkItemInit(aOSWorkItem_t *item,
                      aOSWorkFunction_t function, void *argument);

/**
 * @brief 提交系统队列，自动选择任务/ISR 路径。
 * @param[in,out] item 已初始化工作项。
 * @retval A_STATUS_OK 已排队，或已在队列中合并本次提交（不改变原位置）。
 * @retval A_STATUS_BUSY 正在取消。
 * @retval A_STATUS_NOT_READY 服务未初始化。
 * @retval A_STATUS_INVALID_PARAM 工作项/处理函数无效。
 * @note 执行中的项可再排队一次；无堆分配。ISR 优先级必须符合 OS 限制。
 */
aStatus_t aOSWorkSubmit(aOSWorkItem_t *item);

/**
 * @brief 非阻塞取消排队执行，任务/ISR 均可调用。
 * @param[in,out] item 已初始化工作项。
 * @retval A_STATUS_OK 已空闲或成功移除排队项。
 * @retval A_STATUS_BUSY 处理函数仍在执行；拒绝其重新提交直到退出。
 * @retval A_STATUS_INVALID_PARAM 空指针。
 * @note 不打断正在执行的函数，返回 BUSY 时不能释放对象。
 */
aStatus_t aOSWorkCancel(aOSWorkItem_t *item);

/**
 * @brief 取消并等待在途函数退出，仅普通任务调用。
 * @param[in,out] item 工作项；调用前须阻止其他生产者再次提交。
 * @param[in] timeout 总等待预算。
 * @return OK 表示可回收对象；TIMEOUT 表示仍被引用；工作线程/ISR 返回 BUSY。
 * @note 失败不能释放对象；不得持有工作函数所需的锁。
 */
aStatus_t aOSWorkCancelSync(aOSWorkItem_t *item, aTimeout_t timeout);

/**
 * @brief 等待工作项不再排队或运行，不取消。
 * @param[in,out] item 有效工作项。
 * @param[in] timeout 等待预算；调用前须停止新的提交。
 * @return OK 已空闲，TIMEOUT 超时，BUSY 上下文不允许等待，INVALID_PARAM 参数错误。
 * @note 生命周期同步而非消息计数；当前后端使用 1 ms 延时检查。
 */
aStatus_t aOSWorkWaitIdle(aOSWorkItem_t *item, aTimeout_t timeout);
#endif

/**
 * @brief 创建一次性软件定时器，尚不启动。
 * @param[in,out] timer 输出槽，初始值必须为 NULL。
 * @param[in] callback 非空回调，在 OS 定时器服务任务执行，不得阻塞。
 * @param[in] argument 回调参数；必须保持有效至定时器不再访问。
 * @retval A_STATUS_OK 创建成功。
 * @retval A_STATUS_INVALID_PARAM 参数为空或已有定时器。
 * @retval A_STATUS_NO_MEMORY 分配失败。
 * @retval A_STATUS_NOT_READY ISR 上下文调用。
 */
aStatus_t aOSTimerCreate(aOSTimer_t *timer, aOSTimerCallback_t callback,
                         void *argument);

/**
 * @brief 启动/重新计时一次性定时器。
 * @param[in] timer 有效定时器。
 * @param[in] milliseconds 正延时，按 OS tick 向上取整。
 * @retval A_STATUS_OK 控制命令已入队。
 * @retval A_STATUS_BUSY 命令队列满、正在销毁或其他上下文回调尚未退出。
 * @retval A_STATUS_INVALID_PARAM 空对象或延时为零。
 * @retval A_STATUS_NOT_READY ISR 上下文调用。
 * @note 仅任务上下文，不同步等待定时器服务处理命令；允许自身回调重新计时。
 *       生命周期操作由调用方串行化；失败不替换上一次有效计时。
 */
aStatus_t aOSTimerStart(aOSTimer_t timer, uint32_t milliseconds);

/**
 * @brief 立即禁用尚未进入的回调，并尝试投递停止命令。
 * @param[in] timer 有效对象；NULL 不操作。
 * @retval A_STATUS_OK 命令已入队或对象为空。
 * @retval A_STATUS_BUSY 命令队列满，但软件回调已被禁用。
 * @retval A_STATUS_NOT_READY ISR 上下文调用，未修改对象。
 * @note 仅任务上下文，非阻塞；已进入的回调可继续执行。不是退出屏障。
 */
aStatus_t aOSTimerStop(aOSTimer_t timer);

/**
 * @brief 同步删除定时器，等待服务队列屏障后释放资源。
 * @param[in,out] timer 对象槽，成功置 NULL；失败保留对象供重试，空槽成功。
 * @retval A_STATUS_OK 在途回调已退出，定时器不再访问 argument。
 * @retval A_STATUS_NOT_READY ISR 或调度器未运行。
 * @retval A_STATUS_BUSY 定时服务自身调用，或命令提交失败。
 * @note 仅普通任务；可能阻塞。不得持有回调所需的锁，不得与其他生命周期
 *       操作并发。回调投递的工作须由调用方另行排空，本屏障不等待其他任务。
 */
aStatus_t aOSTimerDestroy(aOSTimer_t *timer);

/**
 * @brief 进入当前后端任务临界区。
 * @note 与 Exit 成对；临界区内只能执行短小且不阻塞的操作，不提供跨核互斥。
 */
void aOSCriticalEnter(void);

/**
 * @brief 退出一层由当前任务进入的临界区。
 * @warning 必须配对 Enter，不可用于恢复 ISR 中断屏蔽状态。
 */
void aOSCriticalExit(void);

/**
 * @brief 在 ISR 中保存并设置中断屏蔽状态。
 * @return 交给对应 ExitFromISR 的恢复令牌。
 * @note 仅合法 OS 中断优先级；不屏蔽全部更高优先级中断。
 */
aOSCriticalState_t aOSCriticalEnterFromISR(void);

/**
 * @brief 恢复 ISR 进入临界区前的屏蔽状态。
 * @param[in] state 对应 EnterFromISR 返回的原始令牌，按嵌套逆序恢复。
 */
void aOSCriticalExitFromISR(aOSCriticalState_t state);

/**
 * @brief 创建非递归任务互斥锁。
 * @param[in,out] mutex 输出槽，初始 *mutex 必须为 NULL。
 * @retval A_STATUS_OK 已创建。
 * @retval A_STATUS_INVALID_PARAM 输出槽为空或已有锁。
 * @retval A_STATUS_NO_MEMORY 无法分配。
 */
aStatus_t aOSMutexCreate(aOSMutex_t *mutex);

/**
 * @brief 销毁互斥锁并清空对象槽。
 * @param[in,out] mutex 对象槽；NULL 或空对象不操作。
 * @warning 必须没有持锁者、等待者和新访问者；不取消等待，不在 ISR 调用。
 */
void aOSMutexDestroy(aOSMutex_t *mutex);

/**
 * @brief 在任务上下文获取不可递归互斥锁。
 * @param[in] mutex 有效互斥锁。
 * @param[in] timeout NO_WAIT、有限毫秒或 FOREVER。
 * @retval A_STATUS_OK 已取得锁。
 * @retval A_STATUS_BUSY NO_WAIT 获取失败。
 * @retval A_STATUS_TIMEOUT 有限等待失败。
 * @retval A_STATUS_NOT_READY 调度器未运行且要求阻塞。
 * @retval A_STATUS_INVALID_PARAM 对象或超时无效。
 */
aStatus_t aOSMutexLock(aOSMutex_t mutex, aTimeout_t timeout);

/**
 * @brief 由持锁任务释放互斥锁。
 * @param[in] mutex 当前任务持有的有效锁。
 * @retval A_STATUS_OK 释放成功。
 * @retval A_STATUS_INVALID_PARAM 对象为空。
 * @retval A_STATUS_ERROR 后端释放失败。
 * @warning 不能跨任务释放，也不能在 ISR 调用；错误所有者可能触发后端断言。
 */
aStatus_t aOSMutexUnlock(aOSMutex_t mutex);

/**
 * @brief 创建递归任务互斥锁。
 * @param[in,out] mutex 输出槽，初始 *mutex 必须为 NULL。
 * @retval A_STATUS_OK 已创建。
 * @retval A_STATUS_INVALID_PARAM 输出槽为空或已有锁。
 * @retval A_STATUS_NO_MEMORY 无法分配。
 */
aStatus_t aOSRecursiveMutexCreate(aOSRecursiveMutex_t *mutex);

/**
 * @brief 销毁互斥锁并清空对象槽。
 * @param[in,out] mutex 对象槽；NULL 或空对象不操作。
 * @warning 必须没有持锁者、等待者和新访问者；不取消等待，不在 ISR 调用。
 */
void aOSRecursiveMutexDestroy(aOSRecursiveMutex_t *mutex);

/**
 * @brief 在任务上下文获取可递归互斥锁。
 * @param[in] mutex 有效互斥锁。
 * @param[in] timeout NO_WAIT、有限毫秒或 FOREVER。
 * @retval A_STATUS_OK 已取得锁；每次成功都必须匹配一次 Unlock。
 * @retval A_STATUS_BUSY NO_WAIT 获取失败。
 * @retval A_STATUS_TIMEOUT 有限等待失败。
 * @retval A_STATUS_NOT_READY 调度器未运行且要求阻塞。
 * @retval A_STATUS_INVALID_PARAM 对象或超时无效。
 */
aStatus_t aOSRecursiveMutexLock(aOSRecursiveMutex_t mutex,
                               aTimeout_t timeout);

/**
 * @brief 由持锁任务释放一层递归锁。
 * @param[in] mutex 当前任务持有的有效锁。
 * @retval A_STATUS_OK 释放成功。
 * @retval A_STATUS_INVALID_PARAM 对象为空。
 * @retval A_STATUS_ERROR 后端释放失败。
 * @warning 不能跨任务释放，也不能在 ISR 调用；错误所有者可能触发后端断言。
 */
aStatus_t aOSRecursiveMutexUnlock(aOSRecursiveMutex_t mutex);

/**
 * @brief 写入调试用全局故障记录，不停机也不打印。
 * @param[in] code 故障类型，最后写入作为标记。
 * @param[in] status 关联状态码。
 * @param[in] context 借用的上下文字符串，可 NULL；记录存续期间须有效。
 * @warning 不复制字符串，也不是多写者原子日志；调用方避免并发覆盖。
 */
void aOSRecordFault(aOSFaultCode_t code, aStatus_t status,
                    const char *context);

/**
 * @brief 获取当前任务的项目错误码。
 * @return 当前任务错误槽；调度器启动前使用独立的启动错误槽。
 * @note 仅在流式 API 返回 -1 后解释；成功不自动清零，不是 C errno。
 */
aErrno_t aOSGetErrno(void);

/**
 * @brief 设置当前任务的项目错误码。
 * @param[in] error aErrno_t 值。
 * @warning 仅任务/启动上下文，不从 ISR 修改被中断任务的错误槽。
 */
void aOSSetErrno(aErrno_t error);

/**
 * @brief 映射错误状态并构造流式失败返回值。
 * @param[in] status 失败状态，不应传入 A_STATUS_OK。
 * @return 恒为 -1，同时通过 aStatusToErrno() 设置任务 errno。
 */
aSSize_t aOSFailWithStatus(aStatus_t status);

/**
 * @brief 为无进展的等待到期生成流式错误。
 * @param[in] timeout 原请求超时，不是剩余预算。
 * @return 恒为 -1；NO_WAIT 设置 A_EAGAIN，其余设置 A_ETIMEDOUT。
 * @note 仅已确定失败时调用，不执行等待或有效性检查。
 */
aSSize_t aOSFailWithTimeout(aTimeout_t timeout);

/**
 * @brief 查询截止状态，尚未到期时 yield 一次。
 * @param[in] timepoint 操作截止状态；NULL 表示不超时。
 * @return 已到期 A_TRUE；否则 A_FALSE。
 * @warning 这是协作式轮询辅助，不会把调用任务挂起睡眠。
 */
aBool_t aOSPollWaitExpired(const aTimepoint_t *timepoint);

#endif
