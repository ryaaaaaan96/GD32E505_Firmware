/**
 * @file aPoll.h
 * @brief 周期作业、合并事件和临时请求；执行任务由应用创建。
 * 仅任务上下文，多生产者、单消费者；回调在 Process 调用者中执行。
 * 配置数组及 context 借用至销毁，生命周期操作由应用串行编排。
 */
#ifndef A_POLL_H
#define A_POLL_H

#include "aOS.h"

#ifndef APOLL_STATIC_ENABLE
#define APOLL_STATIC_ENABLE 1
#endif
#ifndef APOLL_DYNAMIC_ENABLE
#define APOLL_DYNAMIC_ENABLE 1
#endif

#if (APOLL_STATIC_ENABLE != 0 && APOLL_STATIC_ENABLE != 1) || \
    (APOLL_DYNAMIC_ENABLE != 0 && APOLL_DYNAMIC_ENABLE != 1)
#error "aPoll allocation switches must be 0 or 1"
#endif
#if !APOLL_STATIC_ENABLE && !APOLL_DYNAMIC_ENABLE
#error "aPoll requires an allocation API"
#endif

typedef struct aPollHandle aPollHandle_t;

typedef struct {
    aStatus_t status; /**< 业务执行结果，与调度器错误分开。 */
    aBool_t advance; /**< 已尝试本步骤；通信失败也可以推进。 */
} aPollStepResult_t;

typedef void (*aPollExecuteFn_t)(void *context, size_t step_index,
                               aPollStepResult_t *result);
typedef void (*aPollEventFn_t)(void *context);

/** 执行完整一轮后停止；普通 Trigger 不重新启动。 */
#define APOLL_POLL_ONCE UINT32_MAX

typedef struct {
    aPollExecuteFn_t execute;
    void *context;
    size_t step_count;
    /** 整轮起点最小间隔：0 连续执行，APOLL_POLL_ONCE 仅执行一轮。
     * 普通周期不超过 INT32_MAX；超期不积压补跑。
     */
    uint32_t poll_time_ms;
    uint32_t interval_ms; /**< 本作业成功推进后等待，不阻塞其他作业。 */
    uint32_t error_delay_ms; /**< 失败或未推进后的等待，必须大于零。 */
} aPollJobConfig_t;

typedef struct {
    aPollEventFn_t function;
    void *context;
} aPollEventConfig_t;

typedef struct {
    aPollExecuteFn_t execute; /**< 临时请求只执行一次，step_index 为零。 */
    void *context;
    void (*complete)(void *context, const aPollStepResult_t *result);
} aPollRequest_t;

typedef struct {
    const aPollJobConfig_t *jobs;
    size_t job_count;
    const aPollEventConfig_t *events;
    size_t event_count;
    size_t request_capacity; /**< 为零时不创建队列及队列锁。 */
} aPollConfig_t;

typedef enum {
    APOLL_WORK_NONE,
    APOLL_WORK_JOB,
    APOLL_WORK_EVENT,
    APOLL_WORK_REQUEST
} aPollWorkType_t;

typedef struct {
    aPollWorkType_t type;
    size_t index; /**< 作业或事件下标；临时请求不提供编号。 */
    size_t step_index;
    aPollStepResult_t execution;
    aTimeout_t wait; /**< 本次处理后的建议等待；事件可提前唤醒。 */
} aPollProcessResult_t;

/** @brief 填写空配置，无作业、事件或临时队列。
 * @param[out] config 配置输出，NULL 不操作；不申请资源。
 */
void aPollConfigStructInit(aPollConfig_t *config);
/** @brief 填写作业默认值：一步、错误等待 5 ms，需补齐 execute。
 * @param[out] config 配置输出，NULL 不操作；不申请资源。
 */
void aPollJobConfigStructInit(aPollJobConfig_t *config);
/** @brief 填写空事件配置，需补齐 function。
 * @param[out] config 配置输出，NULL 不操作；不申请资源。
 */
void aPollEventConfigStructInit(aPollEventConfig_t *config);
/** @brief 填写空请求，需补齐 execute。
 * @param[out] request 请求输出，NULL 不操作；不申请资源。
 */
void aPollRequestStructInit(aPollRequest_t *request);

/* 配置结构体复制，数组及上下文借用至销毁；生命周期由应用单线程编排。
 * 静态实例零初始化，再提供数组及容量；见 aPoll_instance.h。
 * OS 对象仍可能分配。
 * 除 poll_time_ms 的 APOLL_POLL_ONCE 标记外，配置时间不得超过 INT32_MAX。
 * 相邻处理间隔须小于时基完整回绕周期。
 */
