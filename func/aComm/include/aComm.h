/**
 * @file aComm.h
 * @brief 协议注册、通信表、调度及数据绑定；任务由应用创建。
 * 仅任务上下文，多生产者、单消费者；回调在 Process 调用者中执行。
 * 配置数组及 context 借用至销毁，生命周期操作由应用串行编排。
 */
#ifndef A_COMM_H
#define A_COMM_H

#include "aOS.h"
#include "aComm_endpoint.h"

#ifndef ACOMM_STATIC_ENABLE
#define ACOMM_STATIC_ENABLE 1
#endif
#ifndef ACOMM_DYNAMIC_ENABLE
#define ACOMM_DYNAMIC_ENABLE 1
#endif

#if (ACOMM_STATIC_ENABLE != 0 && ACOMM_STATIC_ENABLE != 1) || \
    (ACOMM_DYNAMIC_ENABLE != 0 && ACOMM_DYNAMIC_ENABLE != 1)
#error "aComm allocation switches must be 0 or 1"
#endif
#if !ACOMM_STATIC_ENABLE && !ACOMM_DYNAMIC_ENABLE
#error "aComm requires an allocation API"
#endif

/* -------------------- 通信表与计划 -------------------- */

#define ACOMM_POLL_ONCE UINT32_MAX
#define ACOMM_MAP_FLAG_READ (1U << 0)
#define ACOMM_MAP_FLAG_WRITE (1U << 1)
#define ACOMM_MAP_SIG_WHOLE ACOMM_DATA_WHOLE
#define ACOMM_MAP_FLAG_BREAK_BEFORE (1U << 2)
#define ACOMM_MAP_FLAG_ACCESS_MASK \
    (ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE)
#define ACOMM_MAP_FLAG_MASK \
    (ACOMM_MAP_FLAG_ACCESS_MASK | ACOMM_MAP_FLAG_BREAK_BEFORE)

typedef struct {
    uint32_t poll_time_ms; /**< 0 不采集；ONCE 一轮；其余为整轮最小周期。 */
    uint16_t flags; /**< READ/WRITE 权限及 BREAK_BEFORE 断帧标志。 */
    uint16_t deviceID; /**< 数据端点中的设备标识。 */
    size_t sigIndex; /**< 设备点表中的 SIG 下标。 */
    size_t paramIndex; /**< 字段下标；ACOMM_MAP_SIG_WHOLE 表示整个 SIG。 */
    uint32_t subscribe_events; /**< 订阅事件掩码，零表示不订阅。 */
    uint32_t publish_events; /**< 操作完成时发布的事件掩码，零表示不发布。 */
} aCommEntry_t;

typedef struct {
    aStatus_t status;
    aBool_t advance; /**< false 表示未执行，不消耗重试次数。 */
    aBool_t retryable; /**< 已执行失败且可重试，由调度器限制次数。 */
} aCommStepResult_t;

typedef struct {
    uint8_t data[ACOMM_PREPARED_CAPACITY];
    size_t size; /**< 0 表示没有缓存；字节由协议复制，禁止直接强转。 */
} aCommPrepared_t;

typedef struct {
    const void *entries; /**< 原始具体协议数组。 */
    const size_t *indices; /**< 本次操作的原表下标，不复制表项。 */
    size_t count;
} aCommBatch_t;

typedef struct {
    const aCommEntry_t *(*get)(const void *entries, size_t index);
    aStatus_t (*check)(void *context, const aCommEntry_t *entry);
    aBool_t (*merge)(void *context, const aCommEntry_t *first,
        const aCommEntry_t *previous, const aCommEntry_t *next);
    aStatus_t (*prepare)(void *context, const aCommBatch_t *batch,
        aCommPrepared_t *prepared);
    void (*execute)(void *context, const aCommBatch_t *batch,
        const aCommPrepared_t *prepared, aCommStepResult_t *execution,
        aCommEndpointResult_t *result);
    aStatus_t (*acquire)(void *context);
    aStatus_t (*release)(void *context);
} aCommTableOps_t;

