#include "aModbus_mapping_internal.h"
#include "aModbus_endpoint.h"
#include "aModbus_instance.h"

#include <string.h>

static const aCommEndpointOps_t endpoint_ops;

static aBool_t area_valid(uint16_t area)
{
    return (unsigned)area <= AMODBUS_AREA_INPUT_REGISTERS;
}

static aBool_t area_writable(uint16_t area)
{
    return area == AMODBUS_AREA_COILS ||
           area == AMODBUS_AREA_HOLDING_REGISTERS;
}

aStatus_t aModbusMapDescribe(const aCommDataInfo_t *sig,
    const aCommTarget_t *target, uint16_t area,
    uint8_t order, aModbusMapValue_t *info)
{
    info->params = NULL;
    info->param_count = 0U;
    if (!area_valid(area) || (order != AMODBUS_WORD_HIGH_FIRST &&
        order != AMODBUS_WORD_LOW_FIRST)) return A_STATUS_INVALID_PARAM;
    if (target->paramIndex == ACOMM_DATA_WHOLE) {
        info->type = sig->type;
        info->size = sig->size;
        info->range = sig->range;
    } else {
        const aDataField_t *param;
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
            target->paramIndex != ACOMM_DATA_WHOLE) {
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
            order != AMODBUS_WORD_HIGH_FIRST)
            return A_STATUS_UNSUPPORTED;
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
aStatus_t aModbusMapLayout(const aModbusMapValue_t *info)
{
    size_t end = 0U;
    if (info->type != ALIB_DATA_STRUCT) return A_STATUS_OK;
    for (size_t i = 0U; i < info->param_count; i++) {
        const aDataField_t *param = &info->params[i];
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

static aModbusMapValue_t param_info(const aDataField_t *param)
{
    const aModbusMapValue_t info = {
        .type = param->type, .size = param->size, .range = param->range,
        .quantity = (uint16_t)(param->size / 2U)
    };
    return info;
}
/* 整组字序只影响 32 位整数，RAW 和 U16 保持各自固定编码规则。 */
static uint8_t param_order(const aDataField_t *param,
    uint8_t order)
{
    return param->type == ALIB_DATA_U32 || param->type == ALIB_DATA_S32 ?
           order : AMODBUS_WORD_HIGH_FIRST;
}

static aStatus_t value_check(const aModbusMapValue_t *info,
                             const void *data)
{
    return aDataRangeCheck(info->type, info->range, data) ? A_STATUS_OK :
        A_STATUS_INVALID_PARAM;
}

static aStatus_t value_decode(const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
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

static aStatus_t value_encode(const aModbusMapValue_t *info,
    uint16_t area, uint8_t order,
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
static aStatus_t scalar_register_copy(const aModbusMapValue_t *info,
    uint8_t order, size_t first, size_t count,
    void *value, uint16_t *registers, aBool_t write)
{
    uint16_t encoded[2];
    aStatus_t status;
    if (write) {
        if (first != 0U || count != info->quantity) {
            return A_STATUS_NOT_FOUND;
        }
        return value_decode(info, AMODBUS_AREA_HOLDING_REGISTERS,
                            order, registers, value);
    }
    if (first == 0U && count == info->quantity) {
        return value_encode(info, AMODBUS_AREA_HOLDING_REGISTERS,
                            order, value, registers);
    }
    if (info->type == ALIB_DATA_RAW) {
        const uint8_t *bytes = value;
        for (size_t i = 0U; i < count; i++) {
            size_t offset = (first + i) * 2U;
            registers[i] = (uint16_t)((uint16_t)bytes[offset] << 8U);
            if (offset + 1U < info->size) registers[i] |= bytes[offset + 1U];
        }
        return A_STATUS_OK;
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
static aStatus_t register_copy(const aModbusMapValue_t *info,
    uint8_t order, size_t first, size_t count,
    void *value, uint16_t *registers, aBool_t write)
{
    size_t end = first + count;
    if (info->type != ALIB_DATA_STRUCT) {
        return scalar_register_copy(info, order, first, count,
                                    value, registers, write);
    }
    for (size_t i = 0U; i < info->param_count; i++) {
        const aDataField_t *param = &info->params[i];
        size_t start = param->offset / 2U;
        size_t finish = start + param->size / 2U;
        size_t at, until;
        aModbusMapValue_t field;
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

aStatus_t aModbusMapCopy(const aModbusMapValue_t *info, uint16_t area,
    uint8_t order, size_t first, size_t count, void *value, void *wire,
    aBool_t write)
{
    if (aModbusAreaIsBits(area)) {
        if (first != 0U || count != 1U) return A_STATUS_NOT_FOUND;
        return write ? value_decode(info, area, order, wire, value) :
                       value_encode(info, area, order, value, wire);
    }
    return register_copy(info, order, first, count, value, wire, write);
}

aStatus_t aModbusMapArea(uint16_t area,
    aModbusMapArea_t *info)
{
    if (!area_valid(area)) return A_STATUS_INVALID_PARAM;
    aBool_t bits = aModbusAreaIsBits(area);
    *info = (aModbusMapArea_t){
        .unit_bits = bits ? 1U : 16U,
        .read_limit = bits ? AMODBUS_MAX_BITS : AMODBUS_MAX_REGISTERS,
        .write_limit = bits ? 1968U : 123U,
        .address_limit = 65536U, .writable = area_writable(area)
    };
    return A_STATUS_OK;
}

aStatus_t aModbusMapCheck(aCommEndpoint_t *base,
    const aModbusMapAddress_t *address, const void *data, size_t size,
    aBool_t write)
{
    aModbusInfo_t info = {.transport_type = AMODBUS_TRANSPORT_RTU};
    if (base != NULL) {
        aModbusHandle_t *handle = (aModbusHandle_t *)base;
        aStatus_t status = aModbusGetInfo(handle, &info);
        if (status != A_STATUS_OK) return status;
    }
    if (info.transport_type == AMODBUS_TRANSPORT_RTU &&
        (address->unit_id > 247U || (!write && address->unit_id == 0U))) {
        return A_STATUS_INVALID_PARAM;
    }
    return aModbusAccessCheck((aModbusArea_t)address->area, address->address,
        address->quantity, data, size, write);
}

aStatus_t aModbusMapReady(aCommEndpoint_t *base)
{
    aModbusInfo_t info;
    if (base == NULL) return A_STATUS_NOT_READY;
    aModbusHandle_t *handle = (aModbusHandle_t *)base;
    aStatus_t status = aModbusGetInfo(handle, &info);
    if (status != A_STATUS_OK) return status;
    return info.role == AMODBUS_ROLE_CLIENT ? A_STATUS_OK :
           A_STATUS_UNSUPPORTED;
}
#if AMODBUS_CLIENT_ENABLE
static void result_copy(aCommEndpointResult_t *out,
    const aModbusResult_t *result)
{
    if (out != NULL) *out = (aCommEndpointResult_t){
        result->exception, result->started, result->retryable
    };
}
#endif
static aStatus_t read_value(aCommEndpoint_t *base,
    const aCommReadAccess_t *request)
{
#if AMODBUS_CLIENT_ENABLE
    if (base == NULL) return A_STATUS_NOT_READY;
    aModbusHandle_t *handle = (aModbusHandle_t *)base;
    const aModbusMapAddress_t *address = request->target;
    aModbusResult_t result = {0};
    const aModbusClientReadRequest_t read = {
        .unit_id = address->unit_id,
        .access = {(aModbusArea_t)address->area, address->address,
            address->quantity, request->data, request->size, request->timeout},
        .result = &result
    };
    aStatus_t status = aModbusClientRead(handle, &read);
    result_copy(request->result, &result);
    return status;
#else
    (void)base; (void)request;
    return A_STATUS_UNSUPPORTED;
#endif
}

static aStatus_t write_value(aCommEndpoint_t *base,
    const aCommWriteAccess_t *request)
{
#if AMODBUS_CLIENT_ENABLE
    if (base == NULL) return A_STATUS_NOT_READY;
    aModbusHandle_t *handle = (aModbusHandle_t *)base;
    const aModbusMapAddress_t *address = request->target;
    aModbusResult_t result = {0};
    const aModbusClientWriteRequest_t write = {
        .unit_id = address->unit_id,
        .access = {(aModbusArea_t)address->area, address->address,
            address->quantity, request->data, request->size, request->timeout},
        .result = &result
    };
    aStatus_t status = aModbusClientWrite(handle, &write);
    result_copy(request->result, &result);
    return status;
#else
    (void)base; (void)request;
    return A_STATUS_UNSUPPORTED;
#endif
}

aStatus_t aModbusMapPrepare(const aModbusMapAddress_t *address,
    aCommPrepared_t *prepared)
{
#if AMODBUS_CLIENT_ENABLE
    aModbusPreparedRead_t frame;
    const aModbusReadSpec_t spec = {address->unit_id,
        (aModbusArea_t)address->area, address->address, address->quantity};
    _Static_assert(sizeof(frame) <= ACOMM_PREPARED_CAPACITY,
                   "prepared frame storage is too small");
    aStatus_t status = aModbusClientPrepareRead(&spec, &frame);
    if (status == A_STATUS_OK) {
        memcpy(prepared->data, &frame, sizeof(frame));
        prepared->size = sizeof(frame);
    }
    return status;
#else
    (void)address; (void)prepared;
    return A_STATUS_UNSUPPORTED;
#endif
}

aStatus_t aModbusMapReadPrepared(aCommEndpoint_t *base,
    const aCommPrepared_t *prepared, const aCommReadAccess_t *request)
{
#if AMODBUS_CLIENT_ENABLE
    if (base == NULL) return A_STATUS_NOT_READY;
    aModbusHandle_t *handle = (aModbusHandle_t *)base;
    aModbusPreparedRead_t frame;
    aModbusResult_t result = {0};
    if (prepared->size != sizeof(frame)) return A_STATUS_INVALID_PARAM;
    memcpy(&frame, prepared->data, sizeof(frame));
    const aModbusPreparedReadRequest_t read = {
        &frame, request->data, request->size, request->timeout, &result
    };
    aStatus_t status = aModbusClientReadPrepared(handle, &read);
    result_copy(request->result, &result);
    return status;
#else
    (void)base; (void)prepared; (void)request;
    return A_STATUS_UNSUPPORTED;
#endif
}

aStatus_t aModbusMapSlice(const aCommDataInfo_t *info, size_t index,
    uint16_t *offset, uint8_t *order)
{
    if (index >= info->param_count) return A_STATUS_NOT_FOUND;
    const aDataField_t *p = &info->params[index];
    if (p->offset % 2U != 0U || p->size % 2U != 0U ||
        p->offset / 2U > UINT16_MAX) return A_STATUS_UNSUPPORTED;
    *offset = (uint16_t)(p->offset / 2U);
    if (p->type != ALIB_DATA_U32 && p->type != ALIB_DATA_S32) {
        *order = AMODBUS_WORD_HIGH_FIRST;
    }
    return A_STATUS_OK;
}

aBool_t aModbusMapMerge(const aModbusMapAddress_t *first,
    const aModbusMapAddress_t *next)
{
    aModbusMapArea_t area;
    if (aModbusMapArea(first->area, &area) != A_STATUS_OK) return A_FALSE;
    return first->unit_id == next->unit_id && first->area == next->area &&
        (uint32_t)first->address + first->quantity == next->address &&
        first->quantity <= area.read_limit &&
        next->quantity <= area.read_limit - first->quantity;
}

static const aCommEndpointOps_t endpoint_ops = {
    .read = read_value, .write = write_value
};
aStatus_t aModbusMapEndpointCheck(const aCommEndpoint_t *endpoint)
{
    return endpoint == NULL || endpoint->ops == &endpoint_ops ?
        A_STATUS_OK : A_STATUS_UNSUPPORTED;
}
_Static_assert(offsetof(aModbusHandle_t, comm) == 0U,
               "common endpoint must be first");
aCommEndpoint_t *aModbusCommEndpoint(aModbusHandle_t *handle)
{
    if (handle == NULL) return NULL;
    return &handle->comm;
}
/* 仅在实例初始化时写入，运行中查询端点不修改状态。 */
void aModbusEndpointInit(aModbusHandle_t *handle)
{
    handle->comm.ops = &endpoint_ops;
}

aStatus_t aModbusBindChannel(aCommChannel_t *channel,
    aModbusTransport_t *transport)
{
    aCommTransport_t source;
    aStatus_t status;
    if (transport == NULL) return A_STATUS_INVALID_PARAM;
    status = aCommChannelBindTransport(channel, &source);
    if (status != A_STATUS_OK) return status;
    /* 仅转交回调，不取得通道所有权，也不暴露任何通道内部布局。 */
    transport->stream = source.stream;
    transport->context = source.context;
    transport->discard_input = source.discard_input;
    transport->prepare_frame = source.prepare_frame;
    transport->wait_transmit_complete = source.wait_transmit_complete;
    transport->finish = source.finish;
    return A_STATUS_OK;
}
