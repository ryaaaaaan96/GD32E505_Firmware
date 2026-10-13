/** @brief 从站：处理协议请求，通过地址回调访问数据；不负责映射。 */
#ifndef AMODBUS_SERVER_H
#define AMODBUS_SERVER_H
#include "aModbus.h"

typedef struct {
    aTimeout_t timeout; /**< 本次 Process 总预算，包括接收、数据回调和发送。 */
    aModbusResult_t *result; /**< 可为 NULL；无请求时成功且 exception 为零。 */
} aModbusServerProcessRequest_t;

typedef struct {
    aModbusConfig_t modbus;
    uint8_t unit_id;
    aModbusAddressReadFn_t read; /**< NULL 表示不支持读取。 */
    aModbusAddressWriteFn_t write; /**< NULL 表示不支持写入。 */
    void *context;
} aModbusServerConfig_t;

void aModbusServerConfigStructInit(aModbusServerConfig_t *config);
void aModbusServerProcessRequestStructInit(
    aModbusServerProcessRequest_t *request);
#if AMODBUS_SERVER_ENABLE
/** 校验传输和地址回调，不进行 I/O 或分配资源。 */
aStatus_t aModbusServerConfigCheck(const aModbusServerConfig_t *config);
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusServerInitStatic(const aModbusServerConfig_t *config,
    aModbusHandle_t *handle);
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusServerCreate(const aModbusServerConfig_t *config,
    aModbusHandle_t **handle_out);
#endif
aStatus_t aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request);
#endif
#endif
