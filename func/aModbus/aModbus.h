/**
 * @file aModbus.h
 * @brief 基于 nanoMODBUS 的主从站；业务数据统一接入 aBus。
 * 仅任务上下文，不创建任务。同一实例的并发/重入调用返回 BUSY。
 * 生命周期由应用串行编排；销毁前停止调用者，传输与 aBus 均为借用。
 */
#ifndef A_MODBUS_H
#define A_MODBUS_H

#include "aBus.h"
#include "aOS.h"
#include "aStream.h"

#ifndef AMODBUS_STATIC_ENABLE
#define AMODBUS_STATIC_ENABLE 1
#endif
#ifndef AMODBUS_DYNAMIC_ENABLE
#define AMODBUS_DYNAMIC_ENABLE 1
#endif
#ifndef AMODBUS_CLIENT_ENABLE
#define AMODBUS_CLIENT_ENABLE 1
#endif
#ifndef AMODBUS_SERVER_ENABLE
#define AMODBUS_SERVER_ENABLE 1
#endif
#if (AMODBUS_STATIC_ENABLE < 0 || AMODBUS_STATIC_ENABLE > 1) || \
    (AMODBUS_DYNAMIC_ENABLE < 0 || AMODBUS_DYNAMIC_ENABLE > 1) || \
    (AMODBUS_CLIENT_ENABLE < 0 || AMODBUS_CLIENT_ENABLE > 1) || \
    (AMODBUS_SERVER_ENABLE < 0 || AMODBUS_SERVER_ENABLE > 1)
#error "aModbus switches must be 0 or 1"
#endif
#if !AMODBUS_STATIC_ENABLE && !AMODBUS_DYNAMIC_ENABLE
#error "aModbus requires an allocation API"
#endif
#if !AMODBUS_CLIENT_ENABLE && !AMODBUS_SERVER_ENABLE
#error "aModbus requires a role"
#endif

#define AMODBUS_ACCESS_READ (1U << 0)
#define AMODBUS_ACCESS_WRITE (1U << 1)
/** paramIndex 的特殊值，表示整个标量、RAW 或符合连续布局的 STRUCT SIG。 */
#define AMODBUS_SIG_WHOLE SIZE_MAX
#define AMODBUS_MAX_REGISTERS 125U
#define AMODBUS_MAX_BITS 2000U
#define AMODBUS_MAX_VALUE_SIZE 250U

typedef struct aModbusHandle aModbusHandle_t;

typedef enum {
    AMODBUS_ROLE_CLIENT,
    AMODBUS_ROLE_SERVER
} aModbusRole_t;

typedef enum {
    AMODBUS_TRANSPORT_RTU,
    AMODBUS_TRANSPORT_TCP
} aModbusTransportType_t;

typedef enum {
    AMODBUS_AREA_COILS,
    AMODBUS_AREA_DISCRETE_INPUTS,
    AMODBUS_AREA_HOLDING_REGISTERS,
    AMODBUS_AREA_INPUT_REGISTERS
} aModbusArea_t;

typedef enum {
    AMODBUS_WORD_HIGH_FIRST,
    AMODBUS_WORD_LOW_FIRST
} aModbusWordOrder_t;

/** 协议借用的传输，不创建或关闭端口。
 * stream 负责同步字节收发，允许部分进度；回调返回后不得持有缓冲区。
 * TCP 端口须将 EOF 转成 -1/A_EIO，0 仅表示本次没有数据。
 * flush 可为 NULL；非空时在完整报文提交后调用，再等待 RTU 线路完成。
 * 带 flush 的流写入/提交失败后，后续操作返回 NOT_READY，须清理端口
 * 输出缓冲并重建实例；库无法撤回端口已接收的部分报文。
 * RTU 还必须提供输入恢复、帧间隔和发送完成操作；context 只供这些
 * 辅助操作及 finish 使用，不改变 aStream 的无 context 约定。
 * TCP 的辅助操作通常为空；不得随意清空后续合法 TCP 报文。
 */
