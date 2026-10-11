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

static aStatus_t value_info(const aBusSigInfo_t *sig,
    const aModbusSigTarget_t *target, aModbusArea_t area,
    aModbusWordOrder_t order, aModbusValueInfo_t *info)
{
    info->params = NULL;
    info->param_count = 0U;
    if (!area_valid(area) || (order != AMODBUS_WORD_HIGH_FIRST &&
        order != AMODBUS_WORD_LOW_FIRST)) return A_STATUS_INVALID_PARAM;
    if (target->paramIndex == AMODBUS_SIG_WHOLE) {
        info->type = sig->type;
        info->size = sig->size;
        info->range = sig->range;
    } else {
        const aBusParam_t *param;
        if (sig->type != ALIB_DATA_STRUCT) return A_STATUS_UNSUPPORTED;
        if (target->paramIndex >= sig->param_count || sig->params == NULL) {
            return A_STATUS_NOT_FOUND;
        }
        param = &sig->params[target->paramIndex];
        if (param->offset > sig->size ||
            param->size > sig->size - param->offset) {
            return A_STATUS_INVALID_PARAM;
        }
        info->type = param->type;
        info->size = param->size;
        info->range = param->range;
    }
    if (info->size == 0U || info->size > AMODBUS_MAX_VALUE_SIZE) {
        return A_STATUS_INVALID_PARAM;
    }
    if (info->type == ALIB_DATA_STRUCT) {
        if (aModbusAreaIsBits(area) ||
            target->paramIndex != AMODBUS_SIG_WHOLE) {
            return A_STATUS_UNSUPPORTED;
        }
        if (info->range != NULL || sig->params == NULL ||
            sig->param_count == 0U || info->size % 2U != 0U) {
            return A_STATUS_INVALID_PARAM;
        }
        info->params = sig->params;
        info->param_count = sig->param_count;
        info->quantity = (uint16_t)(info->size / 2U);
        return A_STATUS_OK;
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

/* 严格整组布局：每项占完整寄存器，按偏移连续覆盖全部 RAM 字节。
 * 从站初始化及主站请求预检查执行；定义在实例使用期间不可修改。
 */
static aStatus_t structure_check(const aModbusValueInfo_t *info)
{
    size_t end = 0U;
    if (info->type != ALIB_DATA_STRUCT) return A_STATUS_OK;
    for (size_t i = 0U; i < info->param_count; i++) {
        const aBusParam_t *param = &info->params[i];
        if (param->offset != end || param->size == 0U ||
            param->size % 2U != 0U || param->size > info->size - end) {
            return A_STATUS_INVALID_PARAM;
        }
        if (param->type == ALIB_DATA_RAW) {
            if (param->range != NULL) return A_STATUS_INVALID_PARAM;
        } else if ((param->type != ALIB_DATA_U16 &&
                    param->type != ALIB_DATA_U32 &&
                    param->type != ALIB_DATA_S32) ||
                   aDataTypeSize(param->type) != param->size) {
            return A_STATUS_UNSUPPORTED;
        }
        end += param->size;
    }
    return end == info->size ? A_STATUS_OK : A_STATUS_INVALID_PARAM;
}

static aModbusValueInfo_t param_info(const aBusParam_t *param)
{
    const aModbusValueInfo_t info = {
        .type = param->type, .size = param->size, .range = param->range,
        .quantity = (uint16_t)(param->size / 2U)
    };
    return info;
}

/* 整组字序只影响 32 位整数，RAW 和 U16 保持各自固定编码规则。 */
static aModbusWordOrder_t param_order(const aBusParam_t *param,
    aModbusWordOrder_t order)
{
    return param->type == ALIB_DATA_U32 || param->type == ALIB_DATA_S32 ?
           order : AMODBUS_WORD_HIGH_FIRST;
}

static aStatus_t target_info(aBusHandle_t *bus,
    const aModbusSigTarget_t *target, aModbusArea_t area,
    aModbusWordOrder_t order, aModbusValueInfo_t *info)
{
    const aBusSigQuery_t query = {target->deviceID, target->sigIndex};
    aBusSigInfo_t sig;
    aStatus_t status = aBusGetSigInfo(bus, &query, &sig);
    if (status != A_STATUS_OK) return status;
    return value_info(&sig, target, area, order, info);
}

aStatus_t aModbusBusTargetInfo(aModbusHandle_t *handle,
    const aModbusSigTarget_t *target, aModbusArea_t area,
    aModbusWordOrder_t order, aModbusValueInfo_t *info)
{
    aStatus_t status = target_info(handle->config.bus, target,
                                    area, order, info);
    /* 从站映射已在初始化检查；主站可临时提交任意目标，需逐请求检查。 */
    if (status == A_STATUS_OK && handle->config.role == AMODBUS_ROLE_CLIENT) {
        return structure_check(info);
    }
    return status;
}

aStatus_t aModbusClientSigCheck(const aModbusConfig_t *config,
    const aModbusClientSigRequest_t *request, aBool_t write)
{
    aModbusValueInfo_t info;
    aStatus_t status;
    uint16_t placeholder;

    if (config == NULL || config->bus == NULL || request == NULL ||
        config->role != AMODBUS_ROLE_CLIENT ||
        !aTimeoutIsValid(request->timeout)) return A_STATUS_INVALID_PARAM;
    if (config->transport_type == AMODBUS_TRANSPORT_RTU) {
        if (request->unit_id > 247U || (!write && request->unit_id == 0U)) {
            return A_STATUS_INVALID_PARAM;
        }
    } else if (config->transport_type != AMODBUS_TRANSPORT_TCP) {
        return A_STATUS_INVALID_PARAM;
    }
    status = target_info(config->bus, &request->target, request->area,
                        request->word_order, &info);
    if (status != A_STATUS_OK) return status;
    status = structure_check(&info);
    if (status != A_STATUS_OK) return status;
    if (!write && config->sig_write == NULL) {
        aBusSigInfo_t sig;
        const aBusSigQuery_t query = {
            request->target.deviceID, request->target.sigIndex
        };
        size_t capacity = config->snapshot == NULL ?
            AMODBUS_MAX_VALUE_SIZE : config->snapshot_size;
        status = aBusGetSigInfo(config->bus, &query, &sig);
        if (status != A_STATUS_OK) return status;
        if (sig.size > capacity) return A_STATUS_INVALID_PARAM;
    }
    /* AccessCheck 只检查地址、容量及对齐，不解引用缓冲区。 */
    return aModbusAccessCheck(request->area, request->address,
        info.quantity, &placeholder, AMODBUS_MAX_VALUE_SIZE, write);
}

aStatus_t aModbusClientPollCheck(const aModbusConfig_t *config,
    const aModbusPoll_t *poll, uint8_t unit_id)
{
    aModbusValueInfo_t info;
    aStatus_t status;

    if (poll == NULL || poll->quantity == 0U ||
        (poll->flags & ~AMODBUS_POLL_FLAG_BREAK_BEFORE) != 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    /* 复用单项请求的类型/地址检查；此处只校验，不执行或等待事务。 */
    const aModbusClientSigRequest_t request = {
        .unit_id = unit_id, .area = poll->area, .address = poll->address,
        .target = poll->target, .word_order = poll->word_order,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    status = aModbusClientSigCheck(config, &request, A_FALSE);
    if (status != A_STATUS_OK) return status;
    status = target_info(config->bus, &poll->target, poll->area,
                        poll->word_order, &info);
    if (status != A_STATUS_OK) return status;
    /* 显式长度是协议配置；目标类型改变后必须重新核对，不能静默扩宽读取。 */
    return poll->quantity == info.quantity ? A_STATUS_OK :
           A_STATUS_INVALID_PARAM;
}

/* 区域决定基础读写能力；每个回调段和离散映射独立声明权限。 */
static aBool_t permissions_valid(aModbusArea_t area, uint16_t flags)
{
    return flags != 0U &&
        (flags & ~(AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE)) == 0U &&
        (area_writable(area) || (flags & AMODBUS_ACCESS_WRITE) == 0U);
}

aStatus_t aModbusBusDefinitionsCheck(const aModbusConfig_t *config)
{
    uint32_t previous_end = 0U;
    aModbusArea_t previous_area = AMODBUS_AREA_COILS;
    size_t range_index = 0U;

    if ((config->ranges == NULL) != (config->range_count == 0U) ||
        (config->maps == NULL) != (config->map_count == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    /* 先检查回调段的边界、排序、权限及必需回调。 */
    for (size_t i = 0U; i < config->range_count; i++) {
        const aModbusAddressRange_t *range = &config->ranges[i];
        uint32_t end;
        if (!area_valid(range->area) || range->quantity == 0U ||
            range->quantity > 65536U ||
            !permissions_valid(range->area, range->flags)) {
            return A_STATUS_INVALID_PARAM;
        }
        end = (uint32_t)range->address + range->quantity;
        if (end > 65536U || (i != 0U &&
            (range->area < previous_area ||
             (range->area == previous_area &&
              range->address < previous_end))) ||
            ((range->flags & AMODBUS_ACCESS_READ) != 0U &&
             range->read == NULL) ||
            ((range->flags & AMODBUS_ACCESS_WRITE) != 0U &&
             range->write == NULL)) {
            return A_STATUS_INVALID_PARAM;
        }
        previous_area = range->area;
        previous_end = end;
    }
    previous_area = AMODBUS_AREA_COILS;
    previous_end = 0U;
    /* 校验显式协议长度与 aBus 目标匹配，再检查排序和区间重叠。 */
    for (size_t i = 0U; i < config->map_count; i++) {
        const aModbusBusMap_t *map = &config->maps[i];
        aModbusValueInfo_t info;
        aStatus_t status;
        uint32_t end;
        if (!area_valid(map->area) || map->quantity == 0U ||
            !permissions_valid(map->area, map->flags)) {
            return A_STATUS_INVALID_PARAM;
        }
        status = target_info(config->bus, &map->target, map->area,
                             map->word_order, &info);
        if (status != A_STATUS_OK) return status;
        status = structure_check(&info);
        if (status != A_STATUS_OK) return status;
        if (map->quantity != info.quantity) return A_STATUS_INVALID_PARAM;
        end = (uint32_t)map->address + map->quantity;
        if (end > 65536U || (i != 0U &&
            (map->area < previous_area ||
             (map->area == previous_area && map->address < previous_end))) ||
            (!aModbusAreaIsBits(map->area) &&
             (map->flags & AMODBUS_ACCESS_WRITE) != 0U &&
             map->quantity > 123U)) {
            return A_STATUS_INVALID_PARAM;
        }
        previous_area = map->area;
        previous_end = end;
        /* 两张有序表只向前遍历，检查段与映射的完整区间是否相交。 */
        while (range_index < config->range_count) {
            const aModbusAddressRange_t *range = &config->ranges[range_index];
            if (range->area < map->area ||
                (range->area == map->area &&
                 (uint32_t)range->address + range->quantity <= map->address)) {
                range_index++;
                continue;
            }
            if (range->area == map->area && range->address < end) {
                return A_STATUS_INVALID_PARAM;
            }
            break;
        }
        if (((map->flags & AMODBUS_ACCESS_READ) != 0U &&
             config->sig_read == NULL) ||
            ((map->flags & AMODBUS_ACCESS_WRITE) != 0U &&
             config->sig_write == NULL)) {
            aBusSigInfo_t sig;
            const aBusSigQuery_t query = {
                map->target.deviceID, map->target.sigIndex
            };
            size_t capacity = config->snapshot == NULL ?
                AMODBUS_MAX_VALUE_SIZE : config->snapshot_size;
            status = aBusGetSigInfo(config->bus, &query, &sig);
            if (status != A_STATUS_OK) return status;
            if (sig.size > capacity) return A_STATUS_INVALID_PARAM;
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

static aStatus_t value_decode(const aModbusValueInfo_t *info,
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

static aStatus_t value_encode(const aModbusValueInfo_t *info,
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

/* 只复制所请求的寄存器；标量/RAW 写入必须完整，读取可截取半个标量。 */
static aStatus_t scalar_register_copy(const aModbusValueInfo_t *info,
    aModbusWordOrder_t order, size_t first, size_t count,
    void *value, uint16_t *registers, aBool_t write)
{
    uint16_t encoded[AMODBUS_MAX_REGISTERS];
    aStatus_t status;
    if (write) {
        if (first != 0U || count != info->quantity) {
            return A_STATUS_NOT_FOUND;
        }
        return value_decode(info, AMODBUS_AREA_HOLDING_REGISTERS,
                            order, registers, value);
    }
    status = value_encode(info, AMODBUS_AREA_HOLDING_REGISTERS,
                           order, value, encoded);
    if (status == A_STATUS_OK) {
        memcpy(registers, encoded + first, count * sizeof(uint16_t));
    }
    return status;
}

/* 整组使用 offset / 2 定位，只编解码本次请求涉及的字段。
 * 不展开运行时点表；value 为整组工作区，保留未涉及的字段。
 */
static aStatus_t register_copy(const aModbusValueInfo_t *info,
    aModbusWordOrder_t order, size_t first, size_t count,
    void *value, uint16_t *registers, aBool_t write)
{
    size_t end = first + count;
    if (info->type != ALIB_DATA_STRUCT) {
        return scalar_register_copy(info, order, first, count,
                                    value, registers, write);
    }
    for (size_t i = 0U; i < info->param_count; i++) {
        const aBusParam_t *param = &info->params[i];
        size_t start = param->offset / 2U;
        size_t finish = start + param->size / 2U;
        size_t at, until;
        aModbusValueInfo_t field;
        aStatus_t status;
        if (start >= end) break;
        if (finish <= first) continue;
        at = start > first ? start : first;
        until = finish < end ? finish : end;
        field = param_info(param);
        status = scalar_register_copy(&field, param_order(param, order),
            at - start, until - at, (uint8_t *)value + param->offset,
            registers + at - first, write);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

aStatus_t aModbusValueDecode(const aModbusValueInfo_t *info,
    aModbusArea_t area, aModbusWordOrder_t order,
    const void *protocol, void *value)
{
    if (info->type == ALIB_DATA_STRUCT) {
        return register_copy(info, order, 0U, info->quantity, value,
                              (uint16_t *)protocol, A_TRUE);
    }
    return value_decode(info, area, order, protocol, value);
}

aStatus_t aModbusValueEncode(const aModbusValueInfo_t *info,
    aModbusArea_t area, aModbusWordOrder_t order,
    const void *value, void *protocol)
{
    if (info->type == ALIB_DATA_STRUCT) {
        return register_copy(info, order, 0U, info->quantity,
                              (void *)value, protocol, A_FALSE);
    }
    return value_encode(info, area, order, value, protocol);
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

/* 返回首个大于查询地址的映射下标；前一项可能覆盖查询起点。 */
static size_t map_upper_bound(const aModbusConfig_t *config,
    aModbusArea_t area, uint32_t address)
{
    size_t first = 0U;
    size_t last = config->map_count;
    while (first < last) {
        size_t middle = first + (last - first) / 2U;
        const aModbusBusMap_t *map = &config->maps[middle];
        if (map->area < area ||
            (map->area == area && map->address <= address)) {
            first = middle + 1U;
        } else {
            last = middle;
        }
    }
    return first;
}

static const aModbusBusMap_t *map_find(aModbusHandle_t *handle,
    aModbusArea_t area, uint32_t address)
{
    size_t after = map_upper_bound(&handle->config, area, address);
    if (after != 0U && handle->config.maps[after - 1U].area == area) {
        return &handle->config.maps[after - 1U];
    }
    return NULL;
}

static uint16_t segment_size(uint32_t address, uint32_t end,
                             uint16_t remaining)
{
    uint32_t size = end - address;
    return size < remaining ? (uint16_t)size : remaining;
}

/* 部分整组写入必须在字段边界开始和结束，禁止半值读改写。 */
static aStatus_t map_write_check(aModbusHandle_t *handle,
    const aModbusBusMap_t *map, uint32_t at, uint16_t count)
{
    aModbusValueInfo_t info;
    aStatus_t status;
    size_t first = (at - map->address) * 2U;
    size_t end = first + count * 2U;
    aBool_t start_found = A_FALSE;
    aBool_t end_found = A_FALSE;
    if (at == map->address && count == map->quantity) return A_STATUS_OK;
    status = aModbusBusTargetInfo(handle, &map->target, map->area,
                                   map->word_order, &info);
    if (status != A_STATUS_OK) return status;
    if (info.type != ALIB_DATA_STRUCT) return A_STATUS_NOT_FOUND;
    for (size_t i = 0U; i < info.param_count; i++) {
        const aBusParam_t *param = &info.params[i];
        if (param->offset == first) start_found = A_TRUE;
        if (param->offset + param->size == end) end_found = A_TRUE;
    }
    return start_found && end_found ? A_STATUS_OK : A_STATUS_NOT_FOUND;
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
        if (range != NULL) {
            if ((range->flags & permission) == 0U) return A_STATUS_NOT_FOUND;
            count = segment_size(at, (uint32_t)range->address +
                                  range->quantity, left);
        } else {
            const aModbusBusMap_t *map = map_find(handle, area, at);
            if (map == NULL || (map->flags & permission) == 0U) {
                return A_STATUS_NOT_FOUND;
            }
            /* 初始化已验证长度；路由检查直接使用配置，避免查询 aBus。 */
            if (at >= (uint32_t)map->address + map->quantity) {
                return A_STATUS_NOT_FOUND;
            }
            count = segment_size(at, (uint32_t)map->address +
                                  map->quantity, left);
            if (write) {
                aStatus_t status = map_write_check(handle, map, at, count);
                if (status != A_STATUS_OK) return status;
            }
        }
        at += count;
        left = (uint16_t)(left - count);
    }
    return A_STATUS_OK;
}

/* 帧内同一 SIG 统一取快照或提交，非连续映射也属于同一组。
 * 跨 SIG 仍独立处理；用户自定义回调自行决定同步及事务边界。
 */
static aBool_t same_sig(const aModbusSigTarget_t *a,
                        const aModbusSigTarget_t *b)
{
    return a->deviceID == b->deviceID && a->sigIndex == b->sigIndex;
}

static aBool_t group_seen(aModbusHandle_t *handle, aModbusArea_t area,
    uint32_t start, uint32_t current, const aModbusSigTarget_t *target)
{
    size_t first;
    if (current == start) return A_FALSE;
    first = map_upper_bound(&handle->config, area, start);
    if (first != 0U) first--;
    for (size_t m = first; m < handle->config.map_count; m++) {
        const aModbusBusMap_t *map = &handle->config.maps[m];
        if (map->area < area) continue;
        if (map->area > area || map->address >= current) break;
        if (!same_sig(&map->target, target)) continue;
        if (map->address >= start) return A_TRUE;
        if ((uint32_t)map->address + map->quantity > start) {
            return A_TRUE;
        }
    }
    return A_FALSE;
}

typedef struct {
    aModbusHandle_t *handle;
    aModbusArea_t area;
    uint32_t start;
    uint32_t end;
    void *protocol;
    const aModbusSigTarget_t *target;
    aBusSigInfo_t sig;
    aBool_t write;
} group_access_t;

static aStatus_t group_copy(void *context, void *snapshot, size_t size)
{
    group_access_t *group = context;
    aModbusHandle_t *handle = group->handle;
    size_t first = map_upper_bound(&handle->config, group->area, group->start);
    (void)size;

    /* 从请求起点二分定位，只扫描可能参与本帧的映射。 */
    if (first != 0U) first--;
    for (size_t m = first; m < handle->config.map_count; m++) {
        const aModbusBusMap_t *map = &handle->config.maps[m];
        aModbusValueInfo_t info;
        aStatus_t status;
        uint32_t at, end;
        size_t count, offset;
        uint8_t bit = 0U;
        void *value;
        void *protocol;

        if (map->area < group->area) continue;
        if (map->area > group->area || map->address >= group->end) break;
        if (!same_sig(&map->target, group->target)) continue;
        status = value_info(&group->sig, &map->target, group->area,
                            map->word_order, &info);
        if (status != A_STATUS_OK) return status;
        at = map->address > group->start ? map->address : group->start;
        end = (uint32_t)map->address + map->quantity;
        if (end > group->end) end = group->end;
        if (at >= end) continue;
        count = end - at;
        offset = at - group->start;
        value = snapshot;
        if (map->target.paramIndex != AMODBUS_SIG_WHOLE) {
            value = (uint8_t *)snapshot +
                group->sig.params[map->target.paramIndex].offset;
        }
        if (aModbusAreaIsBits(group->area)) {
            if (group->write) {
                bit = nmbs_bitfield_read(
                    (uint8_t *)group->protocol, offset);
            }
            protocol = &bit;
        } else {
            protocol = (uint16_t *)group->protocol + offset;
        }
        if (!aModbusAreaIsBits(group->area)) {
            status = register_copy(&info, map->word_order,
                at - map->address, count, value, protocol, group->write);
        } else if (group->write) {
            status = aModbusValueDecode(&info, group->area,
                                        map->word_order, protocol, value);
        } else {
            status = aModbusValueEncode(&info, group->area,
                map->word_order, value, &bit);
            if (status == A_STATUS_OK) {
                nmbs_bitfield_write((uint8_t *)group->protocol,
                                    offset, bit != 0U);
            }
        }
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

static aStatus_t group_transfer(aModbusHandle_t *handle,
    aModbusArea_t area, uint16_t address, uint16_t quantity, void *protocol,
    const aModbusSigTarget_t *target, aBool_t write)
{
    group_access_t group = {
        .handle = handle, .area = area, .start = address,
        .end = (uint32_t)address + quantity, .protocol = protocol,
        .target = target, .write = write
    };
    aBusSigQuery_t query = {target->deviceID, target->sigIndex};
    aStatus_t status = aBusGetSigInfo(handle->config.bus, &query,
                                      &group.sig);
    if (status != A_STATUS_OK) return status;
    if (group.sig.size > handle->config.snapshot_size) {
        return A_STATUS_INVALID_PARAM;
    }
    if (write) {
        const aBusUpdateRequest_t update = {
            .target = query, .buffer = handle->config.snapshot,
            .capacity = handle->config.snapshot_size,
            .edit = group_copy, .context = &group,
            .timeout = aModbusRemaining(handle)
        };
        return aBusUpdateSig(handle->config.bus, &update);
    } else {
        const aBusGetIndexRequest_t read = {
            .deviceID = query.deviceID, .sigIndex = query.sigIndex,
            .dst = handle->config.snapshot, .size = group.sig.size,
            .timeout = aModbusRemaining(handle)
        };
        status = aBusGetByIndex(handle->config.bus, &read);
        if (status != A_STATUS_OK) return status;
        return group_copy(&group, handle->config.snapshot, group.sig.size);
    }
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
        if (range != NULL) {
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
            const aModbusBusMap_t *map = map_find(handle, request->area, at);
            aModbusValueInfo_t info;
            value_buffer_t value;
            count = segment_size(at, (uint32_t)map->address +
                                  map->quantity, left);
            if (handle->config.sig_read == NULL) {
                if (!group_seen(handle, request->area, request->address,
                                 at, &map->target)) {
                    status = group_transfer(handle, request->area,
                        request->address, request->quantity, request->data,
                        &map->target, A_FALSE);
                    if (status != A_STATUS_OK) return status;
                }
                at += count;
                left = (uint16_t)(left - count);
                continue;
            }
            status = aModbusBusTargetInfo(handle, &map->target,
                request->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            status = aModbusBusReadValue(handle, &map->target,
                                          value.bytes, info.size);
            if (status != A_STATUS_OK) return status;
            if (aModbusAreaIsBits(request->area)) {
                status = aModbusValueEncode(&info, request->area,
                    map->word_order, value.bytes, bits);
            } else {
                status = register_copy(&info, map->word_order,
                    at - map->address, count, value.bytes,
                    (uint16_t *)request->data + offset, A_FALSE);
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

/* 自定义整组回调在完整写入时调用一次；部分写入按完整字段回调。
 * 不读取业务旧值来合成整组，回调提供者自行保证多字段同步和失败语义。
 */
static aStatus_t map_callback_write(aModbusHandle_t *handle,
    const aModbusBusMap_t *map, const aModbusValueInfo_t *info,
    size_t first, size_t count, const void *value)
{
    if (info->type != ALIB_DATA_STRUCT ||
        (first == 0U && count == info->quantity)) {
        return aModbusBusWriteValue(handle, &map->target, value, info->size);
    }
    for (size_t i = 0U; i < info->param_count; i++) {
        const aBusParam_t *param = &info->params[i];
        aModbusSigTarget_t target = map->target;
        aStatus_t status;
        if (param->offset < first * 2U) continue;
        if (param->offset >= (first + count) * 2U) break;
        target.paramIndex = i;
        status = aModbusBusWriteValue(handle, &target,
            (const uint8_t *)value + param->offset, param->size);
        if (status != A_STATUS_OK) return status;
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
        if (range != NULL) {
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
            const aModbusBusMap_t *map = map_find(handle, request->area, at);
            aModbusValueInfo_t info;
            value_buffer_t value;
            const void *protocol;
            status = aModbusBusTargetInfo(handle, &map->target,
                request->area, map->word_order, &info);
            if (status != A_STATUS_OK) return status;
            count = segment_size(at, (uint32_t)map->address +
                                  map->quantity, left);
            if (aModbusAreaIsBits(request->area)) {
                const uint8_t *input = request->data;
                bits[0] = nmbs_bitfield_read(input, offset) ? 1U : 0U;
                protocol = bits;
            } else {
                protocol = (const uint16_t *)request->data + offset;
            }
            if (aModbusAreaIsBits(request->area)) {
                status = aModbusValueDecode(&info, request->area,
                    map->word_order, protocol, value.bytes);
            } else {
                status = register_copy(&info, map->word_order,
                    at - map->address, count, value.bytes,
                    (uint16_t *)protocol, A_TRUE);
            }
            if (status == A_STATUS_OK && apply) {
                if (handle->config.sig_write == NULL) {
                    if (!group_seen(handle, request->area, request->address,
                                     at, &map->target)) {
                        status = group_transfer(handle, request->area,
                            request->address, request->quantity,
                            (void *)request->data, &map->target, A_TRUE);
                    }
                } else {
                    status = map_callback_write(handle, map, &info,
                        at - map->address, count, value.bytes);
                }
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

/* 一次远端读取可覆盖多个 SIG，提交前先检查所有目标值。 */
typedef struct {
    const aModbusPoll_t *items;
    size_t count;
    size_t selected;
    const void *data;
    aBusSigInfo_t sig;
} publish_context_t;

static aStatus_t publish_edit(void *context, void *snapshot, size_t size)
{
    publish_context_t *publish = context;
    const aModbusPoll_t *first = publish->items;
    const aModbusSigTarget_t *target =
        &publish->items[publish->selected].target;
    (void)size;

    for (size_t i = 0U; i < publish->count; i++) {
        const aModbusPoll_t *item = &publish->items[i];
        aModbusValueInfo_t info;
        aStatus_t status;
        uint8_t bit;
        void *value = snapshot;
        const void *protocol;
        size_t offset = item->address - first->address;
        if (!same_sig(&item->target, target)) continue;
        status = value_info(&publish->sig, &item->target, item->area,
                            item->word_order, &info);
        if (status != A_STATUS_OK) return status;
        if (item->target.paramIndex != AMODBUS_SIG_WHOLE) {
            value = (uint8_t *)snapshot +
                publish->sig.params[item->target.paramIndex].offset;
        }
        if (aModbusAreaIsBits(item->area)) {
            bit = nmbs_bitfield_read((const uint8_t *)publish->data, offset);
            protocol = &bit;
        } else {
            protocol = (const uint16_t *)publish->data + offset;
        }
        status = aModbusValueDecode(&info, item->area, item->word_order,
                                    protocol, value);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

aStatus_t aModbusBusPublish(aModbusHandle_t *handle,
    const aModbusPoll_t *items, size_t count, const void *data)
{
    publish_context_t publish = {
        .items = items, .count = count, .data = data
    };
    /* 第一遍解码检查范围；任何无效值都不会触发数据提交。 */
    for (size_t i = 0U; i < count; i++) {
        aModbusValueInfo_t info;
        value_buffer_t value;
        uint8_t bit;
        const void *protocol;
        size_t offset = items[i].address - items[0].address;
        aStatus_t status = aModbusBusTargetInfo(handle, &items[i].target,
            items[i].area, items[i].word_order, &info);
        if (status != A_STATUS_OK) return status;
        if (aModbusAreaIsBits(items[i].area)) {
            bit = nmbs_bitfield_read((const uint8_t *)data, offset);
            protocol = &bit;
        } else {
            protocol = (const uint16_t *)data + offset;
        }
        status = aModbusValueDecode(&info, items[i].area, items[i].word_order,
                                    protocol, value.bytes);
        if (status != A_STATUS_OK) return status;
    }
    for (size_t i = 0U; i < count; i++) {
        const aModbusSigTarget_t *target = &items[i].target;
        aBusSigQuery_t query = {target->deviceID, target->sigIndex};
        aStatus_t status;
        aBool_t seen = A_FALSE;
        if (handle->config.sig_write != NULL) {
            aModbusValueInfo_t info;
            value_buffer_t value;
            uint8_t bit;
            const void *protocol;
            size_t offset = items[i].address - items[0].address;
            status = aModbusBusTargetInfo(handle, target, items[i].area,
                                          items[i].word_order, &info);
            if (status != A_STATUS_OK) return status;
            if (aModbusAreaIsBits(items[i].area)) {
                bit = nmbs_bitfield_read((const uint8_t *)data, offset);
                protocol = &bit;
            } else {
                protocol = (const uint16_t *)data + offset;
            }
            status = aModbusValueDecode(&info, items[i].area,
                items[i].word_order, protocol, value.bytes);
            if (status == A_STATUS_OK) {
                status = aModbusBusWriteValue(handle, target,
                                              value.bytes, info.size);
            }
        } else {
            for (size_t j = 0U; j < i; j++) {
                if (same_sig(target, &items[j].target)) seen = A_TRUE;
            }
            if (seen) continue;
            publish.selected = i;
            status = aBusGetSigInfo(handle->config.bus, &query, &publish.sig);
            if (status == A_STATUS_OK) {
                const aBusUpdateRequest_t update = {
                    .target = query, .buffer = handle->config.snapshot,
                    .capacity = handle->config.snapshot_size,
                    .edit = publish_edit, .context = &publish,
                    .timeout = aModbusRemaining(handle)
                };
                status = aBusUpdateSig(handle->config.bus, &update);
            }
        }
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}
