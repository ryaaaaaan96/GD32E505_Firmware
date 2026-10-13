/** @brief Modbus 地址与数据目标的映射、协议数组及事件请求。 */
#ifndef AMODBUS_MAPPING_H
#define AMODBUS_MAPPING_H
#include "aComm.h"
#include "aModbus.h"
#include "aComm_endpoint.h"
#include "aModbus_endpoint.h"
typedef enum { AMODBUS_MAPPING_CLIENT, AMODBUS_MAPPING_SERVER }
    aModbusMappingRole_t;
typedef struct aModbusMapping aModbusMapping_t;
/** comm 管理数据访问与快照；protocol 提供 Modbus 编解码和事务。
 * 两端只借用，映射销毁不销毁端点。协议端点嵌入 aModbus 实例。
 * 自定义 SIG 回调为同步操作；完整 STRUCT 调用一次，部分写按完整字段调用。
 * 自定义回调负责同步与范围校验，字段间失败不回滚。
 */
typedef struct {
    aCommBindingConfig_t comm;
    aCommEndpoint_t *protocol;
} aModbusMappingConfig_t;
void aModbusMappingConfigStructInit(aModbusMappingConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusMappingDeInitStatic(aModbusMapping_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusMappingDestroy(aModbusMapping_t *handle);
#endif
/** 主站直接在远端地址与本地 SIG/Param 之间传输。
 * 读取：远端 → 解码 → 数据端点；写入：数据端点 → 编码 → 远端。
 * 长度由目标元信息决定；角色/站号/区域与远端设备定义由应用保证。
 */
typedef struct {
    uint8_t unit_id;
    uint16_t area;
    uint16_t address;
    aCommTarget_t target;
    uint8_t word_order; /**< 端点定义的编码选项，通用层原样传递。 */
    aTimeout_t timeout;
    aCommEndpointResult_t *result;
} aModbusMappingClientSigRequest_t;
/** 主站通信表的一条只读映射，供采集计划及事件请求共用。
 * 数组构成完整通信表；站号、事务超时由设备统一配置。
 * comm.poll_time_ms 仅声明自动采集策略，计时和执行进度由调度器维护；
 * 配为零的条目仍可用于临时读写和事件请求。
 * comm.flags 指定读写权限；不含 BREAK_BEFORE 时允许自动合并。
 * STRUCT 可选择字段或 WHOLE 整组；标量/RAW 使用 ACOMM_MAP_SIG_WHOLE。
 * 整组布局要求与从站映射相同；单个采集项不会被拆成多帧。
 * quantity 必须显式填写且非零，不根据数据端点自动补全。
 * quantity 的单位和类型转换由协议端点定义，通用层不规定寄存器宽度。
 * 库检查远端长度与数据端点目标匹配，不提供隐式截断或类型扩展。
 */
typedef struct {
    aCommEntry_t comm; /**< 公共首成员；由 aComm 访问和调度。 */
    uint16_t area;
    uint16_t address;
    uint16_t quantity; /**< 地址单位数，单位宽度由端点定义。 */
    uint8_t word_order; /**< 端点定义的编码选项，通用层原样传递。 */
} aModbusClientMap_t;
/** 主站默认设备通信表。可以只用即时接口：maps=NULL、map_count=0。
 * 通信表也用于可选的总线请求绑定，不要求创建采集计划。
 * 其他站号通过即时请求或独立映射上下文访问，同一线路由应用串行化。
 * 生命周期内表不可变；调度器、端口和任务由应用创建及销毁。
 */
typedef struct {
    aModbusMappingConfig_t binding;
    const aModbusClientMap_t *maps;
    size_t map_count;
    uint8_t unit_id;
    aTimeout_t request_timeout;
    aCommSubmitFn_t submit;
    void *submit_context;
} aModbusMappingClientConfig_t;
void aModbusMappingClientConfigStructInit(aModbusMappingClientConfig_t *config);
void aModbusMappingClientSigRequestStructInit(
    aModbusMappingClientSigRequest_t *request);
aStatus_t aModbusMappingClientConfigCheck(
    const aModbusMappingClientConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusMappingClientInitStatic(
    const aModbusMappingClientConfig_t *config,
    aModbusMapping_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusMappingClientCreate(const aModbusMappingClientConfig_t *config,
    aModbusMapping_t **handle_out);
#endif
/** 启动前校验全部采集项，不分配资源、不访问端口、不修改数据。
 * config 填充 data/protocol 端点；检查地址与布局，目标定义须已挂载。
 * 默认路径也检查 snapshot 容量，大 SIG 须提供足够的工作区。
 */
aStatus_t aModbusMappingClientSigCheck(const aModbusMappingConfig_t *config,
    const aModbusMappingClientSigRequest_t *request, aBool_t write);
/** 检查主站映射、权限、周期声明、长度及站号，不进行 I/O。
 * 包括不自动采集的条目；超时由调用方另外校验。
 */
aStatus_t aModbusMappingClientMapCheck(const aModbusMappingConfig_t *config,
    const aModbusClientMap_t *map, uint8_t unit_id);
aStatus_t aModbusMappingClientReadSig(aModbusMapping_t *handle,
    const aModbusMappingClientSigRequest_t *request);
aStatus_t aModbusMappingClientWriteSig(aModbusMapping_t *handle,
    const aModbusMappingClientSigRequest_t *request);
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
    aCommEntry_t comm; /**< 公共首成员；由 aComm 访问和调度。 */
    uint16_t area;
    uint16_t address;
    uint16_t quantity; /**< 地址单位数，单位宽度由端点定义。 */
    uint8_t word_order; /**< 端点定义的编码选项，通用层原样传递。 */
} aModbusServerMap_t;
/** 按 area、address 排序的回调地址段；同一区域不得重叠。
 * 必须提供 flags 所允许的读写函数，回调负责段内数据的解释与校验。
 * 命中后权限错误或回调失败直接返回，不再尝试离散映射。
 */
typedef struct {
    uint16_t area;
    uint16_t address;
    uint32_t quantity; /**< 1..65536，address + quantity 不超过 65536。 */
    uint16_t flags;
    aModbusAddressReadFn_t read;
    aModbusAddressWriteFn_t write;
    void *context;
} aModbusMappingAddressRange_t;
/** 非阻塞观察回调，仅设置标志或尝试投递工作，禁止 I/O、等待和重入。
 * mapping/input 只在本次回调中借用；事件不表示主站已经读取数据。
 */
typedef void (*aModbusMappingServerNotifyFn_t)(void *context,
    const aModbusServerMap_t *mapping, const aCommNotifyInput_t *input);
typedef struct {
    aModbusMappingConfig_t binding;
    const aModbusServerMap_t *maps;
    size_t map_count;
    const aModbusMappingAddressRange_t *ranges;
    size_t range_count;
    aModbusMappingServerNotifyFn_t notify;
    void *notify_context;
} aModbusMappingServerConfig_t;
void aModbusMappingServerConfigStructInit(aModbusMappingServerConfig_t *config);
void aModbusMappingServerMapStructInit(aModbusServerMap_t *map);
void aModbusMappingAddressRangeStructInit(aModbusMappingAddressRange_t *range);
/** 校验配置及映射，不进行 I/O 或分配资源。 */
aStatus_t aModbusMappingServerConfigCheck(
    const aModbusMappingServerConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusMappingServerInitStatic(
    const aModbusMappingServerConfig_t *config,
    aModbusMapping_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusMappingServerCreate(const aModbusMappingServerConfig_t *config,
    aModbusMapping_t **handle_out);
#endif
/** 注册给协议端点的地址 read/write，context 为本映射实例。
 * 同步访问映射的数据，无队列、无轮询计划；单消费者，重入返回 BUSY。
 */
aStatus_t aModbusMappingServerRead(void *context,
    const aModbusAddressReadRequest_t *request);
aStatus_t aModbusMappingServerWrite(void *context,
    const aModbusAddressWriteRequest_t *request);
/** 表数组回调；context 为已初始化的 aModbusMapping_t。
 * get 按具体结构体步长定位，aComm 只通过公共首成员访问。
 */
extern const aCommTableOps_t aModbusClientTableOps;

/** 交给 aComm.protocol.config 的只读协议参数；映射数组在通用注册中提供。
 * transport_type 选择 Modbus 报文格式，不表示通道的 STREAM/GAP 模式。
 * 收发回调在初始化时由 channel 注入，不放入产品参数。
 * timeout：主站一次事务预算，从站一次接收处理预算。
 * ranges/notify 仅从站使用；主站须置空。通道时序由应用配置。
 */
typedef struct {
    aModbusRole_t role;
    aModbusTransportType_t transport_type;
    uint8_t unit_id;
    aTimeout_t byte_timeout;
    aTimeout_t timeout;
    const aModbusMappingAddressRange_t *ranges;
    size_t range_count;
    aModbusMappingServerNotifyFn_t notify;
    void *notify_context;
} aModbusCommConfig_t;

/** 应用持有的注册环境，零初始化；通道和数据端点只借用。
 * 初始化前填写 channel、binding 和可选 submit；运行期间禁止修改。
 * 静态资源指针为空时使用动态 API；未启用动态分配则必须提供静态资源。
 * aComm 销毁成功后协议与映射已释放，应用随后关闭端口、释放通道。
 */
typedef struct {
    aCommChannel_t *channel;
    aCommBindingConfig_t binding;
    aCommSubmitFn_t submit;
    void *submit_context;
#if AMODBUS_STATIC_ENABLE
    aModbusHandle_t *protocol_storage;
    aModbusMapping_t *mapping_storage;
#endif
    /* 以下由注册回调维护；protocol 可借用执行串行化的临时事务。 */
    aModbusHandle_t *protocol;
    aModbusMapping_t *mapping;
    aModbusServerProcessRequest_t process;
} aModbusComm_t;

/** 应用选择以下函数组成 aCommProtocolOps_t，本模块不定义生命周期回调表。
 * registration.config 指向 aModbusCommConfig_t，context 指向 aModbusComm_t；
 * items 按 role 指向具体的主站或从站映射数组。
 * Check 只检查配置，不分配资源、不访问端口。
 */
aStatus_t aModbusCommCheck(const aCommProtocol_t *registration);
/** 创建协议与映射；主站返回采集表，从站返回空表以使用 process 回调。
 * 初始化失败回收本次资源；通道及数据端点由应用持有。
 */
aStatus_t aModbusCommInit(const aCommProtocol_t *registration,
    aCommPlanConfig_t *table);
/** 从站服务步骤；context 为 aModbusComm_t，结果通过 result 返回。
 * 主站使用采集表的 execute，其注册表中的 process 可为 NULL。
 */
void aModbusCommProcess(void *context, size_t step,
    aCommStepResult_t *result);
/** 停止所有使用者、解除计划引用后释放协议与映射；context 仍归应用。 */
aStatus_t aModbusCommDeInit(void *context);
#endif
