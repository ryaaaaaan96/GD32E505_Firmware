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

#define AOS_TASK_PRIO_LOWEST 1U
#define AOS_TASK_PRIO_LOW 2U
#define AOS_TASK_PRIO_BELOW_NORMAL 3U
#define AOS_TASK_PRIO_NORMAL 4U
#define AOS_TASK_PRIO_ABOVE_NORMAL 5U
#define AOS_TASK_PRIO_HIGH 6U
#define AOS_TASK_PRIO_REALTIME 7U

/** @brief 任务入口，argument 为创建参数；任务不得直接从入口返回。 */
typedef void (*aOSTaskFunction_t)(void *argument);

/** @brief 借用的 OS 任务标识，删除后失效；调用者不可解引用。 */
typedef void *aOSTaskHandle_t;

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

/** @brief 一次性定时器服务任务回调；argument 为创建参数，不得阻塞。 */
typedef void (*aOSTimerCallback_t)(void *argument);

/** @brief ISR 临界区保存令牌，不应由调用者构造。 */
typedef uintptr_t aOSCriticalState_t;

typedef struct aOSWorkItem aOSWorkItem_t;

/** @brief 共享工作任务回调；argument 为提交参数，不得执行阻塞等待。 */
typedef void (*aOSWorkFunction_t)(void *argument);

/** @brief 调用方存储的工作项；字段由 aOS 管理，不可在运行/排队期间改写。 */
struct aOSWorkItem {
    aOSWorkItem_t *next; /**< 内部工作队列链接。 */
    aOSWorkFunction_t function; /**< 已提交的回调。 */
    void *argument; /**< 借用的回调参数。 */
    aBool_t queued; /**< 是否处于待执行队列。 */
    aBool_t running; /**< 是否正在执行回调。 */
};

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
 * @brief 初始化 OS 适配公共状态并创建共享工作任务。
 * @retval A_STATUS_OK 初始化成功；已有工作任务时不会重复创建。
 * @retval A_STATUS_NO_MEMORY 无法创建工作任务。
 * @note 启动阶段串行调用；不启动调度器。重复调用仍会重置启动 errno 和故障记录。
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
 * @param[in] function 任务入口，不能直接返回。
 * @param[in] name 非空任务名，由后端按名称长度上限保存。
 * @param[in] stack_words 非零栈容量，单位为后端栈字，不是字节。
 * @param[in] argument 原样传给任务；其对象须在任务访问期间有效。
 * @param[in] priority 逻辑优先级，小于后端最大优先级数量。
 * @param[out] handle 可选输出，NULL 表示不获取句柄。
 * @retval A_STATUS_OK 创建成功。
 * @retval A_STATUS_INVALID_PARAM 入口、名称、栈容量或优先级无效。
 * @retval A_STATUS_NO_MEMORY 分配失败。
 * @note 调度器启动后，新任务可能在本函数返回前运行。
 */
aStatus_t aOSCreateTask(aOSTaskFunction_t function, const char *name,
                        uint16_t stack_words, void *argument,
                        uint32_t priority, aOSTaskHandle_t *handle);

/**
 * @brief 删除指定任务。
 * @param[in] handle 有效任务句柄；NULL 不操作，不表示删除当前任务。
 * @warning 不自动释放业务资源；先停止任务访问共享资源，禁止持锁强制删除。
 */
void aOSDeleteTask(aOSTaskHandle_t handle);

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
 * @brief 查询当前任务是否为 aOS 共享工作任务。
 * @return 是则 A_TRUE；未启动调度器或未创建工作任务则 A_FALSE。
 * @note 仅任务上下文使用。
 */
aBool_t aOSIsWorkContext(void);

/**
 * @brief 重置工作项，尚不提交执行。
 * @param[out] item 调用方长期持有的状态，NULL 不操作。
 * @warning 只对未排队且未运行的对象调用；不得初始化覆盖在途工作项。
 */
void aOSWorkItemInit(aOSWorkItem_t *item);

