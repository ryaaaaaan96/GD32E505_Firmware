/**
 * @file aModbus.h
 * @brief 基于 nanoMODBUS 的主从站；只处理协议地址和字节传输。
 * 仅任务上下文，不创建任务。同一实例的并发/重入调用返回 BUSY。
 * 生命周期由应用串行编排；销毁前停止调用者，传输和地址回调均为借用。
 */
#ifndef A_MODBUS_H
#define A_MODBUS_H

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

#define AMODBUS_MAX_REGISTERS 125U
#define AMODBUS_MAX_BITS 2000U
#define AMODBUS_MAX_VALUE_SIZE 250U

typedef struct aModbusHandle aModbusHandle_t;

typedef enum {
    AMODBUS_ROLE_CLIENT,
    AMODBUS_ROLE_SERVER
} aModbusRole_t;

/** 大于 19200 baud 时的 RTU 默认静默时间，单位为微秒。 */
#define AMODBUS_RTU_DEFAULT_BYTE_GAP_US 750U
#define AMODBUS_RTU_DEFAULT_FRAME_GAP_US 1750U

/** 仅用于提供协议默认值；应用可调整后交给外部通道，不绑定时钟频率。 */
typedef struct {
    uint32_t character_us;
    uint32_t byte_gap_us;
    uint32_t frame_gap_us;
} aModbusRtuTiming_t;

/** 可选的默认值助手，不访问时钟、不执行等待或分帧。
 * character_bits 是起始、数据、校验、停止位总和，支持 7..16 位。
 * 大于 19200 baud 使用 750/1750 us，其余为 1.5/3.5 字符时间。
 * 全部向上取整到微秒；应用直接提供最终值时不必调用本函数。
 */
aStatus_t aModbusRtuTimingDefaults(uint32_t baud_rate, uint8_t character_bits,
    aModbusRtuTiming_t *timing);

typedef struct {
    aModbusRole_t role;
    uint8_t unit_id; /**< 从站 1..247；主站不在此过滤响应站号。 */
} aModbusRtuFilter_t;

/** 可注册给外部通道的完整帧检查，context 指向只读 Filter。
 * 从站非本机且非广播返回 NOT_FOUND；长度错误返回 ERROR；合格返回 OK。
 * 本检查必须在协议开始处理完整帧前执行，避免额外字节被忽略后写入数据。
 * CRC、功能码支持性及协议异常仍由事务接口处理。
 */
aStatus_t aModbusRtuFrameCheck(void *context, const void *data, size_t size);

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

/** 协议借用的传输，不创建或关闭端口。
 * stream 负责同步字节收发，允许部分进度；回调返回后不得持有缓冲区。
 * 正长度 read 返回 0 表示 EOF，本次操作返回 EOF，后续返回 NOT_READY。
 * 暂无数据使用 -1/A_EAGAIN，等待耗尽使用 -1/A_ETIMEDOUT。
 * flush 可为 NULL；非空时在完整报文提交后调用，再等待 RTU 线路完成。
 * 带 flush 的流写入/提交失败后，后续操作返回 NOT_READY，须清理端口
 * 输出缓冲并重建实例；库无法撤回端口已接收的部分报文。
 * RTU 还必须提供输入恢复、帧间隔和发送完成操作；context 只供这些
 * 辅助操作及 finish 使用，stream.context 独立绑定字节流实例。
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

/** 地址回调同步完成请求；数据只在调用期间借用，禁止重入当前实例。
 * 寄存器是本机 uint16_t 数值，位数据采用低位在前的紧凑位数组。
 * NOT_FOUND/INVALID_PARAM/UNSUPPORTED 分别生成地址/数值/功能异常。
 */
typedef aStatus_t (*aModbusAddressReadFn_t)(void *context,
    const aModbusAddressReadRequest_t *request);
typedef aStatus_t (*aModbusAddressWriteFn_t)(void *context,
    const aModbusAddressWriteRequest_t *request);

/** 只描述传输；不持有业务数据、映射表、订阅或调度器。 */
typedef struct {
    aModbusTransportType_t transport_type;
    aModbusTransport_t transport;
    aTimeout_t byte_timeout;
} aModbusConfig_t;

/** 初始化后只读的信息，供应用或适配层检查装配关系。 */
typedef struct {
    aModbusRole_t role;
    aModbusTransportType_t transport_type;
} aModbusInfo_t;
aStatus_t aModbusGetInfo(const aModbusHandle_t *handle, aModbusInfo_t *info);

/** 协议异常独立保存；从站成功发送异常响应时 Process 仍返回 OK。
 * 当前官方源码仅接受 1..4 异常；其他远端异常作为无效响应处理。
 */
typedef struct {
    uint8_t exception;
    aBool_t started; /**< 已取得事务执行权；入口 BUSY 时为 false。 */
    aBool_t retryable; /**< 可恢复的读取通信错误；不表示协议栈会重试。 */
} aModbusResult_t;

/** 只检查协议地址、数量、缓冲区容量和对齐，不读取缓冲内容。 */
aBool_t aModbusAreaIsBits(aModbusArea_t area);
aStatus_t aModbusAccessCheck(aModbusArea_t area, uint16_t address,
    uint16_t quantity, const void *data, size_t size, aBool_t write);
void aModbusTransportStructInit(aModbusTransport_t *transport);
void aModbusConfigStructInit(aModbusConfig_t *config);
void aModbusAddressReadRequestStructInit(aModbusAddressReadRequest_t *request);
void aModbusAddressWriteRequestStructInit(
    aModbusAddressWriteRequest_t *request);

/** 调用前停止所有使用者，包括借用本实例的通信适配器。
 * 静态实例首次使用须零初始化；只释放本实例，不关闭应用端口。
 */
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusDeInitStatic(aModbusHandle_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusDestroy(aModbusHandle_t *handle);
#endif
#endif