#if APOLL_STATIC_ENABLE
/** @brief 初始化应用持有的零初始化实例及预分配数组。
 * @param[in] config 配置；数组保持有效且不变，context 指向对象保持有效。
 * @param[in,out] handle 静态实例，已填存储指针和容量。
 * @return OK；参数/容量错误 INVALID_PARAM；已就绪 BUSY；OS 资源错误。
 */
aStatus_t aPollInitStatic(const aPollConfig_t *config, aPollHandle_t *handle);
/** @brief 释放静态实例的 OS 资源，保留应用提供的数组。
 * @param[in,out] handle 有效静态实例，调用前停止所有使用者。
 * @return OK；在执行/等待或请求未排空 BUSY；未就绪 NOT_READY。
 */
aStatus_t aPollDeInitStatic(aPollHandle_t *handle);
#endif
#if APOLL_DYNAMIC_ENABLE
/** @brief 一次分配实例及数组，再创建 OS 资源。
 * @param[in] config 配置；数组及上下文借用至销毁。
 * @param[out] handle_out 接收实例；失败清空，不得覆盖活动实例。
 * @return OK；INVALID_PARAM；NO_MEMORY 或 OS 对象创建错误。
 */
aStatus_t aPollCreate(const aPollConfig_t *config, aPollHandle_t **handle_out);
/** @brief 释放动态实例和 OS 资源；不释放配置及业务上下文。
 * @param[in,out] handle 动态实例，NULL 返回 OK；先停止所有使用者。
 * @return OK；所有权错误 INVALID_PARAM；在途调用/请求未排空 BUSY。
 */
aStatus_t aPollDestroy(aPollHandle_t *handle);
#endif

/** @brief 最多执行一个回调，三类工作轮转；作业、事件按下标轮转。
 * @param[in,out] handle 已就绪实例，唯一消费者调用。
 * @param[out] result 执行类型、业务结果和剩余等待，不能为 NULL。
 * @return OK；无效参数 INVALID_PARAM；未就绪 NOT_READY；重入 BUSY；队列锁错误。
 * 返回值描述调度器状态，业务结果在 result->execution；回调可再次投递。
 * 单消费者；Process/Wait 重入返回 BUSY。回调运行于调用者任务中。
 */
aStatus_t aPollProcess(aPollHandle_t *handle, aPollProcessResult_t *result);
/** @brief 等待通知或最近作业期限，先通知后等待仍会立即返回。
 * @param[in,out] handle 已就绪实例，由 Process 所在任务调用。
 * @param[in] timeout 最大等待预算，内部同时受最近作业期限约束。
 * @return OK 表示消费通知；TIMEOUT 表示到期；NO_WAIT 无通知为 BUSY。
 * 其余返回值为实例、参数或 OS 等待错误。返回后必须重新 Process。
 */
aStatus_t aPollWait(aPollHandle_t *handle, aTimeout_t timeout);

/** @brief 投递合并事件并唤醒消费者；回调期间可再次置位。
 * @param[in,out] handle 已就绪实例，支持多个任务投递。
 * @param[in] event_index events 数组下标。
 * @return OK；下标越界 NOT_FOUND；实例为空 INVALID_PARAM；未就绪 NOT_READY。
 * 通知不提供业务数据快照，context 数据的并发访问由应用同步。
 */
aStatus_t aPollEventPost(aPollHandle_t *handle, size_t event_index);
/** @brief 提前开始一轮；正在执行的轮次不重置，额外触发合并为下一轮。
 * @param[in,out] handle 已就绪实例，支持多个任务调用。
 * @param[in] job_index jobs 数组下标。
 * @return OK；下标越界 NOT_FOUND；实例为空 INVALID_PARAM；未就绪 NOT_READY。
 * 仍遵守成功/失败后的最小等待，不改变物理协议时序。
 * APOLL_POLL_ONCE 作业完成后不重新启动，调用仍返回 OK。
 */
aStatus_t aPollJobTrigger(aPollHandle_t *handle, size_t job_index);
/** @brief 复制描述入队，借用 context 及数据至完成回调结束；不自动重试。
 * @param[in,out] handle 已就绪实例，支持多个任务投递。
 * @param[in] request 有效描述，execute 必填，complete 可为 NULL。
 * @return OK 表示已接收；其他返回值均表示未接收。
 * 队列满或锁竞争返回 BUSY，无队列返回 UNSUPPORTED。
 * 入队后解锁异常由 aOS 记录，仍返回 OK，禁止重投。
 * OK 不表示执行完成；完成回调可能在本函数返回前运行。
 * 销毁前停止所有调用者并排空请求。
 */
aStatus_t aPollSubmit(aPollHandle_t *handle, const aPollRequest_t *request);

#endif
