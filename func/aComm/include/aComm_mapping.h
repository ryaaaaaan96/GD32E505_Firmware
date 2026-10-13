/** @brief 数据端点与线性地址端点的映射、采集计划及事件请求。 */
#ifndef ACOMM_MAPPING_H
#define ACOMM_MAPPING_H
#include "aComm.h"
#include "aComm_endpoint.h"
typedef enum { ACOMM_MAPPING_CLIENT, ACOMM_MAPPING_SERVER }
    aCommMappingRole_t;
#define ACOMM_MAP_ACCESS_READ (1U << 0)
#define ACOMM_MAP_ACCESS_WRITE (1U << 1)
#define ACOMM_MAP_SIG_WHOLE ACOMM_DATA_WHOLE
typedef struct aCommMapping aCommMapping_t;
/** 定位整个 SIG，或 STRUCT 中的一个 Param。 */
typedef aCommTarget_t aCommMappingSigTarget_t;
typedef struct {
    aCommMappingSigTarget_t target;
    void *data;
    size_t size; /**< 必须等于数据端点中目标的完整长度。 */
    aTimeout_t timeout;
} aCommMappingSigReadRequest_t;
typedef struct {
    aCommMappingSigTarget_t target;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aCommMappingSigWriteRequest_t;
/** 回调在调用者任务中同步执行；数据只在调用期间借用。
 * OK 必须完成整项请求；禁止重入当前映射或协议实例。
 * 地址回调接收协议数值，SIG 回调接收本地类型字节；不得绕过业务权限/范围。
 * SIG 回调为 NULL 时默认访问配置中的 数据端点。
 * 整组 STRUCT 读取/完整写入回调一次，target 为 WHOLE，size 为完整结构体。
 * 部分 STRUCT 写入先校验所有输入，再逐个完整字段回调，target 为字段下标；
 * 不先读后写整组。回调提供者负责同步，字段间失败不回滚。
 */
typedef aStatus_t (*aCommMappingSigReadFn_t)(void *context,
    const aCommMappingSigReadRequest_t *request);
typedef aStatus_t (*aCommMappingSigWriteFn_t)(void *context,
    const aCommMappingSigWriteRequest_t *request);
/** 应用持有两个端点；映射仅借用，销毁时不释放端点。
 * 主从都需要协议端点的地址规则；仅主站要求已就绪的事务能力。
 * snapshot 为 NULL 时使用 ACOMM_VALUE_CAPACITY 字节的内部对齐快照；
 * 大 SIG 可提供更大且满足业务类型对齐的独占工作区，不得与数据源重叠。
 */
typedef struct {
    aCommEndpoint_t *data;
    aCommEndpoint_t *protocol;
    aCommMappingSigReadFn_t sig_read;
    aCommMappingSigWriteFn_t sig_write;
    void *sig_context;
    void *snapshot;
    size_t snapshot_size;
} aCommMappingConfig_t;
void aCommMappingConfigStructInit(aCommMappingConfig_t *config);
void aCommMappingSigTargetStructInit(aCommMappingSigTarget_t *target);
void aCommMappingSigReadRequestStructInit(
    aCommMappingSigReadRequest_t *request);
void aCommMappingSigWriteRequestStructInit(
    aCommMappingSigWriteRequest_t *request);
#if ACOMM_STATIC_ENABLE
aStatus_t aCommMappingDeInitStatic(aCommMapping_t *handle);
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommMappingDestroy(aCommMapping_t *handle);
#endif
/** 主站直接在远端地址与本地 SIG/Param 之间传输。
 * 读取：远端 → 解码 → 数据端点；写入：数据端点 → 编码 → 远端。
 * 长度由目标元信息决定；角色/站号/区域与远端设备定义由应用保证。
 */
typedef struct {
    uint8_t unit_id;
    uint16_t area;
    uint16_t address;
    aCommMappingSigTarget_t target;
    uint8_t word_order; /**< 端点定义的编码选项，通用层原样传递。 */
    aTimeout_t timeout;
    aCommEndpointResult_t *result;
} aCommMappingClientSigRequest_t;
/** 采集计划从此项开始新帧。 */
#define ACOMM_MAP_FLAG_BREAK_BEFORE (1U << 0)
/** 主站通信表的一条只读映射，供采集计划及事件请求共用。
 * 数组构成完整通信表；站号、事务超时由设备统一配置。
 * poll_time_ms 仅声明自动采集策略，计时和执行进度由调度器维护；
 * 配为零的条目仍可用于临时读写和事件请求。
 * flags 为零时自动合并；BREAK_BEFORE 可手动拆开连续地址。
 * STRUCT 可选择字段或 WHOLE 整组；标量/RAW 使用 ACOMM_MAP_SIG_WHOLE。
 * 整组布局要求与从站映射相同；单个采集项不会被拆成多帧。
 * quantity 必须显式填写且非零，不根据数据端点自动补全。
 * quantity 的单位和类型转换由协议端点定义，通用层不规定寄存器宽度。
 * 库检查远端长度与数据端点目标匹配，不提供隐式截断或类型扩展。
 */
typedef struct {
    uint16_t area;
    uint16_t address;
    uint16_t quantity; /**< 地址单位数，单位宽度由端点定义。 */
    uint16_t flags;
    aCommTargetConfig_t target;
    uint8_t word_order; /**< 端点定义的编码选项，通用层原样传递。 */
    uint32_t poll_time_ms; /**< 0 不轮询；ONCE 有限执行；其余为周期。 */
    uint16_t access; /**< READ/WRITE 权限，与断帧 flags 分开。 */
} aCommMappingClientMap_t;
/** 采集计划引用原始清单，调用端点规则合并相邻请求。
 * 预编码能力可选；未提供时执行普通读取，编码始终由端点负责。
 * 按逐项周期稳定分组，只保存启用项下标；0 周期不进入计划。
 * 计划绑定一个通信适配实例；清单和实例必须有效至计划销毁。
 * 每个计划只由一个消费者推进；销毁前先停止引用计划的作业。
 */
typedef struct aCommPlan aCommPlan_t;
typedef struct {
    const aCommMappingClientMap_t *items;
    size_t count;
    uint8_t unit_id;
    aTimeout_t timeout; /**< 设备统一的每帧预算，包含发布等待。 */
} aCommPlanConfig_t;
typedef struct {
    size_t frame_index;
    /** 可为 NULL；返回执行结果、推进及可重试建议，不维护重试次数。 */
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
#if ACOMM_STATIC_ENABLE
/* 静态计划提供 indices/index_capacity、frames/capacity，见实例布局头。 */
aStatus_t aCommPlanInitStatic(aCommMapping_t *handle,
    const aCommPlanConfig_t *config, aCommPlan_t *plan);
aStatus_t aCommPlanDeInitStatic(aCommPlan_t *plan);
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommPlanCreate(aCommMapping_t *handle,
    const aCommPlanConfig_t *config, aCommPlan_t **plan_out);
aStatus_t aCommPlanDestroy(aCommPlan_t *plan);
#endif
size_t aCommPlanFrameCount(const aCommPlan_t *plan);
size_t aCommPlanGroupCount(const aCommPlan_t *plan);
aStatus_t aCommPlanGroupGet(
    const aCommPlan_t *plan, size_t index,
    aCommPlanGroup_t *group);
/** 执行一帧；重试次数和等待由通用调度器管理，结果经 request.execution 返回。 */
aStatus_t aCommPlanExecute(aCommPlan_t *plan,
    const aCommPlanRequest_t *request);
/** 主站默认设备通信表。可以只用即时接口：maps=NULL、map_count=0。
 * 通信表也用于可选的总线请求绑定，不要求创建采集计划。
 * 其他站号可通过即时请求或独立采集计划访问，同一线路共用本实例。
 * 生命周期内表不可变；调度器、端口和任务由应用创建及销毁。
 */
typedef struct {
    aCommMappingConfig_t binding;
    const aCommMappingClientMap_t *maps;
    size_t map_count;
    uint8_t unit_id;
    aTimeout_t request_timeout;
    aCommSubmitFn_t submit;
    void *submit_context;
} aCommMappingClientConfig_t;
void aCommMappingClientConfigStructInit(aCommMappingClientConfig_t *config);
void aCommMappingClientSigRequestStructInit(
    aCommMappingClientSigRequest_t *request);
aStatus_t aCommMappingClientConfigCheck(
    const aCommMappingClientConfig_t *config);
#if ACOMM_STATIC_ENABLE
aStatus_t aCommMappingClientInitStatic(
    const aCommMappingClientConfig_t *config,
    aCommMapping_t *handle);
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommMappingClientCreate(const aCommMappingClientConfig_t *config,
    aCommMapping_t **handle_out);
#endif
/** 启动前校验全部采集项，不分配资源、不访问端口、不修改数据。
 * config 填充 data/protocol 端点；检查地址与布局，目标定义须已挂载。
 * 默认路径也检查 snapshot 容量，大 SIG 须提供足够的工作区。
 */
aStatus_t aCommMappingClientSigCheck(const aCommMappingConfig_t *config,
    const aCommMappingClientSigRequest_t *request, aBool_t write);
/** 检查主站映射、权限、周期声明、长度及站号，不进行 I/O。
 * 包括不自动采集的条目；超时由调用方另外校验。
 */
aStatus_t aCommMappingClientMapCheck(const aCommMappingConfig_t *config,
    const aCommMappingClientMap_t *map, uint8_t unit_id);
aStatus_t aCommMappingClientReadSig(aCommMapping_t *handle,
    const aCommMappingClientSigRequest_t *request);
aStatus_t aCommMappingClientWriteSig(aCommMapping_t *handle,
    const aCommMappingClientSigRequest_t *request);
/** 从站通信表的一条只读映射；quantity 显式配置，类型和范围来自 数据端点。
 * quantity 必须非零，并与端点提供的目标协议长度匹配。
 * 初始化拒绝长度不匹配，不自动补全、截断或扩展目标值。
 * 映射表及数据端点目标定义在实例使用期间必须保持不变。
 * subscribe_events 支持 MARK_ONLY、UPDATED、WRITE_COMPLETE。
 * 不接收 REQUEST_READ/WRITE；publish_events 为零。
 * 接收主站写入时由数据端点自动发布 UPDATED，不重复发布。
 * 按 area、address 排序，同一区域的完整映射范围不得相互覆盖，
 * 也不得与地址段回调重叠；允许地址空洞，不需要外围地址段。
 * 类型编码及 STRUCT 布局由协议端点检查；通用层按地址单位定位区间。
 * 部分整组写入必须覆盖完整字段，禁止读旧值拼接半个字段。
 * 默认按同一 SIG 取快照或更新；同步由数据端点负责，跨 SIG 不回滚。
 */
typedef struct {
    uint16_t area;
    uint16_t address;
    uint16_t quantity; /**< 地址单位数，单位宽度由端点定义。 */
    uint16_t flags;
    aCommTargetConfig_t target;
    uint8_t word_order; /**< 端点定义的编码选项，通用层原样传递。 */
} aCommMappingServerMap_t;
/** 按 area、address 排序的回调地址段；同一区域不得重叠。
 * 必须提供 flags 所允许的读写函数，回调负责段内数据的解释与校验。
 * 命中后权限错误或回调失败直接返回，不再尝试离散映射。
 */
typedef struct {
    uint16_t area;
    uint16_t address;
    uint32_t quantity; /**< 1..65536，address + quantity 不超过 65536。 */
    uint16_t flags;
    aCommAddressReadFn_t read;
    aCommAddressWriteFn_t write;
    void *context;
} aCommMappingAddressRange_t;
/** 非阻塞观察回调，仅设置标志或尝试投递工作，禁止 I/O、等待和重入。
 * mapping/input 只在本次回调中借用；事件不表示主站已经读取数据。
 */
typedef void (*aCommMappingServerNotifyFn_t)(void *context,
    const aCommMappingServerMap_t *mapping, const aCommNotifyInput_t *input);
typedef struct {
    aCommMappingConfig_t binding;
    const aCommMappingServerMap_t *maps;
    size_t map_count;
    const aCommMappingAddressRange_t *ranges;
    size_t range_count;
    aCommMappingServerNotifyFn_t notify;
    void *notify_context;
} aCommMappingServerConfig_t;
void aCommMappingServerConfigStructInit(aCommMappingServerConfig_t *config);
void aCommMappingServerMapStructInit(aCommMappingServerMap_t *map);
void aCommMappingAddressRangeStructInit(aCommMappingAddressRange_t *range);
/** 校验配置及映射，不进行 I/O 或分配资源。 */
aStatus_t aCommMappingServerConfigCheck(
    const aCommMappingServerConfig_t *config);
#if ACOMM_STATIC_ENABLE
aStatus_t aCommMappingServerInitStatic(
    const aCommMappingServerConfig_t *config,
    aCommMapping_t *handle);
#endif
#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommMappingServerCreate(const aCommMappingServerConfig_t *config,
    aCommMapping_t **handle_out);
#endif
/** 注册给协议端点的地址 read/write，context 为本映射实例。
 * 同步访问映射的数据，无队列、无轮询计划；单消费者，重入返回 BUSY。
 */
aStatus_t aCommMappingServerRead(void *context,
    const aCommAddressReadRequest_t *request);
aStatus_t aCommMappingServerWrite(void *context,
    const aCommAddressWriteRequest_t *request);
#endif