/** 数组、回调及 context 借用至销毁，不允许移动或修改。
 * get/execute 必填；其余可为 NULL；acquire/release 必须成对。
 * get/check/merge 不分配、不做 I/O，同一输入须返回相同结果；
 * 初始化统计与填充会重复调用这些回调。
 * prepare 只生成缓存，不取得资源、不做 I/O；失败时直接丢弃字节。
 * acquire/release 保护协议上下文的借用生命周期；失败不改变借用计数。
 */
typedef struct {
    const void *items;
    size_t count;
    const aCommTableOps_t *ops;
    void *context;
} aCommPlanConfig_t;

typedef struct aCommPlan aCommPlan_t;
typedef struct {
    size_t frame_index;
    aCommStepResult_t *execution;
    aCommEndpointResult_t *result;
} aCommPlanRequest_t;
typedef struct {
    size_t first_frame;
    size_t frame_count;
    uint32_t poll_time_ms;
} aCommPlanGroup_t;

void aCommPlanConfigStructInit(aCommPlanConfig_t *config);
void aCommPlanRequestStructInit(aCommPlanRequest_t *request);
/* 通常由协议 init 回调返回计划配置；以下也支持独立使用计划。 */
#if ACOMM_STATIC_ENABLE
aStatus_t aCommPlanInitStatic(const aCommPlanConfig_t *config,
    aCommPlan_t *plan);
aStatus_t aCommPlanDeInitStatic(aCommPlan_t *plan);
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommPlanCreate(const aCommPlanConfig_t *config,
    aCommPlan_t **plan_out);
aStatus_t aCommPlanDestroy(aCommPlan_t *plan);
#endif
size_t aCommPlanFrameCount(const aCommPlan_t *plan);
size_t aCommPlanGroupCount(const aCommPlan_t *plan);
aStatus_t aCommPlanGroupGet(const aCommPlan_t *plan, size_t index,
    aCommPlanGroup_t *group);
aStatus_t aCommPlanExecute(aCommPlan_t *plan,
    const aCommPlanRequest_t *request);

/* -------------------- 调度与协议注册 -------------------- */

typedef struct aCommHandle aCommHandle_t;

typedef void (*aCommExecuteFn_t)(void *context, size_t step_index,
                               aCommStepResult_t *result);
typedef void (*aCommEventFn_t)(void *context);

typedef struct {
    aCommExecuteFn_t execute;
    void *context;
    size_t step_count;
    uint16_t retry_count; /**< 每步失败后额外尝试的最大次数，默认 0。 */
    /** 整轮起点最小间隔：0 连续执行，ACOMM_POLL_ONCE 仅执行一轮。
     * 普通周期不超过 INT32_MAX；超期不积压补跑。
     */
    uint32_t poll_time_ms;
    uint32_t interval_ms; /**< 本作业成功推进后等待，不阻塞其他作业。 */
    uint32_t error_delay_ms; /**< 失败或未推进后的等待，必须大于零。 */
} aCommJobConfig_t;

typedef struct {
    aCommEventFn_t function;
    void *context;
} aCommEventConfig_t;

typedef struct {
    aStatus_t (*execute)(void *context); /**< 只执行一次，不自动重试写操作。 */
    void (*complete)(void *context, aStatus_t status);
    void *context;
} aCommRequest_t;
/** 非阻塞提交：OK 表示接受，否则未接受；借用 context 至 complete 返回。 */
typedef aStatus_t (*aCommSubmitFn_t)(void *context,
    const aCommRequest_t *request);

typedef struct aCommProtocol aCommProtocol_t;
/** 协议注册回调，不规定主从角色、地址格式或传输介质。
 * check 不分配资源、不访问端口；init 失败须撤销本次创建的资源。
 * init 成功后可返回采集表；空表则由 process 处理一个服务步骤。
 * deinit 在计划解除引用后调用，须支持失败后重试，成功才转移所有权。
 * 初始化/释放由应用串行编排，启动前和释放前必须停止所有生产者。
 */
typedef struct {
    aStatus_t (*check)(const aCommProtocol_t *protocol);
    aStatus_t (*init)(const aCommProtocol_t *protocol,
        aCommPlanConfig_t *table);
    aCommExecuteFn_t process; /**< 无采集表时必填；在 Process 调用者中运行。 */
    aStatus_t (*deinit)(void *context);
} aCommProtocolOps_t;