typedef struct {
    aStream_t stream;
    void *context;
    aStatus_t (*discard_input)(void *context, aTimeout_t timeout);
    aStatus_t (*prepare_frame)(void *context, aTimeout_t timeout);
    aStatus_t (*wait_transmit_complete)(void *context, aTimeout_t timeout);
    /** 每笔操作结束释放当前帧，不丢弃后续帧；任务上下文，禁止重入。 */
    void (*finish)(void *context);
} aModbusTransport_t;

/** 寄存器区 data 为 uint16_t 数组；位区为低位在前的紧凑位数组。
 * size 为缓冲区字节容量，quantity 为寄存器/位数量，地址均从零开始。
 */
typedef struct {
    aModbusArea_t area;
    uint16_t address;
    uint16_t quantity;
    void *data;
    size_t size;
    aTimeout_t timeout;
} aModbusAddressReadRequest_t;

typedef struct {
    aModbusArea_t area;
    uint16_t address;
    uint16_t quantity;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aModbusAddressWriteRequest_t;

/** 定位整个 SIG，或 STRUCT 中的一个 Param。 */
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    size_t paramIndex;
} aModbusSigTarget_t;

typedef struct {
    aModbusSigTarget_t target;
    void *data;
    size_t size; /**< 必须等于 aBus 中目标的完整长度。 */
    aTimeout_t timeout;
} aModbusSigReadRequest_t;

