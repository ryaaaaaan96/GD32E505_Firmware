#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include "aLib.h"
#if ABUS_ENABLE
#include "aBus.h"

/* 只读点表与静态实例存储由产品提供，运行句柄由 system 私有持有。 */
typedef struct {
    uint16_t instanceID;
    const aBusTable_t *tables;
    size_t table_count;
#if !ABUS_DYNAMIC_ENABLE
    aBusHandle_t *instance;
#endif
} dataBusConfig_t;

/* 检查产品 STRUCT 字段长度总和后填充配置；失败保持 config 不变。
 * 不修改实例、不创建总线或任务，可以重复查询。
 * config 仅调用期间借用；返回的点表、默认值及实例存储具有静态生命周期。
 */
aStatus_t protocolBusConfigInit(dataBusConfig_t *config);
#if APP_DATABASE_ENABLE
#include "aDataBase.h"
/* 容量与点表由同一 JSON 生成，供 system 分配静态索引 RAM。 */
#include "protocol_generated.h"

/* 只填充 tables/table_count、persist_sigs/persist_count，可重复查询。
 * 引用具有静态生命周期；内存区域、索引 RAM 及数据库实例由 system 管理。
 */
void protocolPersistenceConfigInit(aDataBaseKvConfig_t *config);
#endif
#else
/* 总线关闭时无需引入 aBus 头文件，保留统一的协议初始化签名。 */
typedef struct aBusHandle aBusHandle_t;
#endif

/* 创建协议实例、端口及通信任务；总线由 system 初始化，测试任务由 main 创建。
 * 启用通信时 bus 必须已初始化，协议只借用且不销毁，须有效至协议停止。
 * 未启用通信时允许 NULL。调用前 aOS 和协议所用基础服务须就绪。
 * 新任务可能立即运行，须在本调用前完成共享数据初始化及必要的参数恢复。
 * 失败回收当前未启动任务的协议；成功或失败后再次调用均返回 BUSY。
 * 仅启动阶段单线程调用，不提供运行时重启。
 */
aStatus_t protocolInit(aBusHandle_t *bus);

/* 请求提前采集一轮；重复请求合并，从站模式返回 UNSUPPORTED。 */
aStatus_t protocolRefresh(void);

typedef struct {
    uint32_t work_count;
    uint32_t error_count;
    uint32_t timeout_count;
    uint32_t max_process_ms; /**< 单次 Process 时长，含等待；不是整轮周期。 */
    uint32_t dropped_frames;
    size_t queued_frames;
} protocolStats_t;

/* 各计数单独采样，可能跨一次事务；仅任务上下文，计数自然回绕。 */
aStatus_t protocolGetStats(protocolStats_t *stats);

#if APP_MODBUS_ENABLE
#include "aModbus_client.h"
#if ABUS_EVENT_ENABLE
/* 按总线目标投递；不含地址、站号、字序，数据借用至 RequestDone。 */
aStatus_t protocolRequest(const aBusRequestConfig_t *config,
    aBusRequest_t *request, aBusDispatchResult_t *result);
#endif
/* 临时请求和数据借用至完成回调结束；回调在通信任务内执行。
 * response 仅回调期间有效；原 access.result 不使用，统一经 complete 返回。
 */
typedef struct {
    aBool_t write;
    union {
        aModbusClientReadRequest_t read;
        aModbusClientWriteRequest_t write;
    } access;
    void *context;
    void (*complete)(void *context, aStatus_t status,
                     const aModbusResult_t *response);
} protocolModbusRequest_t;
void protocolModbusRequestStructInit(protocolModbusRequest_t *request);
aStatus_t protocolModbusSubmit(const protocolModbusRequest_t *request);
#endif

#endif