struct aCommProtocol {
    const void *config; /**< 只读协议配置，仅对应回调解释。 */
    const void *items; /**< 具体协议结构体数组，不按公共结构体步长遍历。 */
    size_t count;
    const aCommProtocolOps_t *ops;
    void *context; /**< 应用持有的协议运行环境，保持有效至释放成功。 */
};

typedef struct {
    aCommProtocol_t protocol; /**< 可选统一注册；与直接配置 table/jobs 互斥。 */
    aCommPlanConfig_t table; /**< 注册具体协议数组；与 jobs 二选一。 */
    uint16_t retry_count;
    uint32_t interval_ms;
    uint32_t error_delay_ms; /**< 表调度失败等待，StructInit 默认 5 ms。 */
    const aCommJobConfig_t *jobs;
    size_t job_count;
    const aCommEventConfig_t *events;
    size_t event_count;
    size_t request_capacity; /**< 为零时不创建队列及队列锁。 */
} aCommConfig_t;

typedef enum {
    ACOMM_WORK_NONE,
    ACOMM_WORK_JOB,
    ACOMM_WORK_EVENT,
    ACOMM_WORK_REQUEST
} aCommWorkType_t;

typedef struct {
    aCommWorkType_t type;
    size_t index; /**< 作业或事件下标；临时请求不提供编号。 */
    size_t step_index;
    aCommStepResult_t execution;
    aTimeout_t wait; /**< 本次处理后的建议等待；事件可提前唤醒。 */
} aCommProcessResult_t;

/** @brief 填写默认配置：无通信表、作业、事件和队列，错误等待 5 ms。
 * @param[out] config 配置输出，NULL 不操作；不申请资源。
 */
void aCommConfigStructInit(aCommConfig_t *config);
/** @brief 填写作业默认值：一步、错误等待 5 ms，需补齐 execute。
 * @param[out] config 配置输出，NULL 不操作；不申请资源。
 */
void aCommJobConfigStructInit(aCommJobConfig_t *config);
/** @brief 填写空事件配置，需补齐 function。
 * @param[out] config 配置输出，NULL 不操作；不申请资源。
 */
void aCommEventConfigStructInit(aCommEventConfig_t *config);
/** @brief 填写空请求，需补齐 execute。
 * @param[out] request 请求输出，NULL 不操作；不申请资源。
 */
void aCommRequestStructInit(aCommRequest_t *request);

/** 只检查配置及协议注册，不分配资源或开启端口。 */
aStatus_t aCommConfigCheck(const aCommConfig_t *config);

/* 配置结构体复制，数组及上下文借用至销毁；生命周期由应用单线程编排。
 * 静态实例零初始化，再提供数组及容量；通信表模式还需计划与作业存储，
 * 见 aComm_instance.h。
 * OS 对象仍可能分配。
 * 除 poll_time_ms 的 ACOMM_POLL_ONCE 标记外，配置时间不得超过 INT32_MAX。
 * 相邻处理间隔须小于时基完整回绕周期。
 */
#if ACOMM_STATIC_ENABLE
/** @brief 初始化应用持有的零初始化实例及预分配数组。
 * @param[in] config 配置；数组保持有效且不变，context 指向对象保持有效。
 * @param[in,out] handle 静态实例，已填存储指针和容量。
 * @return OK；参数/容量错误 INVALID_PARAM；已就绪 BUSY；OS 资源错误。
 */
aStatus_t aCommInitStatic(const aCommConfig_t *config, aCommHandle_t *handle);
/** @brief 释放静态实例的 OS 资源，保留应用提供的数组。
 * @param[in,out] handle 有效静态实例，调用前停止所有使用者。
 * @return OK；在执行/等待或请求未排空 BUSY；未就绪 NOT_READY。
 */
aStatus_t aCommDeInitStatic(aCommHandle_t *handle);
#endif
#if ACOMM_DYNAMIC_ENABLE
/** @brief 一次分配实例及数组，再创建 OS 资源。
 * @param[in] config 配置；数组及上下文借用至销毁。
 * @param[out] handle_out 接收实例；失败清空，不得覆盖活动实例。
 * @return OK；INVALID_PARAM；NO_MEMORY 或 OS 对象创建错误。
 */