typedef struct {
    aModbusSigTarget_t target;
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aModbusSigWriteRequest_t;

/** 回调在调用者任务中同步执行；数据只在调用期间借用。
 * OK 必须完成整项请求；禁止重入当前 Modbus 实例。
 * 地址回调接收协议数值，SIG 回调接收本地类型字节；不得绕过业务权限/范围。
 * SIG 回调为 NULL 时默认访问配置中的 aBus。
 * 整组 STRUCT 读取/完整写入回调一次，target 为 WHOLE，size 为完整结构体。
 * 部分 STRUCT 写入先校验所有输入，再逐个完整字段回调，target 为字段下标；
 * 不先读后写整组。回调提供者负责同步，字段间失败不回滚。
 */
typedef aStatus_t (*aModbusAddressReadFn_t)(void *context,
    const aModbusAddressReadRequest_t *request);
typedef aStatus_t (*aModbusAddressWriteFn_t)(void *context,
    const aModbusAddressWriteRequest_t *request);
typedef aStatus_t (*aModbusSigReadFn_t)(void *context,
    const aModbusSigReadRequest_t *request);
typedef aStatus_t (*aModbusSigWriteFn_t)(void *context,
    const aModbusSigWriteRequest_t *request);

/** 离散映射的只读定义；quantity 显式配置，类型和范围来自 aBus。
 * quantity 必须非零，并与目标类型的寄存器数或位数匹配。
 * 初始化拒绝长度不匹配，不自动补全、截断或扩展目标值。
 * 映射表及 aBus 目标定义在实例使用期间必须保持不变。
 * 按 area、address 排序，同一区域的完整映射范围不得相互覆盖，
 * 也不得与地址段回调重叠；允许地址空洞，不需要外围地址段。
 * U8/U16 占一寄存器，U32/S32 占两寄存器；位映射仅支持值为 0/1 的 U8。
 * RAW 每寄存器先放前一字节，奇数长度最后一个低字节补零。
 * STRUCT 可通过 paramIndex 映射字段，或用 WHOLE 严格整组映射：
 * params 按 offset 连续覆盖全部 size，每项非零且为偶数字节，不支持嵌套；
 * 字段仅支持 U16/U32/S32/偶数 RAW。quantity == size / 2，地址偏移为
 * param.offset / 2；不传输隐式填充。整组 word_order 仅作用于 U32/S32。
 * 整组仅用于寄存器区；单个整组最大 250 字节，可写整组最大 246 字节。
 * 允许任意寄存器区间读取；整组部分写入须覆盖一个或多个完整字段。
 * 默认路径使用同一 SIG 的快照/更新，锁遵循 aBus 配置；跨 SIG 不回滚。
 */
typedef struct {
    aModbusArea_t area;
    uint16_t address;
    uint16_t quantity; /**< 寄存器数或位数，不是字节数。 */
    uint16_t flags;
    aModbusSigTarget_t target;
    aModbusWordOrder_t word_order;
} aModbusBusMap_t;

/** 按 area、address 排序的回调地址段；同一区域不得重叠。
 * 必须提供 flags 所允许的读写函数，回调负责段内数据的解释与校验。
 * 命中后权限错误或回调失败直接返回，不再尝试离散映射。
 */
typedef struct {
    aModbusArea_t area;
    uint16_t address;
    uint32_t quantity; /**< 1..65536，address + quantity 不超过 65536。 */
    uint16_t flags;
    aModbusAddressReadFn_t read;
    aModbusAddressWriteFn_t write;
    void *context;
} aModbusAddressRange_t;

/** 协议实例配置，不包含应用任务和轮询调度参数。
 * StructInit 设置默认值；直接静态初始化不会自动应用这些默认值。
 * 应用可先定义 const 模板，再复制到局部变量并装配 bus 和 transport。
 */
typedef struct {
    aModbusRole_t role;
    aModbusTransportType_t transport_type;
    uint8_t unit_id; /**< RTU 从站为 1..247；主站目标在每次请求中设置。 */
    aModbusTransport_t transport;
    aBusHandle_t *bus; /**< 必填，已初始化；协议模块不销毁。 */
    aTimeout_t byte_timeout; /**< 每次传输的字节等待限制，默认 20 ms。 */
    const aModbusAddressRange_t *ranges; /**< 可选从站回调段；借用至销毁。 */
    size_t range_count;
    /** 回调段未命中时查找的离散映射；与 ranges 平级，借用至销毁。
     * 两类配置分别排序，初始化拒绝重叠。批量访问先检查完整覆盖和权限，
     * 再执行回调或 SIG 读写；跨回调/SIG 不提供整体回滚。
     */
    const aModbusBusMap_t *maps;
    size_t map_count;
    aModbusSigReadFn_t sig_read;
    aModbusSigWriteFn_t sig_write;
    void *sig_context;
    /** 可选整组快照工作区；NULL 使用实例内 250 字节工作区。
     * 映射的 SIG 大于 250 字节时必填，容量须覆盖最大 SIG。
     * 由实例独占，生命周期覆盖实例；不得与总线数据区重叠。
     * 内部以字节访问，不要求业务数据对齐。
     */
    void *snapshot;
    size_t snapshot_size;
} aModbusConfig_t;

/** 协议异常独立保存；从站成功发送异常响应时 Process 仍返回 OK。
 * 当前官方源码仅接受 1..4 异常；其他远端异常作为无效响应处理。
 */
typedef struct {
    uint8_t exception;
} aModbusResult_t;

typedef struct {
    aTimeout_t timeout; /**< 本次 Process 总预算，包括接收、aBus 和发送。 */
    aModbusResult_t *result; /**< 可为 NULL；无请求时成功且 exception 为零。 */
} aModbusServerProcessRequest_t;

typedef struct {
    uint8_t unit_id;
    aModbusAddressReadRequest_t access;
    aModbusResult_t *result;
} aModbusClientReadRequest_t;

typedef struct {
    uint8_t unit_id;
    aModbusAddressWriteRequest_t access;
    aModbusResult_t *result;
} aModbusClientWriteRequest_t;

/** 主站直接在远端地址与本地 SIG/Param 之间传输。
 * 读取：远端 → 解码 → aBus；写入：aBus → 编码 → 远端。
 * 长度由目标元信息决定；角色/站号/区域与远端设备定义由应用保证。
 */
typedef struct {
    uint8_t unit_id;
    aModbusArea_t area;
    uint16_t address;
    aModbusSigTarget_t target;
    aModbusWordOrder_t word_order;
    aTimeout_t timeout;
    aModbusResult_t *result;
} aModbusClientSigRequest_t;

/** 从此项开始新帧；位于当前批次首项时无需额外处理。 */
#define AMODBUS_POLL_FLAG_BREAK_BEFORE (1U << 0)

/** 单个采集目标；站号、事务超时和采集周期由设备统一配置。
 * flags 为零时自动合并；BREAK_BEFORE 可手动拆开连续地址。
 * STRUCT 可选择字段或 WHOLE 整组；标量/RAW 使用 AMODBUS_SIG_WHOLE。
 * 整组布局要求与从站映射相同；单个采集项不会被拆成多帧。
 * quantity 必须显式填写且非零，不根据 aBus 自动补全。
 * 寄存器数须等于目标字节数向上除以 2；位区域的 U8 目标须为 1 位。
 * 库检查远端长度与 aBus 目标匹配，不提供隐式截断或类型扩展。
 */
typedef struct {
    aModbusArea_t area;
    uint16_t address;
    uint16_t quantity; /**< 寄存器数或位数，不是字节数。 */
    uint16_t flags;
    aModbusSigTarget_t target;
    aModbusWordOrder_t word_order;
} aModbusPoll_t;

/** 同一设备从首项起合并同区域、地址连续且未要求断帧的采集项。
 * unit_id、timeout 和 result 属于整帧，各项不单独配置。
 * completed 返回本次尝试的项数（通信失败也推进）；BUSY 重入为零。
 * 到协议帧容量、不连续项或断帧标记时停止合并，每次调用只发送一帧。
 * 每帧中同一 SIG 一次提交，手动跨帧后不保证各字段来自同次读取。
 * 一致性遵循 aBus 锁配置；自定义 SIG 回调仍逐项调用。
 */
typedef struct {
    const aModbusPoll_t *items;
    size_t count;
    size_t *completed;
    uint8_t unit_id;
    aTimeout_t timeout;
    aModbusResult_t *result;
} aModbusClientBatchRequest_t;

/** 采集计划引用原始清单，预计算分帧；RTU 固定读取报文含 CRC。
 * TCP 同样复用分组，但事务编号和 MBAP 仍在执行时生成。
 * 计划绑定一个协议实例；清单和实例必须有效至计划销毁。
 */
typedef struct aModbusPlan aModbusPlan_t;
typedef struct {
    const aModbusPoll_t *items;
    size_t count;
    uint8_t unit_id;
    aTimeout_t timeout; /**< 设备统一的每帧预算，包含发布等待。 */
} aModbusPlanConfig_t;

typedef struct {
    size_t frame_index;
    aBool_t *attempted; /**< 执行前清零；取得实例并尝试事务后置位。 */
    aModbusResult_t *result;
} aModbusPlanRequest_t;

void aModbusPlanConfigStructInit(aModbusPlanConfig_t *config);
void aModbusPlanRequestStructInit(aModbusPlanRequest_t *request);
#if AMODBUS_CLIENT_ENABLE
#if AMODBUS_STATIC_ENABLE
/* 静态计划预先提供 frames/capacity，见 aModbus_instance.h。 */
aStatus_t aModbusPlanInitStatic(aModbusHandle_t *handle,
    const aModbusPlanConfig_t *config, aModbusPlan_t *plan);
aStatus_t aModbusPlanDeInitStatic(aModbusPlan_t *plan);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusPlanCreate(aModbusHandle_t *handle,
    const aModbusPlanConfig_t *config, aModbusPlan_t **plan_out);
aStatus_t aModbusPlanDestroy(aModbusPlan_t *plan);
#endif
size_t aModbusPlanFrameCount(const aModbusPlan_t *plan);
aStatus_t aModbusPlanExecute(aModbusPlan_t *plan,
    const aModbusPlanRequest_t *request);
#endif

/** 协议服务配置；仅组合协议及请求参数，调度策略由应用另行提供。
 * 不创建任务或端口，所有引用须保持有效至实例销毁。
 */
typedef struct {
    /** 协议配置；使用前由应用装配 bus 和 transport。 */
    aModbusConfig_t modbus;
    /** 主站设备的采集列表；站号使用 modbus.unit_id。 */
    const aModbusPoll_t *polls;
    size_t poll_count;
    /** 设备统一的每帧事务超时，涵盖通信及发布等待；不是采集周期。 */
    aTimeout_t request_timeout;
    /** 从站每次处理请求，传给 aModbusServerProcess。 */
    aModbusServerProcessRequest_t server;
} aModbusServiceConfig_t;

/** 启动前校验全部采集项，不分配资源、不访问端口、不修改数据。
 * config 至少填充 role、transport_type、bus；目标定义须已挂载。
 * 默认路径也检查 snapshot 容量，大 SIG 须提供足够的工作区。
 */
aStatus_t aModbusClientSigCheck(const aModbusConfig_t *config,
    const aModbusClientSigRequest_t *request, aBool_t write);
/** 检查采集项、长度匹配及站号，不进行 I/O；超时由调用方另外校验。 */
aStatus_t aModbusClientPollCheck(const aModbusConfig_t *config,
    const aModbusPoll_t *poll, uint8_t unit_id);

void aModbusTransportStructInit(aModbusTransport_t *transport);
void aModbusConfigStructInit(aModbusConfig_t *config);
/** 重置协议及请求默认值；事务超时默认 1000 ms，不分配资源。 */
void aModbusServiceConfigStructInit(aModbusServiceConfig_t *config);
void aModbusSigTargetStructInit(aModbusSigTarget_t *target);
void aModbusAddressReadRequestStructInit(aModbusAddressReadRequest_t *request);
void aModbusAddressWriteRequestStructInit(
    aModbusAddressWriteRequest_t *request);
void aModbusSigReadRequestStructInit(aModbusSigReadRequest_t *request);
void aModbusSigWriteRequestStructInit(aModbusSigWriteRequest_t *request);
/** quantity 初始化为零，调用方必须按远端协议显式填写。 */
void aModbusBusMapStructInit(aModbusBusMap_t *map);
void aModbusAddressRangeStructInit(aModbusAddressRange_t *range);
void aModbusServerProcessRequestStructInit(
    aModbusServerProcessRequest_t *request);
void aModbusClientReadRequestStructInit(aModbusClientReadRequest_t *request);
void aModbusClientWriteRequestStructInit(aModbusClientWriteRequest_t *request);
void aModbusClientSigRequestStructInit(aModbusClientSigRequest_t *request);

#if AMODBUS_STATIC_ENABLE
/** handle 存储由应用提供，布局见 aModbus_instance.h；只初始化未运行实例。 */
aStatus_t aModbusInitStatic(const aModbusConfig_t *config,
                          aModbusHandle_t *handle);
aStatus_t aModbusDeInitStatic(aModbusHandle_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
/** 只分配实例；运行期无内存申请，失败清空输出。 */
aStatus_t aModbusCreate(const aModbusConfig_t *config,
                      aModbusHandle_t **handle_out);
aStatus_t aModbusDestroy(aModbusHandle_t *handle);
#endif

/** 仅支持基础功能码 01/02/03/04/05/06/0F/10。
 * 跨段请求先检查全部地址与写入值；跨 SIG 写入不提供事务或失败回滚。
 * 32 位/RAW 写入必须完整覆盖映射，禁止半值写入；允许读取部分寄存器。
 * TCP 在半帧/传输/协议错误后实例返回 NOT_READY，应用须重建连接和实例。
 * RTU 错误后通过 discard_input 恢复，主站广播只允许写入且不等待响应。
 */
#if AMODBUS_SERVER_ENABLE
aStatus_t aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request);
#endif
#if AMODBUS_CLIENT_ENABLE
aStatus_t aModbusClientReadBatch(aModbusHandle_t *handle,
    const aModbusClientBatchRequest_t *request);
aStatus_t aModbusClientRead(aModbusHandle_t *handle,
    const aModbusClientReadRequest_t *request);
aStatus_t aModbusClientWrite(aModbusHandle_t *handle,
    const aModbusClientWriteRequest_t *request);
aStatus_t aModbusClientReadSig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request);
aStatus_t aModbusClientWriteSig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request);
#endif

#endif
