#ifndef ACOMM_MAPPING_INTERNAL_H
#define ACOMM_MAPPING_INTERNAL_H
#include "aComm_mapping_instance.h"
#include "aOS.h"
typedef union {
    max_align_t alignment;
    uint8_t bytes[ACOMM_VALUE_CAPACITY];
} aCommMappingDataBuffer_t;
/* 配置目标只提取定位部分；运行时请求不携带订阅策略。 */
#define ACOMM_MAP_LOCATION(t) ((aCommMappingSigTarget_t){ \
    (t).deviceID, (t).sigIndex, (t).paramIndex})
/* 所有内部入口由已取得实例使用权的 Process/Client 调用。 */
aTimeout_t aCommMappingRemaining(aCommMapping_t *handle);
aCommAreaInfo_t aCommMappingArea(aCommEndpoint_t *endpoint, uint16_t area);
aStatus_t aCommMappingAccessCheck(const aCommMappingConfig_t *config,
    uint8_t unit, uint16_t area, uint16_t address, uint16_t quantity,
    const void *data, size_t size, aBool_t write);
aStatus_t aCommMappingDataDefinitionsCheck(
    const aCommMappingServerConfig_t *config);
aStatus_t aCommMappingDataTargetInfo(aCommMapping_t *handle,
    const aCommMappingSigTarget_t *target, uint16_t area,
    uint8_t order, aCommValueInfo_t *info);
aStatus_t aCommMappingDataReadValue(aCommMapping_t *handle,
    const aCommMappingSigTarget_t *target, void *data, size_t size);
aStatus_t aCommMappingDataWriteValue(aCommMapping_t *handle,
    const aCommMappingSigTarget_t *target, const void *data, size_t size);
aStatus_t aCommMappingValueDecode(aCommMapping_t *handle,
    const aCommValueInfo_t *info,
    uint16_t area, uint8_t order,
    const void *protocol, void *value);
aStatus_t aCommMappingValueEncode(aCommMapping_t *handle,
    const aCommValueInfo_t *info,
    uint16_t area, uint8_t order,
    const void *value, void *protocol);
aStatus_t aCommMappingDataRead(aCommMapping_t *handle,
    const aCommAddressReadRequest_t *request);
aStatus_t aCommMappingDataWrite(aCommMapping_t *handle,
    const aCommAddressWriteRequest_t *request);
/* 计划帧按原表下标解码并按 SIG 发布；indices 必须有效。 */
aStatus_t aCommMappingDataPublish(aCommMapping_t *handle,
    const aCommMappingClientMap_t *items, const size_t *indices, size_t count,
    const void *data);
aStatus_t aCommMappingOperationBegin(aCommMapping_t *handle,
    aCommMappingRole_t role, aTimeout_t timeout);
aStatus_t aCommMappingConfigCheck(const aCommMappingConfig_t *config);
aStatus_t aCommMappingStoragePrepare(aCommMapping_t *handle,
    size_t subscriptions);
aStatus_t aCommMappingCoreInit(aCommMapping_t *handle,
    aCommMappingRole_t role);
aStatus_t aCommMappingCoreDeInit(aCommMapping_t *handle);
#if ACOMM_DYNAMIC_ENABLE
aCommMapping_t *aCommMappingAllocate(const aCommMappingConfig_t *config,
    size_t subscriptions);
#endif
aStatus_t aCommMappingSubscribe(aCommMapping_t *handle,
    const aCommTargetConfig_t *target, const void *definition,
    aCommNotifyFn_t notify);
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