aStatus_t aCommCreate(const aCommConfig_t *config, aCommHandle_t **handle_out);
/** @brief 释放动态实例和 OS 资源；不释放配置及业务上下文。
 * @param[in,out] handle 动态实例，NULL 返回 OK；先停止所有使用者。
 * @return OK；所有权错误 INVALID_PARAM；在途调用/请求未排空 BUSY。
 */
aStatus_t aCommDestroy(aCommHandle_t *handle);
#endif

/** @brief 最多执行一个回调，三类工作轮转；作业、事件按下标轮转。
 * @param[in,out] handle 已就绪实例，唯一消费者调用。
 * @param[out] result 执行类型、业务结果和剩余等待，不能为 NULL。
 * @return OK；无效参数 INVALID_PARAM；未就绪 NOT_READY；重入 BUSY；队列锁错误。
 * 返回值描述调度器状态，业务结果在 result->execution；回调可再次投递。
 * 单消费者；Process/Wait 重入返回 BUSY。回调运行于调用者任务中。
 */
aStatus_t aCommProcess(aCommHandle_t *handle, aCommProcessResult_t *result);
/** @brief 等待通知或最近作业期限，先通知后等待仍会立即返回。
 * @param[in,out] handle 已就绪实例，由 Process 所在任务调用。
 * @param[in] timeout 最大等待预算，内部同时受最近作业期限约束。
 * @return OK 表示消费通知；TIMEOUT 表示到期；NO_WAIT 无通知为 BUSY。
 * 其余返回值为实例、参数或 OS 等待错误。返回后必须重新 Process。
 */
aStatus_t aCommWait(aCommHandle_t *handle, aTimeout_t timeout);

/** @brief 投递合并事件并唤醒消费者；回调期间可再次置位。
 * @param[in,out] handle 已就绪实例，支持多个任务投递。
 * @param[in] event_index events 数组下标。
 * @return OK；下标越界 NOT_FOUND；实例为空 INVALID_PARAM；未就绪 NOT_READY。
 * 通知不提供业务数据快照，context 数据的并发访问由应用同步。
 */
aStatus_t aCommEventPost(aCommHandle_t *handle, size_t event_index);
/** @brief 提前开始一轮；正在执行的轮次不重置，额外触发合并为下一轮。
 * @param[in,out] handle 已就绪实例，支持多个任务调用。
 * @param[in] job_index jobs 数组下标。
 * @return OK；下标越界 NOT_FOUND；实例为空 INVALID_PARAM；未就绪 NOT_READY。
 * 仍遵守成功/失败后的最小等待，不改变物理协议时序。
 * ACOMM_POLL_ONCE 作业完成后不重新启动，调用仍返回 OK。
 */
aStatus_t aCommJobTrigger(aCommHandle_t *handle, size_t job_index);
/** @brief 复制描述入队，借用 context 及数据至完成回调结束；不自动重试。
 * @param[in,out] handle 已就绪实例，支持多个任务投递。
 * @param[in] request 有效描述，execute 必填，complete 可为 NULL。
 * @return OK 表示已接收；其他返回值均表示未接收。
 * 队列满或锁竞争返回 BUSY，无队列返回 UNSUPPORTED。
 * 入队后解锁异常由 aOS 记录，仍返回 OK，禁止重投。
 * OK 不表示执行完成；完成回调可能在本函数返回前运行。
 * 销毁前停止所有调用者并排空请求。
 */
aStatus_t aCommSubmit(aCommHandle_t *handle, const aCommRequest_t *request);

/** 提前触发全部作业；已完成的 ONCE 不重新启动。 */
aStatus_t aCommTrigger(aCommHandle_t *handle);

/* -------------------- 数据绑定与事件请求 -------------------- */

