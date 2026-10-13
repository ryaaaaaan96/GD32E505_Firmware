#ifndef A_MODBUS_INTERNAL_H
#define A_MODBUS_INTERNAL_H

#include "aModbus_instance.h"
#include "aOS.h"

aTimeout_t aModbusRemaining(aModbusHandle_t *handle);
aBool_t aModbusAreaIsBits(aModbusArea_t area);
aStatus_t aModbusAccessCheck(aModbusArea_t area, uint16_t address,
    uint16_t quantity, const void *data, size_t size, aBool_t write);

nmbs_error aModbusReadFrame(aModbusHandle_t *handle, uint8_t unit,
    aModbusArea_t area, uint16_t address, uint16_t quantity, void *data);

/* 共享传输和生命周期，业务从 client/server 入口进入。 */
int32_t aModbusTimeoutMs(aTimeout_t timeout);
int32_t aModbusTransportRead(uint8_t *buffer, uint16_t count,
    int32_t byte_timeout_ms, void *context);
int32_t aModbusTransportWrite(const uint8_t *buffer, uint16_t count,
    int32_t byte_timeout_ms, void *context);
void aModbusTransportDiscard(nmbs_t *core, void *context);
aStatus_t aModbusOperationBegin(aModbusHandle_t *handle,
    aModbusRole_t role, aTimeout_t timeout, aModbusResult_t *result);
aStatus_t aModbusOperationEnd(aModbusHandle_t *handle, nmbs_error error,
    aModbusResult_t *result);
aStatus_t aModbusConfigCheck(const aModbusConfig_t *config);
aStatus_t aModbusStoragePrepare(aModbusHandle_t *handle);
aStatus_t aModbusCoreInit(aModbusHandle_t *handle, aModbusRole_t role,
    uint8_t unit_id);
aStatus_t aModbusCoreDeInit(aModbusHandle_t *handle);
#if AMODBUS_DYNAMIC_ENABLE
aModbusHandle_t *aModbusAllocate(void);
#endif
#if AMODBUS_SERVER_ENABLE
void aModbusServerCallbacks(aModbusHandle_t *handle,
    nmbs_callbacks *callbacks);
#endif
void aModbusEndpointInit(aModbusHandle_t *handle);
#endif
