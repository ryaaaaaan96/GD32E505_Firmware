/** @brief 主站：即时读写和固定读请求预编码；不依赖业务或调度器。 */
#ifndef AMODBUS_CLIENT_H
#define AMODBUS_CLIENT_H
#include "aModbus.h"

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

/** 固定读请求：准备阶段只编码，不访问端口。
 * 准备后保持不变；RTU 复用含 CRC 的 8 字节请求，TCP 动态生成事务编号。
 * 本对象不包含周期、重试或数据映射，可由任意调用方保存。
 */
typedef struct {
    uint8_t unit_id;
    aModbusArea_t area;
    uint16_t address;
    uint16_t quantity;
} aModbusReadSpec_t;
typedef struct {
    aModbusReadSpec_t spec;
    uint8_t rtu[8]; /**< 协议栈维护，调用方不得自行修改。 */
} aModbusPreparedRead_t;
typedef struct {
    const aModbusPreparedRead_t *frame;
    void *data;
    size_t size;
    aTimeout_t timeout;
    aModbusResult_t *result;
} aModbusPreparedReadRequest_t;

typedef struct {
    aModbusConfig_t modbus;
} aModbusClientConfig_t;

void aModbusClientConfigStructInit(aModbusClientConfig_t *config);
void aModbusClientReadRequestStructInit(aModbusClientReadRequest_t *request);
void aModbusClientWriteRequestStructInit(aModbusClientWriteRequest_t *request);
#if AMODBUS_CLIENT_ENABLE
aStatus_t aModbusClientConfigCheck(const aModbusClientConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusClientInitStatic(const aModbusClientConfig_t *config,
    aModbusHandle_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusClientCreate(const aModbusClientConfig_t *config,
    aModbusHandle_t **handle_out);
#endif
aStatus_t aModbusClientRead(aModbusHandle_t *handle,
    const aModbusClientReadRequest_t *request);
aStatus_t aModbusClientWrite(aModbusHandle_t *handle,
    const aModbusClientWriteRequest_t *request);
aStatus_t aModbusClientPrepareRead(const aModbusReadSpec_t *spec,
    aModbusPreparedRead_t *frame);
aStatus_t aModbusClientReadPrepared(aModbusHandle_t *handle,
    const aModbusPreparedReadRequest_t *request);
#endif
#endif
