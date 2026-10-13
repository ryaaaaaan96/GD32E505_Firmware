#ifndef AMODBUS_MAPPING_INTERNAL_H
#define AMODBUS_MAPPING_INTERNAL_H
#include "aModbus_mapping_instance.h"
#include "aOS.h"

/* 地址规则与业务值的转换契约，仅供本模块使用。 */
typedef struct {
    aDataType_t type;
    size_t size;
    const aDataRange_t *range;
    uint16_t quantity;
    const aDataField_t *params;
    size_t param_count;
} aModbusMapValue_t;
typedef struct {
    uint8_t unit_id;
    uint16_t area;
    uint16_t address;
    uint16_t quantity;
} aModbusMapAddress_t;
/** Modbus 区域规则：位区域单位为 1 bit，寄存器区域为 16 bit。
 * 只描述地址容量、读写上限及权限，不参与调度。 */
typedef struct {
    uint16_t unit_bits;
    uint16_t read_limit, write_limit;
    uint32_t address_limit;
    aBool_t writable;
} aModbusMapArea_t;
/* Modbus 内部规则直接调用，不再通过通用端点扩展表二次分派。 */
aStatus_t aModbusMapEndpointCheck(const aCommEndpoint_t *endpoint);
aStatus_t aModbusMapReady(aCommEndpoint_t *endpoint);
aStatus_t aModbusMapArea(uint16_t area, aModbusMapArea_t *info);
aStatus_t aModbusMapCheck(aCommEndpoint_t *endpoint,
    const aModbusMapAddress_t *address, const void *data, size_t size,
    aBool_t write);
aStatus_t aModbusMapDescribe(const aCommDataInfo_t *sig,
    const aCommTarget_t *target, uint16_t area, uint8_t order,
    aModbusMapValue_t *info);
aStatus_t aModbusMapLayout(const aModbusMapValue_t *info);
aStatus_t aModbusMapCopy(const aModbusMapValue_t *info, uint16_t area,
    uint8_t order, size_t first, size_t count, void *value, void *wire,
    aBool_t write);
aStatus_t aModbusMapSlice(const aCommDataInfo_t *info, size_t index,
    uint16_t *offset, uint8_t *order);
aBool_t aModbusMapMerge(const aModbusMapAddress_t *first,
    const aModbusMapAddress_t *next);
aStatus_t aModbusMapPrepare(const aModbusMapAddress_t *address,
    aCommPrepared_t *prepared);
aStatus_t aModbusMapReadPrepared(aCommEndpoint_t *endpoint,
    const aCommPrepared_t *prepared, const aCommReadAccess_t *request);

typedef union {
    max_align_t alignment;
    uint8_t bytes[ACOMM_VALUE_CAPACITY];
} aModbusMappingDataBuffer_t;
/* 配置目标只提取定位部分；运行时请求不携带订阅策略。 */
#define ACOMM_MAP_LOCATION(t) ((aCommTarget_t){ \
    (t).deviceID, (t).sigIndex, (t).paramIndex})
#define AMODBUS_MAP_TARGET(c) ((aCommTargetConfig_t){ \
    (c).deviceID, (c).sigIndex, (c).paramIndex, \
    (c).subscribe_events, (c).publish_events})
/* 所有内部入口由已取得实例使用权的 Process/Client 调用。 */
aTimeout_t aModbusMappingRemaining(aModbusMapping_t *handle);
aModbusMapArea_t aModbusMappingArea(uint16_t area);
aStatus_t aModbusMappingAccessCheck(const aModbusMappingConfig_t *config,
    uint8_t unit, uint16_t area, uint16_t address, uint16_t quantity,
    const void *data, size_t size, aBool_t write);
aStatus_t aModbusMappingDataDefinitionsCheck(
    const aModbusMappingServerConfig_t *config);
aStatus_t aModbusMappingDataTargetInfo(aModbusMapping_t *handle,
    const aCommTarget_t *target, uint16_t area,
    uint8_t order, aModbusMapValue_t *info);
aStatus_t aModbusMappingValueDecode(const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
    const void *protocol, void *value);
aStatus_t aModbusMappingValueEncode(const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
    const void *value, void *protocol);
aStatus_t aModbusMappingDataRead(aModbusMapping_t *handle,
    const aModbusAddressReadRequest_t *request);
aStatus_t aModbusMappingDataWrite(aModbusMapping_t *handle,
    const aModbusAddressWriteRequest_t *request);
/* 计划帧按原表下标解码并按 SIG 发布；indices 必须有效。 */
aStatus_t aModbusMappingDataPublish(aModbusMapping_t *handle,
    const aModbusClientMap_t *items, const size_t *indices, size_t count,
    const void *data);
aStatus_t aModbusMappingConfigCheck(const aModbusMappingConfig_t *config);
/* 紧凑位数组按低位在前保存，地址单位为位时使用。 */
static inline aBool_t bit_read(const uint8_t *data, size_t index)
{
    return (data[index / 8U] & (1U << (index % 8U))) != 0U;
}

static inline void bit_write(uint8_t *data, size_t index, aBool_t value)
{
    uint8_t mask = (uint8_t)(1U << (index % 8U));
    if (value) data[index / 8U] |= mask;
    else data[index / 8U] &= (uint8_t)~mask;
}
#endif
