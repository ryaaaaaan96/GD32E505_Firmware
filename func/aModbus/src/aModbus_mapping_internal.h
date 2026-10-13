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
/** 线性地址端的协议规则；回调不修改映射，不执行调度。
 * unit_bits 为 1 或 8 的倍数；缓冲区为端点约定的数据表示。
 * copy 处理单位区间与业务值的转换，write 表示解码写入业务值。
 * prepare/read_prepared 可选，预编码对象是字节表示，禁止强转为私有结构。
 */
typedef struct {
    uint16_t unit_bits;
    uint16_t read_limit, write_limit;
    uint32_t address_limit;
    aBool_t writable;
} aModbusMapArea_t;
typedef struct {
    aStatus_t (*ready)(aCommEndpoint_t *);
    aStatus_t (*area)(aCommEndpoint_t *, uint16_t, aModbusMapArea_t *);
    aStatus_t (*check)(aCommEndpoint_t *, const aModbusMapAddress_t *,
        const void *, size_t, aBool_t);
    aStatus_t (*describe)(const aCommDataInfo_t *, const aCommTarget_t *,
        uint16_t, uint8_t, aModbusMapValue_t *);
    aStatus_t (*layout)(const aModbusMapValue_t *);
    aStatus_t (*copy)(const aModbusMapValue_t *, uint16_t, uint8_t,
        size_t, size_t, void *, void *, aBool_t);
    aStatus_t (*slice)(const aCommDataInfo_t *, size_t, uint16_t *, uint8_t *);
    aBool_t (*merge)(aCommEndpoint_t *, const aModbusMapAddress_t *,
        const aModbusMapAddress_t *);
    aStatus_t (*prepare)(aCommEndpoint_t *, const aModbusMapAddress_t *,
        aCommPrepared_t *);
    aStatus_t (*read_prepared)(aCommEndpoint_t *, const aCommPrepared_t *,
        const aCommReadAccess_t *);
    uint32_t provider_type;
} aModbusAddressOps_t;


static inline const aModbusAddressOps_t *aModbusAddressOps(
    const aCommEndpoint_t *endpoint)
{
    return endpoint == NULL || endpoint->ops == NULL ? NULL :
        endpoint->ops->extension;
}

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
aModbusMapArea_t aModbusMappingArea(aCommEndpoint_t *endpoint, uint16_t area);
aStatus_t aModbusMappingAccessCheck(const aModbusMappingConfig_t *config,
    uint8_t unit, uint16_t area, uint16_t address, uint16_t quantity,
    const void *data, size_t size, aBool_t write);
aStatus_t aModbusMappingDataDefinitionsCheck(
    const aModbusMappingServerConfig_t *config);
aStatus_t aModbusMappingDataTargetInfo(aModbusMapping_t *handle,
    const aCommTarget_t *target, uint16_t area,
    uint8_t order, aModbusMapValue_t *info);
aStatus_t aModbusMappingValueDecode(aModbusMapping_t *handle,
    const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
    const void *protocol, void *value);
aStatus_t aModbusMappingValueEncode(aModbusMapping_t *handle,
    const aModbusMapValue_t *info,
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