/**
 * @brief 向共享工作任务提交短小的非阻塞回调。
 * @param[in,out] item 已初始化工作项；直到不再排队/运行都必须有效。
 * @param[in] function 非空回调，在工作任务串行执行，不是 ISR。
 * @param[in] argument 原样传入，可为 NULL；依赖对象保持有效至执行结束。
 * @retval A_STATUS_OK 已排队；正在执行但尚未重新排队的项也可再次提交。
 * @retval A_STATUS_BUSY 已在队列中，本次不会覆盖已有回调。
 * @retval A_STATUS_NOT_READY aOSInit 尚未创建工作任务。
 * @retval A_STATUS_INVALID_PARAM item 或 function 为空。
 */
aStatus_t aOSWorkSubmit(aOSWorkItem_t *item,
                        aOSWorkFunction_t function, void *argument);

/**
 * @brief 从 ISR 提交工作项，在共享任务而非 ISR 执行回调。
 * @param[in,out] item 生命周期同 aOSWorkSubmit()。
 * @param[in] function 非空、短小且不得阻塞的回调。
 * @param[in] argument 用户参数，可为 NULL。
 * @retval A_STATUS_OK 已排队。
 * @retval A_STATUS_BUSY 已排队。
 * @retval A_STATUS_INVALID_PARAM 空参数或工作任务尚未创建。
 * @warning IRQ 优先级必须通过 aOSValidateIsrPriority()。
 */
aStatus_t aOSWorkSubmitFromISR(aOSWorkItem_t *item,
                               aOSWorkFunction_t function, void *argument);

/**
 * @brief 等待工作项既未排队也未运行。
 * @param[in,out] item 有效工作项，等待期间不能释放。
 * @param[in] timeout 合法超时，调用方须先停止新的提交。
 * @retval A_STATUS_OK 已空闲。
 * @retval A_STATUS_BUSY 仍忙且当前位于工作任务或调度器未运行。
 * @retval A_STATUS_TIMEOUT 非工作任务等待到期，包含 NO_WAIT 时仍忙。
 * @retval A_STATUS_INVALID_PARAM item 为空。
 * @note 任务上下文生命周期屏障，不取消工作；有效等待期间以 1 ms 延时检查。
 */
aStatus_t aOSWorkWaitIdle(aOSWorkItem_t *item, aTimeout_t timeout);

/**
 * @brief 创建一次性软件定时器，尚不启动。
 * @param[in,out] timer 输出槽，初始值必须为 NULL。
 * @param[in] callback 非空回调，在 OS 定时器服务任务执行，不得阻塞。
 * @param[in] argument 回调参数；必须保持有效至定时器不再访问。
 * @retval A_STATUS_OK 创建成功。
 * @retval A_STATUS_INVALID_PARAM 参数为空或已有定时器。
 * @retval A_STATUS_NO_MEMORY 分配失败。
 */
aStatus_t aOSTimerCreate(aOSTimer_t *timer, aOSTimerCallback_t callback,
                         void *argument);

/**
 * @brief 启动/重新计时一次性定时器。
 * @param[in] timer 有效定时器。
 * @param[in] milliseconds 正延时，按 OS tick 向上取整。
 * @retval A_STATUS_OK 控制命令已入队。
 * @retval A_STATUS_BUSY 定时器命令队列满。
 * @retval A_STATUS_INVALID_PARAM 空对象或延时为零。
 * @note 不同步等待定时器服务处理命令。
 */
aStatus_t aOSTimerStart(aOSTimer_t timer, uint32_t milliseconds);

/**
 * @brief 尝试投递停止定时器的命令。
 * @param[in] timer 有效对象；NULL 不操作。
 * @warning 当前后端不报告队列满错误；返回不是回调退出屏障，也不保证已停止。
 */
void aOSTimerStop(aOSTimer_t timer);

/**
 * @brief 投递停止/删除命令并释放包装对象。
 * @param[in,out] timer 对象槽，返回时置 NULL；空槽不操作。
 * @warning 当前后端没有在途回调退出屏障。调用方须确保回调不会再访问包装对象
 *          或 argument，禁止与到期回调并发销毁；不可在定时器服务回调内阻塞删除。
 */
void aOSTimerDestroy(aOSTimer_t *timer);

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
