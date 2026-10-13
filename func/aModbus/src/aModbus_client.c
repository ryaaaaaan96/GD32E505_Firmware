#include "aModbus_internal.h"
#include <limits.h>
#include <string.h>

void aModbusClientReadRequestStructInit(aModbusClientReadRequest_t *request)
{
    if (request != NULL) {
        const aModbusClientReadRequest_t defaults = {
            .unit_id = 1U,
            .access.area = AMODBUS_AREA_HOLDING_REGISTERS,
            .access.timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}

void aModbusClientWriteRequestStructInit(aModbusClientWriteRequest_t *request)
{
    if (request != NULL) {
        const aModbusClientWriteRequest_t defaults = {
            .unit_id = 1U,
            .access.area = AMODBUS_AREA_HOLDING_REGISTERS,
            .access.timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}

#if AMODBUS_CLIENT_ENABLE
static aStatus_t destination_check(aModbusHandle_t *handle,
                                  uint8_t unit, aBool_t write)
{
    if (handle->config->transport_type == AMODBUS_TRANSPORT_RTU &&
        (unit > 247U || (!write && unit == 0U))) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}

nmbs_error aModbusReadFrame(aModbusHandle_t *handle, uint8_t unit,
    aModbusArea_t area, uint16_t address, uint16_t quantity, void *data)
{
    nmbs_error error;
    nmbs_set_destination_rtu_address(&handle->core, unit);
    switch (area) {
    case AMODBUS_AREA_COILS:
        error = nmbs_read_coils(&handle->core, address, quantity, data);
        break;
    case AMODBUS_AREA_DISCRETE_INPUTS:
        error = nmbs_read_discrete_inputs(&handle->core,
            address, quantity, data);
        break;
    case AMODBUS_AREA_HOLDING_REGISTERS:
        return nmbs_read_holding_registers(&handle->core,
            address, quantity, data);
    case AMODBUS_AREA_INPUT_REGISTERS:
        return nmbs_read_input_registers(&handle->core,
            address, quantity, data);
    default: return NMBS_ERROR_INVALID_ARGUMENT;
    }
    /* 上游位读取只检查最大容量，补上与本次请求相等的字节数校验。
     * RTU 的 byte count 在偏移 2，TCP 在 MBAP 后偏移 8。
     * 调用方使用内部位缓冲，校验失败不会发布或复制到业务缓冲区。
     */
    size_t at = handle->config->transport_type == AMODBUS_TRANSPORT_RTU ?
                2U : 8U;
    if (error == NMBS_ERROR_NONE &&
        handle->core.msg.buf[at] != (quantity + 7U) / 8U) {
        return NMBS_ERROR_INVALID_RESPONSE;
    }
    return error;
}

static nmbs_error client_write(aModbusHandle_t *handle, uint8_t unit,
    aModbusArea_t area, uint16_t address, uint16_t quantity, const void *data)
{
    nmbs_set_destination_rtu_address(&handle->core, unit);
    if (area == AMODBUS_AREA_COILS) {
        if (quantity == 1U) {
            return nmbs_write_single_coil(&handle->core, address,
                nmbs_bitfield_read((const uint8_t *)data, 0U));
        }
        return nmbs_write_multiple_coils(&handle->core,
                                         address, quantity, data);
    }
    if (quantity == 1U) {
        uint16_t value;
        memcpy(&value, data, sizeof(value));
        return nmbs_write_single_register(&handle->core, address, value);
    }
    return nmbs_write_multiple_registers(&handle->core,
                                         address, quantity, data);
}

aStatus_t aModbusClientRead(aModbusHandle_t *handle,
    const aModbusClientReadRequest_t *request)
{
    nmbs_bitfield bits;
    const aModbusAddressReadRequest_t *access;
    nmbs_error error;
    aStatus_t status;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (request->result != NULL) *request->result = (aModbusResult_t){0};
    access = &request->access;
    status = aModbusAccessCheck(access->area, access->address,
        access->quantity, access->data, access->size, A_FALSE);
    if (status != A_STATUS_OK) return status;
    status = aModbusOperationBegin(handle, AMODBUS_ROLE_CLIENT,
                              access->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    status = destination_check(handle, request->unit_id, A_FALSE);
    if (status != A_STATUS_OK) {
        aAtomicStore(&handle->active, A_FALSE);
        return status;
    }
    /* 官方位读取会清零完整 nmbs_bitfield，不能直接传应用的小缓冲区。 */
    error = aModbusReadFrame(handle, request->unit_id, access->area,
        access->address, access->quantity,
        aModbusAreaIsBits(access->area) ? (void *)bits : access->data);
    if (error == NMBS_ERROR_NONE && aModbusAreaIsBits(access->area)) {
        memcpy(access->data, bits, (access->quantity + 7U) / 8U);
    }
    return aModbusOperationEnd(handle, error, request->result);
}

aStatus_t aModbusClientWrite(aModbusHandle_t *handle,
    const aModbusClientWriteRequest_t *request)
{
    const aModbusAddressWriteRequest_t *access;
    aStatus_t status;
    nmbs_error error;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (request->result != NULL) *request->result = (aModbusResult_t){0};
    access = &request->access;
    status = aModbusAccessCheck(access->area, access->address,
        access->quantity, access->data, access->size, A_TRUE);
    if (status != A_STATUS_OK) return status;
    status = aModbusOperationBegin(handle, AMODBUS_ROLE_CLIENT,
                              access->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    status = destination_check(handle, request->unit_id, A_TRUE);
    if (status != A_STATUS_OK) {
        aAtomicStore(&handle->active, A_FALSE);
        return status;
    }
    error = client_write(handle, request->unit_id, access->area,
        access->address, access->quantity, access->data);
    status = aModbusOperationEnd(handle, error, request->result);
    if (request->result != NULL) request->result->retryable = A_FALSE;
    return status;
}


#endif

void aModbusClientConfigStructInit(aModbusClientConfig_t *config)
{
    if (config != NULL) aModbusConfigStructInit(&config->modbus);
}
#if AMODBUS_CLIENT_ENABLE
aStatus_t aModbusClientConfigCheck(const aModbusClientConfig_t *config)
{
    return config == NULL ? A_STATUS_INVALID_PARAM :
        aModbusConfigCheck(&config->modbus);
}
static aStatus_t client_init(const aModbusClientConfig_t *config,
    aModbusHandle_t *handle)
{
    aStatus_t status = aModbusStoragePrepare(handle);
    if (status != A_STATUS_OK) return status;
    handle->client = *config;
    return aModbusCoreInit(handle, AMODBUS_ROLE_CLIENT, 0U);
}
#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusClientInitStatic(const aModbusClientConfig_t *config,
    aModbusHandle_t *handle)
{
    aStatus_t status = aModbusClientConfigCheck(config);
    return status == A_STATUS_OK ? client_init(config, handle) : status;
}
#endif
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusClientCreate(const aModbusClientConfig_t *config,
    aModbusHandle_t **handle_out)
{
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    aStatus_t status = aModbusClientConfigCheck(config);
    if (status != A_STATUS_OK) return status;
    aModbusHandle_t *handle = aModbusAllocate();
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = client_init(config, handle);
    if (status != A_STATUS_OK) aOSFree(handle);
    else {
        handle->dynamic = A_TRUE;
        *handle_out = handle;
    }
    return status;
}
#endif
#endif

#if AMODBUS_CLIENT_ENABLE
static nmbs_error prepared_receive(aModbusHandle_t *handle, uint8_t *data,
                                   uint16_t size, int32_t timeout)
{
    int32_t received = aModbusTransportRead(data, size, timeout, handle);
    if (received < 0) return NMBS_ERROR_TRANSPORT;
    return received == size ? NMBS_ERROR_NONE : NMBS_ERROR_TIMEOUT;
}

static nmbs_error prepared_rtu_read(aModbusHandle_t *handle,
    const aModbusPreparedRead_t *frame, aBool_t bits, void *output)
{
    uint8_t *response = handle->core.msg.buf;
    uint16_t bytes = bits ? (frame->spec.quantity + 7U) / 8U :
                           frame->spec.quantity * 2U;
    uint16_t payload, crc;
    int32_t written;
    int32_t byte_timeout = aModbusTimeoutMs(handle->config->byte_timeout);
    nmbs_error error;
    aModbusTransportDiscard(&handle->core, handle);
    if (handle->io_status != A_STATUS_OK) return NMBS_ERROR_TRANSPORT;
    handle->recover_input = A_FALSE;
    written = aModbusTransportWrite(frame->rtu, sizeof(frame->rtu),
                               byte_timeout, handle);
    if (written < 0) return NMBS_ERROR_TRANSPORT;
    if (written != sizeof(frame->rtu)) return NMBS_ERROR_TIMEOUT;
    error = prepared_receive(handle, response, 1U,
                              aModbusTimeoutMs(aModbusRemaining(handle)));
    if (error != NMBS_ERROR_NONE) return error;
    error = prepared_receive(handle, response + 1U, 2U, byte_timeout);
    if (error != NMBS_ERROR_NONE) return error;
    if (response[0] != frame->rtu[0]) return NMBS_ERROR_INVALID_UNIT_ID;
    if (response[1] == (uint8_t)(frame->rtu[1] | 0x80U)) payload = 0U;
    else {
        if (response[1] != frame->rtu[1] || response[2] != bytes) {
            return NMBS_ERROR_INVALID_RESPONSE;
        }
        payload = bytes;
    }
    error = prepared_receive(handle, response + 3U, payload + 2U,
                              byte_timeout);
    if (error != NMBS_ERROR_NONE) return error;
    crc = nmbs_crc_calc(response, payload + 3U, NULL);
    if (response[payload + 3U] != (uint8_t)(crc >> 8U) ||
        response[payload + 4U] != (uint8_t)crc) return NMBS_ERROR_CRC;
    if (payload == 0U) {
        if (response[2] < 1U || response[2] > 4U) {
            return NMBS_ERROR_INVALID_RESPONSE;
        }
        return (nmbs_error)response[2];
    }
    if (bits) memcpy(output, response + 3U, bytes);
    else {
        uint16_t *registers = output;
        for (size_t i = 0U; i < frame->spec.quantity; i++) {
            registers[i] = (uint16_t)((uint16_t)response[3U + i * 2U] << 8U) |
                           response[4U + i * 2U];
        }
    }
    return NMBS_ERROR_NONE;
}


aStatus_t aModbusClientPrepareRead(const aModbusReadSpec_t *spec,
    aModbusPreparedRead_t *frame)
{
    uint16_t placeholder = 0U;
    if (spec == NULL || frame == NULL) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusAccessCheck(spec->area, spec->address,
        spec->quantity, &placeholder, AMODBUS_MAX_VALUE_SIZE, A_FALSE);
    if (status != A_STATUS_OK) return status;
    frame->spec = *spec;
    frame->rtu[0] = spec->unit_id;
    frame->rtu[1] = (uint8_t)(spec->area + 1U);
    frame->rtu[2] = (uint8_t)(spec->address >> 8U);
    frame->rtu[3] = (uint8_t)spec->address;
    frame->rtu[4] = (uint8_t)(spec->quantity >> 8U);
    frame->rtu[5] = (uint8_t)spec->quantity;
    uint16_t crc = nmbs_crc_calc(frame->rtu, 6U, NULL);
    frame->rtu[6] = (uint8_t)(crc >> 8U);
    frame->rtu[7] = (uint8_t)crc;
    return A_STATUS_OK;
}

aStatus_t aModbusClientReadPrepared(aModbusHandle_t *handle,
    const aModbusPreparedReadRequest_t *request)
{
    nmbs_bitfield bits;
    if (request == NULL || request->frame == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (request->result != NULL) *request->result = (aModbusResult_t){0};
    const aModbusReadSpec_t *spec = &request->frame->spec;
    aStatus_t status = aModbusAccessCheck(spec->area, spec->address,
        spec->quantity, request->data, request->size, A_FALSE);
    if (status != A_STATUS_OK) return status;
    status = aModbusOperationBegin(handle, AMODBUS_ROLE_CLIENT,
        request->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    status = destination_check(handle, spec->unit_id, A_FALSE);
    if (status != A_STATUS_OK) {
        aAtomicStore(&handle->active, A_FALSE);
        return status;
    }
    aBool_t bit_area = aModbusAreaIsBits(spec->area);
    void *data = bit_area ? (void *)bits : request->data;
    nmbs_error error =
        handle->config->transport_type == AMODBUS_TRANSPORT_RTU ?
        prepared_rtu_read(handle, request->frame, bit_area, data) :
        aModbusReadFrame(handle, spec->unit_id, spec->area,
            spec->address, spec->quantity, data);
    if (error == NMBS_ERROR_NONE && bit_area) {
        memcpy(request->data, bits, (spec->quantity + 7U) / 8U);
    }
    return aModbusOperationEnd(handle, error, request->result);
}
#endif