typedef struct aCommBinding aCommBinding_t;
typedef struct {
    aCommTarget_t target;
    void *data;
    size_t size;
    aTimeout_t timeout;
} aCommValueReadRequest_t;
typedef struct {
    aCommTarget_t target;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aCommValueWriteRequest_t;
/* 回调同步完成整项操作；缓冲区仅在调用期间借用，不得重入当前绑定。 */
typedef aStatus_t (*aCommValueReadFn_t)(void *,
    const aCommValueReadRequest_t *);
typedef aStatus_t (*aCommValueWriteFn_t)(void *,
    const aCommValueWriteRequest_t *);
typedef struct {
    aCommEndpoint_t *data;
    aCommValueReadFn_t sig_read; /**< NULL 时使用数据端点。 */
    aCommValueWriteFn_t sig_write;
    void *sig_context;
    void *snapshot; /**< NULL 使用内部工作区；外部区须对齐且不与数据重叠。 */
    size_t snapshot_size;
} aCommBindingConfig_t;

/** 请求已通过目标、权限及重叠范围检查；协议只解释自己的表项和地址。
 * target 是此次实际访问目标；整组绑定可能收到某个完整字段的请求。
 * info 为整个 SIG 的元信息；source 为写请求新值，读请求时为 NULL。
 */
typedef struct {
    const aCommEntry_t *entry;
    aCommTarget_t target;
    aCommDataInfo_t info;
    const void *source;
    aBool_t write;
} aCommBindingRequest_t;
typedef struct {
    uint32_t type; /**< 关联查询标签，aComm 不据此选择协议。 */
    void *context;
    aStatus_t (*execute)(void *, const aCommBindingRequest_t *);
    void (*observe)(void *, const aCommEntry_t *, const aCommNotifyInput_t *);
    aCommSubmitFn_t submit;
    void *submit_context;
} aCommBindingEvents_t;

/** 同一个 SIG 的快照读或原子更新；edit 负责协议转换，不负责申请工作区。
 * write=false：读取完整快照后调用 edit；true：由端点在同步区间调用 edit。
 * 此接口走数据端点；自定义 sig_read/sig_write 的同步由提供者负责。
 */
typedef struct {
    aCommDataQuery_t target;
    size_t size;
    aStatus_t (*edit)(void *, void *, size_t);
    void *context;
    aTimeout_t timeout;
    aBool_t write;
} aCommBindingTransfer_t;

void aCommBindingConfigStructInit(aCommBindingConfig_t *config);
void aCommValueReadRequestStructInit(aCommValueReadRequest_t *request);
void aCommValueWriteRequestStructInit(aCommValueWriteRequest_t *request);
aStatus_t aCommBindingConfigCheck(const aCommBindingConfig_t *config);
aStatus_t aCommBindingEntryCheck(const aCommBindingConfig_t *config,
    const aCommBindingEvents_t *events, const aCommEntry_t *entry);
/** 静态嵌入实例：先零初始化并提供订阅槽；配置复制，引用借用至 DeInit。
 * 无独立堆分配；共享工作区要求调用者串行化访问。
 * observe 在通知现场执行，禁止阻塞；execute 在队列消费者中执行。
 */
aStatus_t aCommBindingInit(const aCommBindingConfig_t *config,
    const aCommBindingEvents_t *events, aCommBinding_t *binding);
aStatus_t aCommBindingSubscribe(aCommBinding_t *binding,
    const aCommEntry_t *entry);
/** 停止生产者后调用；未完成请求或端点仍借用节点时返回 BUSY。 */
aStatus_t aCommBindingDeInit(aCommBinding_t *binding);
aStatus_t aCommBindingRead(aCommBinding_t *binding,
    const aCommValueReadRequest_t *request);
aStatus_t aCommBindingWrite(aCommBinding_t *binding,
    const aCommValueWriteRequest_t *request);
aStatus_t aCommBindingTransfer(aCommBinding_t *binding,
    const aCommBindingTransfer_t *request);
/** 为宿主的一次分配计算附加存储；只给订阅项分配槽位及端点原生节点。 */
aStatus_t aCommBindingStorageSize(const aCommBindingConfig_t *config,
    size_t count, size_t *size);
/** storage 必须标准最大对齐，容量至少为 StorageSize 返回值。 */
aStatus_t aCommBindingStorageSetup(aCommBinding_t *binding,
    const aCommBindingConfig_t *config, void *storage, size_t count);
#endif
