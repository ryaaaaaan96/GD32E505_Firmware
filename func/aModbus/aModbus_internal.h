#ifndef A_MODBUS_INTERNAL_H
#define A_MODBUS_INTERNAL_H

#include "aModbus_instance.h"
#include "aOS.h"

typedef struct {
    aDataType_t type;
    size_t size;
    const aBusRange_t *range;
    uint16_t quantity;
} aModbusValueInfo_t;

/* 所有内部入口由已取得实例使用权的 Process/Client 调用。 */
aTimeout_t aModbusRemaining(aModbusHandle_t *handle);
aBool_t aModbusAreaIsBits(aModbusArea_t area);
aStatus_t aModbusAccessCheck(aModbusArea_t area, uint16_t address,
    uint16_t quantity, const void *data, size_t size, aBool_t write);
aStatus_t aModbusBusDefinitionsCheck(const aModbusConfig_t *config);
aStatus_t aModbusBusTargetInfo(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, aModbusArea_t area,
    aModbusWordOrder_t order, aModbusValueInfo_t *info);
aStatus_t aModbusBusReadValue(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, void *data, size_t size);
aStatus_t aModbusBusWriteValue(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, const void *data, size_t size);
aStatus_t aModbusValueDecode(const aModbusValueInfo_t *info,
    aModbusArea_t area, aModbusWordOrder_t order,
    const void *protocol, void *value);
aStatus_t aModbusValueEncode(const aModbusValueInfo_t *info,
    aModbusArea_t area, aModbusWordOrder_t order,
    const void *value, void *protocol);
aStatus_t aModbusBusRead(aModbusHandle_t *handle,
    const aModbusAddressReadRequest_t *request);
aStatus_t aModbusBusWrite(aModbusHandle_t *handle,
    const aModbusAddressWriteRequest_t *request);

#endif
