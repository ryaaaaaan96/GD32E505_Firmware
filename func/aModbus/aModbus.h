/**
 * @file aModbus.h
 * @brief 基于 nanoMODBUS 的主从站；业务数据统一接入 aBus。
 * 仅任务上下文，不创建任务。同一实例的并发/重入调用返回 BUSY。
 * 生命周期由应用串行编排；销毁前停止调用者，传输与 aBus 均为借用。
 */
#ifndef A_MODBUS_H
#define A_MODBUS_H

#include "aBus.h"

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
/** paramIndex 的特殊值，表示整个标量或 RAW SIG。 */
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

/** 同步传输适配；配置复制，context 借用至销毁。
 * read/write 使用项目 errno 和 aTimeout_t，允许部分进度；0 表示无进展。
 * socket EOF 必须返回 -1/A_EIO，不能作为暂时无数据处理。
 * 回调返回后不得持有缓冲区；TCP 写入必须消费/复制，RTU 还须确认线路完成。
 * RTU 必须提供三个辅助操作：清理残留输入并恢复帧边界、发送前保证帧间隔、
 * 等待线路发送完成。具体微秒时序、接收帧边界及 DE 控制属于传输/设备层。
 * TCP 辅助操作可为 NULL；discard_input 不得丢弃下一条合法 TCP 请求。
 */
typedef struct {
    void *context;
    aSSize_t (*read)(void *context, void *data, size_t size,
                    aTimeout_t timeout);
    aSSize_t (*write)(void *context, const void *data, size_t size,
                     aTimeout_t timeout);
    aStatus_t (*discard_input)(void *context, aTimeout_t timeout);
    aStatus_t (*prepare_frame)(void *context, aTimeout_t timeout);
    aStatus_t (*wait_transmit_complete)(void *context, aTimeout_t timeout);
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

/** 定位一个 aBus 标量/RAW SIG，或 STRUCT 中的一个 Param。 */
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
 */
typedef aStatus_t (*aModbusAddressReadFn_t)(void *context,
    const aModbusAddressReadRequest_t *request);
typedef aStatus_t (*aModbusAddressWriteFn_t)(void *context,
    const aModbusAddressWriteRequest_t *request);
typedef aStatus_t (*aModbusSigReadFn_t)(void *context,
    const aModbusSigReadRequest_t *request);
typedef aStatus_t (*aModbusSigWriteFn_t)(void *context,
    const aModbusSigWriteRequest_t *request);

/** 只读映射；长度、类型、范围来自 aBus，不重复保存。
 * address 在所属地址段内，按地址严格递增且不得相互覆盖。
 * U8/U16 占一寄存器，U32/S32 占两寄存器；位映射仅支持值为 0/1 的 U8。
 * RAW 每寄存器先放前一字节，奇数长度最后一个低字节补零。
 * STRUCT 必须通过 paramIndex 映射字段，不导出结构体布局和填充字节。
 */
typedef struct {
    uint16_t address;
    uint16_t flags;
    aModbusSigTarget_t target;
    aModbusWordOrder_t word_order;
} aModbusBusMap_t;

/** 按 area、address 排序的只读地址段；同一区域不得重叠。
 * maps 模式与直接 read/write 回调模式二选一，禁止混用。
 * 映射间允许空洞，访问空洞返回非法地址；权限由段与映射共同限制。
 * 直接回调模式必须提供 flags 所允许的读写函数。
 */
typedef struct {
    aModbusArea_t area;
    uint16_t address;
    uint32_t quantity; /**< 1..65536，address + quantity 不超过 65536。 */
    uint16_t flags;
    const aModbusBusMap_t *maps;
    size_t map_count;
    aModbusAddressReadFn_t read;
    aModbusAddressWriteFn_t write;
    void *context;
} aModbusAddressRange_t;

typedef struct {
    aModbusRole_t role;
    aModbusTransportType_t transport_type;
    uint8_t unit_id; /**< RTU 从站为 1..247；主站目标在每次请求中设置。 */
    aModbusTransport_t transport;
    aBusHandle_t *bus; /**< 必填，已初始化；协议模块不销毁。 */
    aTimeout_t byte_timeout; /**< 每次传输的字节等待限制，默认 20 ms。 */
    const aModbusAddressRange_t *ranges; /**< 从站路由；借用至销毁。 */
    size_t range_count;
    aModbusSigReadFn_t sig_read;
    aModbusSigWriteFn_t sig_write;
    void *sig_context;
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

void aModbusTransportStructInit(aModbusTransport_t *transport);
void aModbusConfigStructInit(aModbusConfig_t *config);
void aModbusSigTargetStructInit(aModbusSigTarget_t *target);
void aModbusAddressReadRequestStructInit(aModbusAddressReadRequest_t *request);
void aModbusAddressWriteRequestStructInit(
    aModbusAddressWriteRequest_t *request);
void aModbusSigReadRequestStructInit(aModbusSigReadRequest_t *request);
void aModbusSigWriteRequestStructInit(aModbusSigWriteRequest_t *request);
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
