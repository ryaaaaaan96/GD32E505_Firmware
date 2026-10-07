#include "aModbus_internal.h"

#include <string.h>

typedef union {
    max_align_t alignment;
    uint8_t bytes[AMODBUS_MAX_VALUE_SIZE];
} value_buffer_t;

aBool_t aModbusAreaIsBits(aModbusArea_t area)
{
    return area == AMODBUS_AREA_COILS ||
           area == AMODBUS_AREA_DISCRETE_INPUTS;
}

static aBool_t area_valid(aModbusArea_t area)
{
    return (unsigned)area <= AMODBUS_AREA_INPUT_REGISTERS;
}

static aBool_t area_writable(aModbusArea_t area)
{
    return area == AMODBUS_AREA_COILS ||
           area == AMODBUS_AREA_HOLDING_REGISTERS;
}

aStatus_t aModbusAccessCheck(aModbusArea_t area, uint16_t address,
    uint16_t quantity, const void *data, size_t size, aBool_t write)
{
    size_t required;
    unsigned maximum;
    if (!area_valid(area) || data == NULL || quantity == 0U ||
        (uint32_t)address + quantity > 65536U) {
        return A_STATUS_INVALID_PARAM;
    }
    if (write && !area_writable(area)) return A_STATUS_UNSUPPORTED;
    if (aModbusAreaIsBits(area)) {
        maximum = write ? 1968U : AMODBUS_MAX_BITS;
        required = (quantity + 7U) / 8U;
    } else {
        maximum = write ? 123U : AMODBUS_MAX_REGISTERS;
        required = quantity * sizeof(uint16_t);
        if ((uintptr_t)data % _Alignof(uint16_t) != 0U) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    return quantity <= maximum && size >= required ? A_STATUS_OK :
           A_STATUS_INVALID_PARAM;
}

static aStatus_t target_info(aBusHandle_t *bus,
    const aModbusSigTarget_t *target, aModbusArea_t area,
    aModbusWordOrder_t order, aModbusValueInfo_t *info)
{
    const aBusSigQuery_t query = {
        .deviceID = target->deviceID, .sigIndex = target->sigIndex
    };
    aBusSigInfo_t sig;
    aStatus_t status;

    if (!area_valid(area) || (order != AMODBUS_WORD_HIGH_FIRST &&
        order != AMODBUS_WORD_LOW_FIRST)) return A_STATUS_INVALID_PARAM;
    status = aBusGetSigInfo(bus, &query, &sig);
    if (status != A_STATUS_OK) return status;
    if (target->paramIndex == AMODBUS_SIG_WHOLE) {
        info->type = sig.type;
        info->size = sig.size;
        info->range = sig.range;
    } else {
        const aBusParam_t *param;
        if (sig.type != ALIB_DATA_STRUCT) return A_STATUS_UNSUPPORTED;
        if (target->paramIndex >= sig.param_count || sig.params == NULL) {
            return A_STATUS_NOT_FOUND;
        }
        param = &sig.params[target->paramIndex];
        if (param->offset > sig.size ||
            param->size > sig.size - param->offset) {
            return A_STATUS_INVALID_PARAM;
        }
        info->type = param->type;
        info->size = param->size;
        info->range = param->range;
    }
    if (info->size == 0U || info->size > AMODBUS_MAX_VALUE_SIZE) {
        return A_STATUS_INVALID_PARAM;
    }
    if (info->type == ALIB_DATA_RAW) {
        if (info->range != NULL || order != AMODBUS_WORD_HIGH_FIRST) {
            return A_STATUS_INVALID_PARAM;
        }
    } else if (aDataTypeSize(info->type) != info->size) {
        return A_STATUS_UNSUPPORTED;
    }
    if (aModbusAreaIsBits(area)) {
        if (info->type != ALIB_DATA_U8 ||
            order != AMODBUS_WORD_HIGH_FIRST) return A_STATUS_UNSUPPORTED;
        info->quantity = 1U;
    } else {
        info->quantity = (uint16_t)((info->size + 1U) / 2U);
        if (info->size != 4U && order != AMODBUS_WORD_HIGH_FIRST) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    return A_STATUS_OK;
}

aStatus_t aModbusBusTargetInfo(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, aModbusArea_t area,
    aModbusWordOrder_t order, aModbusValueInfo_t *info)
{
    return target_info(handle->config.bus, target, area, order, info);
}

aStatus_t aModbusBusDefinitionsCheck(const aModbusConfig_t *config)
{
    uint32_t previous_end = 0U;
    aModbusArea_t previous_area = AMODBUS_AREA_COILS;
    if ((config->ranges == NULL) != (config->range_count == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    /* 每段检查区域、地址边界、顺序、权限及两种处理方式是否互斥。 */
    for (size_t i = 0U; i < config->range_count; i++) {
        const aModbusAddressRange_t *range = &config->ranges[i];
        uint32_t end = (uint32_t)range->address + range->quantity;
        uint32_t map_end = range->address;
        if (!area_valid(range->area) || range->quantity == 0U ||
            range->quantity > 65536U || end > 65536U ||
            range->flags == 0U ||
            (range->flags & ~(AMODBUS_ACCESS_READ |
                              AMODBUS_ACCESS_WRITE)) != 0U ||
            (!area_writable(range->area) &&
             (range->flags & AMODBUS_ACCESS_WRITE) != 0U) ||
            (i != 0U && (range->area < previous_area ||
             (range->area == previous_area &&
              range->address < previous_end)))) {
            return A_STATUS_INVALID_PARAM;
        }
        previous_area = range->area;
        previous_end = end;
        if (range->map_count == 0U) {
            if (range->maps != NULL ||
                ((range->flags & AMODBUS_ACCESS_READ) != 0U &&
                 range->read == NULL) ||
                ((range->flags & AMODBUS_ACCESS_WRITE) != 0U &&
                 range->write == NULL)) return A_STATUS_INVALID_PARAM;
            continue;
        }
        if (range->maps == NULL || range->read != NULL ||
            range->write != NULL) return A_STATUS_INVALID_PARAM;
        /* 每项检查 aBus 目标、类型/长度、映射覆盖以及写入可达性。 */
        for (size_t j = 0U; j < range->map_count; j++) {
            const aModbusBusMap_t *map = &range->maps[j];
            aModbusValueInfo_t info;
            aStatus_t status = target_info(config->bus, &map->target,
                range->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            if (map->flags == 0U || (map->flags & ~range->flags) != 0U ||
                map->address < map_end ||
                (uint32_t)map->address + info.quantity > end ||
                (!aModbusAreaIsBits(range->area) &&
                 (map->flags & AMODBUS_ACCESS_WRITE) != 0U &&
                 info.quantity > 123U)) return A_STATUS_INVALID_PARAM;
            map_end = (uint32_t)map->address + info.quantity;
        }
    }
    return A_STATUS_OK;
}

aStatus_t aModbusBusReadValue(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, void *data, size_t size)
{
    if (handle->config.sig_read != NULL) {
        const aModbusSigReadRequest_t request = {
            .target = *target, .data = data, .size = size,
            .timeout = aModbusRemaining(handle)
        };
        return handle->config.sig_read(handle->config.sig_context,
                                        &request);
    }
    if (target->paramIndex == AMODBUS_SIG_WHOLE) {
        const aBusGetIndexRequest_t request = {
            .deviceID = target->deviceID, .sigIndex = target->sigIndex,
            .dst = data, .size = size, .timeout = aModbusRemaining(handle)
        };
        return aBusGetByIndex(handle->config.bus, &request);
    } else {
        const aBusGetParamRequest_t request = {
            .deviceID = target->deviceID, .sigIndex = target->sigIndex,
            .paramIndex = target->paramIndex, .dst = data, .size = size,
            .timeout = aModbusRemaining(handle)
        };
        return aBusGetParam(handle->config.bus, &request);
    }
}

aStatus_t aModbusBusWriteValue(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, const void *data, size_t size)
{
    if (handle->config.sig_write != NULL) {
        const aModbusSigWriteRequest_t request = {
            .target = *target, .data = data, .size = size,
            .timeout = aModbusRemaining(handle)
        };
        return handle->config.sig_write(handle->config.sig_context,
                                         &request);
    }
    if (target->paramIndex == AMODBUS_SIG_WHOLE) {
        const aBusSetIndexRequest_t request = {
            .deviceID = target->deviceID, .sigIndex = target->sigIndex,
            .src = data, .size = size, .timeout = aModbusRemaining(handle)
        };
        return aBusSetByIndex(handle->config.bus, &request);
    } else {
        const aBusSetParamRequest_t request = {
            .deviceID = target->deviceID, .sigIndex = target->sigIndex,
            .paramIndex = target->paramIndex, .src = data, .size = size,
            .timeout = aModbusRemaining(handle)
        };
        return aBusSetParam(handle->config.bus, &request);
    }
}

/* 在提交任何映射前检查协议数值与范围；aBus 提交时仍执行最终校验。 */
static aStatus_t value_check(const aModbusValueInfo_t *info,
                             const void *data)
{
    aDataValue_t value = {0};
    const aBusRange_t *range = info->range;
    if (range == NULL) return A_STATUS_OK;
    memcpy(&value, data, info->size);
    switch (info->type) {
    case ALIB_DATA_U8:
        if (value.u8 >= range->min.u8 && value.u8 <= range->max.u8) {
            return A_STATUS_OK;
        }
        break;
    case ALIB_DATA_U16:
        if (value.u16 >= range->min.u16 && value.u16 <= range->max.u16) {
            return A_STATUS_OK;
        }
        break;
    case ALIB_DATA_U32:
        if (value.u32 >= range->min.u32 && value.u32 <= range->max.u32) {
            return A_STATUS_OK;
        }
        break;
    case ALIB_DATA_S32:
        if (value.s32 >= range->min.s32 && value.s32 <= range->max.s32) {
            return A_STATUS_OK;
        }
        break;
    default: return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_INVALID_PARAM;
}

aStatus_t aModbusValueDecode(const aModbusValueInfo_t *info,
    aModbusArea_t area, aModbusWordOrder_t order,
    const void *protocol, void *value)
{
    const uint16_t *registers = protocol;
    uint8_t *bytes = value;
    uint32_t number;
    if (aModbusAreaIsBits(area)) {
        bytes[0] = (*(const uint8_t *)protocol & 1U) != 0U ? 1U : 0U;
    } else if (info->type == ALIB_DATA_RAW) {
        for (size_t i = 0U; i < info->size; i++) {
            bytes[i] = (uint8_t)(registers[i / 2U] >>
                                 (i % 2U == 0U ? 8U : 0U));
        }
        if ((info->size % 2U) != 0U &&
            (registers[info->quantity - 1U] & 0xFFU) != 0U) {
            return A_STATUS_INVALID_PARAM;
        }
    } else if (info->type == ALIB_DATA_U8) {
        if (registers[0] > UINT8_MAX) return A_STATUS_INVALID_PARAM;
        bytes[0] = (uint8_t)registers[0];
    } else if (info->type == ALIB_DATA_U16) {
        memcpy(value, registers, sizeof(uint16_t));
    } else {
        size_t high = order == AMODBUS_WORD_HIGH_FIRST ? 0U : 1U;
        number = ((uint32_t)registers[high] << 16U) |
                 registers[1U - high];
        memcpy(value, &number, sizeof(number));
    }
    return value_check(info, value);
}

aStatus_t aModbusValueEncode(const aModbusValueInfo_t *info,
    aModbusArea_t area, aModbusWordOrder_t order,
    const void *value, void *protocol)
{
    const uint8_t *bytes = value;
    uint16_t *registers = protocol;
    uint32_t number;
    if (aModbusAreaIsBits(area)) {
        if (bytes[0] > 1U) return A_STATUS_INVALID_PARAM;
        *(uint8_t *)protocol = bytes[0];
    } else if (info->type == ALIB_DATA_RAW) {
        memset(registers, 0, info->quantity * sizeof(*registers));
        for (size_t i = 0U; i < info->size; i++) {
            registers[i / 2U] |= (uint16_t)((uint16_t)bytes[i] <<
                                           (i % 2U == 0U ? 8U : 0U));
        }
    } else if (info->type == ALIB_DATA_U8) {
        registers[0] = bytes[0];
    } else if (info->type == ALIB_DATA_U16) {
        memcpy(registers, value, sizeof(uint16_t));
    } else {
        size_t high = order == AMODBUS_WORD_HIGH_FIRST ? 0U : 1U;
        memcpy(&number, value, sizeof(number));
        registers[high] = (uint16_t)(number >> 16U);
        registers[1U - high] = (uint16_t)number;
    }
    return A_STATUS_OK;
}

/* 定义表按区域和地址排序；二分找到起点之前的最后一段/映射。 */
static const aModbusAddressRange_t *range_find(aModbusHandle_t *handle,
    aModbusArea_t area, uint32_t address)
{
    size_t first = 0U;
    size_t last = handle->config.range_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2U;
        const aModbusAddressRange_t *range = &handle->config.ranges[middle];
        if (range->area < area ||
            (range->area == area && range->address <= address)) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    if (first != 0U) {
        const aModbusAddressRange_t *range =
            &handle->config.ranges[first - 1U];
        if (range->area == area &&
            address < (uint32_t)range->address + range->quantity) {
            return range;
        }
    }
    return NULL;
}

static const aModbusBusMap_t *map_find(const aModbusAddressRange_t *range,
                                     uint32_t address)
{
    size_t first = 0U;
    size_t last = range->map_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2U;
        if (range->maps[middle].address <= address) first = middle + 1U;
        else last = middle;
    }
    return first == 0U ? NULL : &range->maps[first - 1U];
}

static uint16_t segment_size(uint32_t address, uint32_t end,
                             uint16_t remaining)
{
    uint32_t size = end - address;
    return size < remaining ? (uint16_t)size : remaining;
}

/* 第一遍只检查路由；不调用业务回调、不修改 aBus 或读输出。 */
static aStatus_t route_check(aModbusHandle_t *handle, aModbusArea_t area,
    uint16_t address, uint16_t quantity, aBool_t write)
{
    uint32_t at = address;
    uint16_t left = quantity;
    uint16_t permission = write ? AMODBUS_ACCESS_WRITE : AMODBUS_ACCESS_READ;
    while (left != 0U) {
        const aModbusAddressRange_t *range = range_find(handle, area, at);
        uint16_t count;
        if (range == NULL || (range->flags & permission) == 0U) {
            return A_STATUS_NOT_FOUND;
        }
        if (range->map_count == 0U) {
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
        } else {
            const aModbusBusMap_t *map = map_find(range, at);
            aModbusValueInfo_t info;
            aStatus_t status;
            if (map == NULL || (map->flags & permission) == 0U) {
                return A_STATUS_NOT_FOUND;
            }
            status = aModbusBusTargetInfo(handle, &map->target, area,
                                           map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            if (at >= (uint32_t)map->address + info.quantity) {
                return A_STATUS_NOT_FOUND;
            }
            if (write && (at != map->address || left < info.quantity)) {
                return A_STATUS_NOT_FOUND;
            }
            count = segment_size(at, (uint32_t)map->address +
                                  info.quantity, left);
        }
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}

aStatus_t aModbusBusRead(aModbusHandle_t *handle,
    const aModbusAddressReadRequest_t *request)
{
    uint32_t at = request->address;
    uint16_t left = request->quantity;
    aStatus_t status = aModbusAccessCheck(request->area, request->address,
        request->quantity, request->data, request->size, A_FALSE);
    if (status != A_STATUS_OK) return status;
    status = route_check(handle, request->area, request->address,
                          request->quantity, A_FALSE);
    if (status != A_STATUS_OK) return status;
    if (aModbusAreaIsBits(request->area)) {
        memset(request->data, 0, (request->quantity + 7U) / 8U);
    }
    while (left != 0U) {
        const aModbusAddressRange_t *range =
            range_find(handle, request->area, at);
        size_t offset = at - request->address;
        uint16_t count;
        uint8_t bits[NMBS_BITFIELD_BYTES_MAX] = {0};
        if (range->map_count == 0U) {
            aModbusAddressReadRequest_t part = *request;
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
            part.address = (uint16_t)at;
            part.quantity = count;
            part.data = aModbusAreaIsBits(request->area) ? (void *)bits :
                        (uint16_t *)request->data + offset;
            part.size = aModbusAreaIsBits(request->area) ?
                        (count + 7U) / 8U : count * sizeof(uint16_t);
            part.timeout = aModbusRemaining(handle);
            status = range->read(range->context, &part);
        } else {
            const aModbusBusMap_t *map = map_find(range, at);
            aModbusValueInfo_t info;
            value_buffer_t value;
            uint16_t encoded[AMODBUS_MAX_REGISTERS];
            status = aModbusBusTargetInfo(handle, &map->target,
                request->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            count = segment_size(at, (uint32_t)map->address +
                                  info.quantity, left);
            status = aModbusBusReadValue(handle, &map->target,
                                          value.bytes, info.size);
            if (status != A_STATUS_OK) return status;
            status = aModbusValueEncode(&info, request->area,
                map->word_order, value.bytes,
                aModbusAreaIsBits(request->area) ? (void *)bits : encoded);
            if (status == A_STATUS_OK &&
                !aModbusAreaIsBits(request->area)) {
                memcpy((uint16_t *)request->data + offset,
                    encoded + at - map->address, count * sizeof(uint16_t));
            }
        }
        if (status != A_STATUS_OK) return status;
        if (aModbusAreaIsBits(request->area)) {
            uint8_t *out = request->data;
            for (size_t i = 0U; i < count; i++) {
                nmbs_bitfield_write(out, offset + i,
                                    nmbs_bitfield_read(bits, i));
            }
        }
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}

/* 两遍处理写入：先检查所有映射值，再逐项提交。
 * 第二遍的锁/业务回调仍可能失败，跨 SIG 不提供事务性或回滚。
 */
static aStatus_t write_pass(aModbusHandle_t *handle,
    const aModbusAddressWriteRequest_t *request, aBool_t apply)
{
    uint32_t at = request->address;
    uint16_t left = request->quantity;
    while (left != 0U) {
        const aModbusAddressRange_t *range =
            range_find(handle, request->area, at);
        size_t offset = at - request->address;
        uint16_t count;
        uint8_t bits[NMBS_BITFIELD_BYTES_MAX] = {0};
        aStatus_t status = A_STATUS_OK;
        if (range->map_count == 0U) {
            aModbusAddressWriteRequest_t part = *request;
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
            if (apply) {
                part.address = (uint16_t)at;
                part.quantity = count;
                if (aModbusAreaIsBits(request->area)) {
                    const uint8_t *input = request->data;
                    for (size_t i = 0U; i < count; i++) {
                        nmbs_bitfield_write(bits, i,
                            nmbs_bitfield_read(input, offset + i));
                    }
                    part.data = bits;
                    part.size = (count + 7U) / 8U;
                } else {
                    part.data = (const uint16_t *)request->data + offset;
                    part.size = count * sizeof(uint16_t);
                }
                part.timeout = aModbusRemaining(handle);
                status = range->write(range->context, &part);
            }
        } else {
            const aModbusBusMap_t *map = map_find(range, at);
            aModbusValueInfo_t info;
            value_buffer_t value;
            const void *protocol;
            status = aModbusBusTargetInfo(handle, &map->target,
                request->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            count = info.quantity;
            if (aModbusAreaIsBits(request->area)) {
                const uint8_t *input = request->data;
                bits[0] = nmbs_bitfield_read(input, offset) ? 1U : 0U;
                protocol = bits;
            } else {
                protocol = (const uint16_t *)request->data + offset;
            }
            status = aModbusValueDecode(&info, request->area,
                map->word_order, protocol, value.bytes);
            if (status == A_STATUS_OK && apply) {
                status = aModbusBusWriteValue(handle, &map->target,
                                               value.bytes, info.size);
            }
        }
        if (status != A_STATUS_OK) return status;
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}

aStatus_t aModbusBusWrite(aModbusHandle_t *handle,
    const aModbusAddressWriteRequest_t *request)
{
    aStatus_t status = aModbusAccessCheck(request->area, request->address,
        request->quantity, request->data, request->size, A_TRUE);
    if (status != A_STATUS_OK) return status;
    status = route_check(handle, request->area, request->address,
                          request->quantity, A_TRUE);
    if (status != A_STATUS_OK) return status;
    status = write_pass(handle, request, A_FALSE);
    if (status != A_STATUS_OK) return status;
    return write_pass(handle, request, A_TRUE);
}
