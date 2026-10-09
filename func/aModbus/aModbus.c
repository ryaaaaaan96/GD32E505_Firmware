#include "aModbus_internal.h"

#include <limits.h>
#include <string.h>

void aModbusTransportStructInit(aModbusTransport_t *transport)
{
    if (transport != NULL) {
        const aModbusTransport_t defaults = {0};
        *transport = defaults;
    }
}

void aModbusConfigStructInit(aModbusConfig_t *config)
{
    if (config != NULL) {
        const aModbusConfig_t defaults = {
            .role = AMODBUS_SERVER_ENABLE ? AMODBUS_ROLE_SERVER :
                                           AMODBUS_ROLE_CLIENT,
            .transport_type = AMODBUS_TRANSPORT_RTU,
            .unit_id = 1U,
            .byte_timeout = A_TIMEOUT_MS(20U)
        };
        *config = defaults;
    }
}

void aModbusSigTargetStructInit(aModbusSigTarget_t *target)
{
    if (target != NULL) {
        const aModbusSigTarget_t defaults = {
            .paramIndex = AMODBUS_SIG_WHOLE
        };
        *target = defaults;
    }
}

void aModbusAddressReadRequestStructInit(aModbusAddressReadRequest_t *request)
{
    if (request != NULL) {
        const aModbusAddressReadRequest_t defaults = {
            .area = AMODBUS_AREA_HOLDING_REGISTERS,
            .timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}

void aModbusAddressWriteRequestStructInit(aModbusAddressWriteRequest_t *request)
{
    if (request != NULL) {
        const aModbusAddressWriteRequest_t defaults = {
            .area = AMODBUS_AREA_HOLDING_REGISTERS,
            .timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}

void aModbusSigReadRequestStructInit(aModbusSigReadRequest_t *request)
{
    if (request != NULL) {
        const aModbusSigReadRequest_t defaults = {
            .target.paramIndex = AMODBUS_SIG_WHOLE,
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = defaults;
    }
}

void aModbusSigWriteRequestStructInit(aModbusSigWriteRequest_t *request)
{
    if (request != NULL) {
        const aModbusSigWriteRequest_t defaults = {
            .target.paramIndex = AMODBUS_SIG_WHOLE,
            .timeout = A_TIMEOUT_NO_WAIT
        };
        *request = defaults;
    }
}

void aModbusBusMapStructInit(aModbusBusMap_t *map)
{
    if (map != NULL) {
        const aModbusBusMap_t defaults = {
            .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
            .target.paramIndex = AMODBUS_SIG_WHOLE
        };
        *map = defaults;
    }
}

void aModbusAddressRangeStructInit(aModbusAddressRange_t *range)
{
    if (range != NULL) {
        const aModbusAddressRange_t defaults = {
            .area = AMODBUS_AREA_HOLDING_REGISTERS,
            .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE
        };
        *range = defaults;
    }
}

void aModbusServerProcessRequestStructInit(
    aModbusServerProcessRequest_t *request)
{
    if (request != NULL) {
        const aModbusServerProcessRequest_t defaults = {
            .timeout = A_TIMEOUT_MS(100U)
        };
        *request = defaults;
    }
}

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

void aModbusClientSigRequestStructInit(aModbusClientSigRequest_t *request)
{
    if (request != NULL) {
        const aModbusClientSigRequest_t defaults = {
            .unit_id = 1U,
            .area = AMODBUS_AREA_HOLDING_REGISTERS,
            .target.paramIndex = AMODBUS_SIG_WHOLE,
            .timeout = A_TIMEOUT_MS(1000U)
        };
        *request = defaults;
    }
}

aTimeout_t aModbusRemaining(aModbusHandle_t *handle)
{
    return aTimepointRemaining(&handle->deadline, aOSGetUptimeMs());
}

static int32_t timeout_ms(aTimeout_t timeout)
{
    if (timeout.type == A_TIMEOUT_TYPE_FOREVER) return -1;
    return timeout.milliseconds > INT32_MAX ? INT32_MAX :
           (int32_t)timeout.milliseconds;
}

static aTimeout_t io_timeout(aModbusHandle_t *handle,
                            const aTimepoint_t *byte_deadline)
{
    aTimeout_t total = aModbusRemaining(handle);
    aTimeout_t byte = aTimepointRemaining(byte_deadline,
                                         aOSGetUptimeMs());
    if (total.type == A_TIMEOUT_TYPE_FOREVER) return byte;
    if (byte.type == A_TIMEOUT_TYPE_FOREVER) return total;
    return A_TIMEOUT_MS(total.milliseconds < byte.milliseconds ?
                        total.milliseconds : byte.milliseconds);
}

static void io_fail(aModbusHandle_t *handle, aStatus_t status)
{
    if (handle->io_status == A_STATUS_OK) handle->io_status = status;
}

/* TCP 上游先收完整 ADU，再按功能码读取缓存。额外检查 PDU 长度，
 * 防止短帧误用缓冲区中上一帧的残留内容，且不修改官方源码。
 */
static aBool_t tcp_length_valid(aModbusHandle_t *handle, aBool_t complete)
{
    const uint8_t *frame = handle->core.msg.buf;
    uint16_t length = (uint16_t)((uint16_t)frame[4] << 8U) | frame[5];
    uint8_t function = frame[7];
    if (handle->config.role == AMODBUS_ROLE_SERVER) {
        if (function >= 1U && function <= 6U) return length == 6U;
        if (function == 15U || function == 16U) {
            return complete ? length == 7U + frame[12] : length >= 7U;
        }
    } else {
        if ((function & 0x80U) != 0U) return length == 3U;
        if (function >= 1U && function <= 4U) {
            return complete ? length == 3U + frame[8] : length >= 3U;
        }
        if (function == 5U || function == 6U ||
            function == 15U || function == 16U) return length == 6U;
    }
    return length >= 2U;
}

/* 一次底层 Read 可能只返回一部分；按字节等待限制继续补齐。
 * 每次有进度重置字节限制，但绝不重置整笔事务的总预算。
 */
static int32_t transport_read(uint8_t *data, uint16_t size,
                              int32_t byte_ms, void *context)
{
    aModbusHandle_t *handle = context;
    size_t done = 0U;
    aTimeout_t byte = byte_ms < 0 ? A_TIMEOUT_FOREVER :
                     A_TIMEOUT_MS((uint32_t)byte_ms);
    aTimepoint_t limit = aTimepointCalc(byte, aOSGetUptimeMs());

    if (handle->io_status != A_STATUS_OK) return -1;
    /* 上游部分功能在接收后才验证长度；先保护其固定帧缓冲区。 */
    if (data < handle->core.msg.buf ||
        data > handle->core.msg.buf + sizeof(handle->core.msg.buf) ||
        size > (size_t)(handle->core.msg.buf +
                        sizeof(handle->core.msg.buf) - data)) {
        io_fail(handle, A_STATUS_ERROR);
        return -1;
    }
    while (done < size) {
        aSSize_t count = handle->config.transport.read(
            handle->config.transport.context, data + done, size - done,
            io_timeout(handle, &limit));
        if (count < 0) {
            aErrno_t error = aOSGetErrno();
            if (error == A_EAGAIN || error == A_ETIMEDOUT) break;
            io_fail(handle, A_STATUS_ERROR);
            return -1;
        }
        if ((size_t)count > size - done) {
            io_fail(handle, A_STATUS_ERROR);
            return -1;
        }
        if (count == 0) break;
        done += (size_t)count;
        limit = aTimepointCalc(byte, aOSGetUptimeMs());
    }
    if (done == size &&
        handle->config.transport_type == AMODBUS_TRANSPORT_TCP &&
        ((data == handle->core.msg.buf + 1U && size == 7U &&
          !tcp_length_valid(handle, A_FALSE)) ||
         (data == handle->core.msg.buf + 8U &&
          !tcp_length_valid(handle, A_TRUE)))) {
        io_fail(handle, A_STATUS_ERROR);
        return -1;
    }
    return (int32_t)done;
}

static int32_t transport_write(const uint8_t *data, uint16_t size,
                               int32_t byte_ms, void *context)
{
    aModbusHandle_t *handle = context;
    aModbusTransport_t *transport = &handle->config.transport;
    size_t done = 0U;
    aTimeout_t byte = byte_ms < 0 ? A_TIMEOUT_FOREVER :
                     A_TIMEOUT_MS((uint32_t)byte_ms);
    aTimepoint_t limit = aTimepointCalc(byte, aOSGetUptimeMs());
    aStatus_t status;

    if (handle->io_status != A_STATUS_OK) return -1;
    if (handle->config.role == AMODBUS_ROLE_SERVER) {
        size_t function = handle->config.transport_type ==
                          AMODBUS_TRANSPORT_RTU ? 1U : 7U;
        if (size >= function + 2U && (data[function] & 0x80U) != 0U) {
            handle->exception = data[function + 1U];
        }
    }
    if (transport->prepare_frame != NULL) {
        status = transport->prepare_frame(transport->context,
                                          aModbusRemaining(handle));
        if (status != A_STATUS_OK) {
            io_fail(handle, status);
            return -1;
        }
    }
    limit = aTimepointCalc(byte, aOSGetUptimeMs());
    while (done < size) {
        aSSize_t count = transport->write(transport->context,
            data + done, size - done, io_timeout(handle, &limit));
        if (count < 0) {
            aErrno_t error = aOSGetErrno();
            if (error == A_EAGAIN || error == A_ETIMEDOUT) break;
            io_fail(handle, A_STATUS_ERROR);
            return -1;
        }
        if ((size_t)count > size - done) {
            io_fail(handle, A_STATUS_ERROR);
            return -1;
        }
        if (count == 0) break;
        done += (size_t)count;
        limit = aTimepointCalc(byte, aOSGetUptimeMs());
    }
    if (done == size && transport->wait_transmit_complete != NULL) {
        status = transport->wait_transmit_complete(transport->context,
            aModbusRemaining(handle));
        if (status != A_STATUS_OK) {
            io_fail(handle, status);
            return -1;
        }
    }
    return (int32_t)done;
}

/* 官方 flush 清理的是输入；不能接 aStream 的输出提交操作。 */
static void transport_discard(nmbs_t *core, void *context)
{
    aModbusHandle_t *handle = context;
    aModbusTransport_t *transport = &handle->config.transport;
    (void)core;
    if (transport->discard_input != NULL) {
        aStatus_t status = transport->discard_input(transport->context,
            aModbusRemaining(handle));
        if (status != A_STATUS_OK) io_fail(handle, status);
    }
}

#if AMODBUS_SERVER_ENABLE
static nmbs_error server_status(aModbusHandle_t *handle, aStatus_t status)
{
    nmbs_error exception;
    switch (status) {
    case A_STATUS_OK: return NMBS_ERROR_NONE;
    case A_STATUS_NOT_FOUND:
        exception = NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        break;
    case A_STATUS_INVALID_PARAM:
        exception = NMBS_EXCEPTION_ILLEGAL_DATA_VALUE;
        break;
    case A_STATUS_UNSUPPORTED:
        exception = NMBS_EXCEPTION_ILLEGAL_FUNCTION;
        break;
    default:
        exception = NMBS_EXCEPTION_SERVER_DEVICE_FAILURE;
        break;
    }
    handle->exception = (uint8_t)exception;
    return exception;
}

static nmbs_error server_read(aModbusHandle_t *handle, aModbusArea_t area,
    uint16_t address, uint16_t quantity, void *data, size_t size)
{
    const aModbusAddressReadRequest_t request = {
        .area = area, .address = address, .quantity = quantity,
        .data = data, .size = size, .timeout = aModbusRemaining(handle)
    };
    return server_status(handle, aModbusBusRead(handle, &request));
}

static nmbs_error server_write(aModbusHandle_t *handle, aModbusArea_t area,
    uint16_t address, uint16_t quantity, const void *data, size_t size)
{
    const aModbusAddressWriteRequest_t request = {
        .area = area, .address = address, .quantity = quantity,
        .data = data, .size = size, .timeout = aModbusRemaining(handle)
    };
    return server_status(handle, aModbusBusWrite(handle, &request));
}

static nmbs_error read_coils(uint16_t address, uint16_t quantity,
    nmbs_bitfield data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_COILS, address, quantity,
                       data, NMBS_BITFIELD_BYTES_MAX);
}

static nmbs_error read_discrete(uint16_t address, uint16_t quantity,
    nmbs_bitfield data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_DISCRETE_INPUTS,
                       address, quantity, data, NMBS_BITFIELD_BYTES_MAX);
}

static nmbs_error read_holding(uint16_t address, uint16_t quantity,
    uint16_t *data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_HOLDING_REGISTERS,
                       address, quantity, data, quantity * sizeof(*data));
}

static nmbs_error read_inputs(uint16_t address, uint16_t quantity,
    uint16_t *data, uint8_t unit, void *context)
{
    (void)unit;
    return server_read(context, AMODBUS_AREA_INPUT_REGISTERS,
                       address, quantity, data, quantity * sizeof(*data));
}

static nmbs_error write_coil(uint16_t address, bool value,
    uint8_t unit, void *context)
{
    uint8_t data = value ? 1U : 0U;
    (void)unit;
    return server_write(context, AMODBUS_AREA_COILS, address, 1U,
                        &data, sizeof(data));
}

static nmbs_error write_register(uint16_t address, uint16_t value,
    uint8_t unit, void *context)
{
    (void)unit;
    return server_write(context, AMODBUS_AREA_HOLDING_REGISTERS,
                        address, 1U, &value, sizeof(value));
}

static nmbs_error write_coils(uint16_t address, uint16_t quantity,
    const nmbs_bitfield data, uint8_t unit, void *context)
{
    (void)unit;
    return server_write(context, AMODBUS_AREA_COILS, address, quantity,
                        data, NMBS_BITFIELD_BYTES_MAX);
}

static nmbs_error write_registers(uint16_t address, uint16_t quantity,
    const uint16_t *data, uint8_t unit, void *context)
{
    (void)unit;
    return server_write(context, AMODBUS_AREA_HOLDING_REGISTERS,
                        address, quantity, data, quantity * sizeof(*data));
}
#endif

static aStatus_t config_check(const aModbusConfig_t *config)
{
    if (config == NULL || config->bus == NULL ||
        config->transport.read == NULL || config->transport.write == NULL ||
        !aTimeoutIsValid(config->byte_timeout)) return A_STATUS_INVALID_PARAM;
    if (config->role != AMODBUS_ROLE_CLIENT &&
        config->role != AMODBUS_ROLE_SERVER) return A_STATUS_INVALID_PARAM;
#if !AMODBUS_CLIENT_ENABLE
    if (config->role == AMODBUS_ROLE_CLIENT) return A_STATUS_UNSUPPORTED;
#endif
#if !AMODBUS_SERVER_ENABLE
    if (config->role == AMODBUS_ROLE_SERVER) return A_STATUS_UNSUPPORTED;
#endif
    if (config->transport_type == AMODBUS_TRANSPORT_RTU) {
        if (config->transport.discard_input == NULL ||
            config->transport.prepare_frame == NULL ||
            config->transport.wait_transmit_complete == NULL ||
            (config->role == AMODBUS_ROLE_SERVER &&
             (config->unit_id == 0U || config->unit_id > 247U))) {
            return A_STATUS_INVALID_PARAM;
        }
    } else if (config->transport_type != AMODBUS_TRANSPORT_TCP) {
        return A_STATUS_INVALID_PARAM;
    }
    if (config->role == AMODBUS_ROLE_SERVER && config->range_count == 0U) {
        return A_STATUS_INVALID_PARAM;
    }
    return aModbusBusDefinitionsCheck(config);
}

static aStatus_t instance_init(const aModbusConfig_t *config,
                              aModbusHandle_t *handle, aBool_t dynamic)
{
    nmbs_platform_conf platform;
    nmbs_error error = NMBS_ERROR_INVALID_ARGUMENT;
    memset(handle, 0, sizeof(*handle));
    atomic_init(&handle->active, A_FALSE);
    handle->config = *config;
    handle->dynamic = dynamic;
    nmbs_platform_conf_create(&platform);
    platform.transport = config->transport_type == AMODBUS_TRANSPORT_RTU ?
                         NMBS_TRANSPORT_RTU : NMBS_TRANSPORT_TCP;
    platform.read = transport_read;
    platform.write = transport_write;
    platform.flush = transport_discard;
    platform.arg = handle;
#if AMODBUS_CLIENT_ENABLE
    if (config->role == AMODBUS_ROLE_CLIENT) {
        error = nmbs_client_create(&handle->core, &platform);
    }
#endif
#if AMODBUS_SERVER_ENABLE
    if (config->role == AMODBUS_ROLE_SERVER) {
        nmbs_callbacks callbacks;
        nmbs_callbacks_create(&callbacks);
        callbacks.read_coils = read_coils;
        callbacks.read_discrete_inputs = read_discrete;
        callbacks.read_holding_registers = read_holding;
        callbacks.read_input_registers = read_inputs;
        callbacks.write_single_coil = write_coil;
        callbacks.write_single_register = write_register;
        callbacks.write_multiple_coils = write_coils;
        callbacks.write_multiple_registers = write_registers;
        callbacks.arg = handle;
        error = nmbs_server_create(&handle->core, config->unit_id,
                                   &platform, &callbacks);
    }
#endif
    if (error != NMBS_ERROR_NONE) return A_STATUS_ERROR;
    nmbs_set_byte_timeout(&handle->core, timeout_ms(config->byte_timeout));
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}

#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusInitStatic(const aModbusConfig_t *config,
                          aModbusHandle_t *handle)
{
    aStatus_t status;
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    status = config_check(config);
    if (status != A_STATUS_OK) return status;
    return instance_init(config, handle, A_FALSE);
}

aStatus_t aModbusDeInitStatic(aModbusHandle_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (atomic_load(&handle->active)) return A_STATUS_BUSY;
    handle->ready = A_FALSE;
    return A_STATUS_OK;
}
#endif

#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusCreate(const aModbusConfig_t *config,
                      aModbusHandle_t **handle_out)
{
    aModbusHandle_t *handle;
    aStatus_t status;
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    status = config_check(config);
    if (status != A_STATUS_OK) return status;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = instance_init(config, handle, A_TRUE);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aModbusDestroy(aModbusHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    if (atomic_load(&handle->active)) return A_STATUS_BUSY;
    handle->ready = A_FALSE;
    aOSFree(handle);
    return A_STATUS_OK;
}
#endif

static aStatus_t operation_begin(aModbusHandle_t *handle,
    aModbusRole_t role, aTimeout_t timeout, aModbusResult_t *result)
{
    if (result != NULL) result->exception = 0U;
    if (handle == NULL || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->config.role != role) return A_STATUS_UNSUPPORTED;
    if (atomic_exchange(&handle->active, A_TRUE)) return A_STATUS_BUSY;
    if (handle->fault) {
        atomic_store(&handle->active, A_FALSE);
        return A_STATUS_NOT_READY;
    }
    handle->deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    handle->io_status = A_STATUS_OK;
    handle->exception = 0U;
    nmbs_set_read_timeout(&handle->core, timeout_ms(timeout));
    return A_STATUS_OK;
}

static aStatus_t operation_end(aModbusHandle_t *handle, nmbs_error error,
                              aModbusResult_t *result)
{
    aStatus_t status = A_STATUS_ERROR;
    if (error > 0) handle->exception = (uint8_t)error;
    if (error == NMBS_ERROR_NONE) status = A_STATUS_OK;
    else if (error == NMBS_ERROR_TIMEOUT) {
        status = !handle->deadline.forever &&
                 handle->deadline.duration_ms == 0U ?
                 A_STATUS_BUSY : A_STATUS_TIMEOUT;
    }
    else if (error == NMBS_ERROR_INVALID_ARGUMENT) {
        status = A_STATUS_INVALID_PARAM;
    }
    if (handle->io_status != A_STATUS_OK) status = handle->io_status;
    if (error < 0 || handle->io_status != A_STATUS_OK) {
        if (handle->config.transport_type == AMODBUS_TRANSPORT_TCP) {
            handle->fault = A_TRUE;
        } else {
            handle->recover_input = A_TRUE;
        }
    }
    if (result != NULL) result->exception = handle->exception;
    if (handle->config.transport.finish != NULL) {
        handle->config.transport.finish(handle->config.transport.context);
    }
    atomic_store(&handle->active, A_FALSE);
    return status;
}

#if AMODBUS_SERVER_ENABLE
aStatus_t aModbusServerProcess(aModbusHandle_t *handle,
    const aModbusServerProcessRequest_t *request)
{
    aStatus_t status;
    nmbs_error error;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    status = operation_begin(handle, AMODBUS_ROLE_SERVER,
                              request->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    if (handle->recover_input) {
        transport_discard(&handle->core, handle);
        if (handle->io_status != A_STATUS_OK) {
            return operation_end(handle, NMBS_ERROR_TRANSPORT,
                                  request->result);
        }
        handle->recover_input = A_FALSE;
    }
    error = nmbs_server_poll(&handle->core);
    return operation_end(handle, error, request->result);
}
#endif

#if AMODBUS_CLIENT_ENABLE
static aStatus_t destination_check(aModbusHandle_t *handle,
                                  uint8_t unit, aBool_t write)
{
    if (handle->config.transport_type == AMODBUS_TRANSPORT_RTU &&
        (unit > 247U || (!write && unit == 0U))) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}

static nmbs_error client_read(aModbusHandle_t *handle, uint8_t unit,
    aModbusArea_t area, uint16_t address, uint16_t quantity, void *data)
{
    nmbs_set_destination_rtu_address(&handle->core, unit);
    switch (area) {
    case AMODBUS_AREA_COILS:
        return nmbs_read_coils(&handle->core, address, quantity, data);
    case AMODBUS_AREA_DISCRETE_INPUTS:
        return nmbs_read_discrete_inputs(&handle->core,
                                          address, quantity, data);
    case AMODBUS_AREA_HOLDING_REGISTERS:
        return nmbs_read_holding_registers(&handle->core,
                                            address, quantity, data);
    case AMODBUS_AREA_INPUT_REGISTERS:
        return nmbs_read_input_registers(&handle->core,
                                         address, quantity, data);
    default: return NMBS_ERROR_INVALID_ARGUMENT;
    }
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
    if (request->result != NULL) request->result->exception = 0U;
    access = &request->access;
    status = aModbusAccessCheck(access->area, access->address,
        access->quantity, access->data, access->size, A_FALSE);
    if (status != A_STATUS_OK) return status;
    status = operation_begin(handle, AMODBUS_ROLE_CLIENT,
                              access->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    status = destination_check(handle, request->unit_id, A_FALSE);
    if (status != A_STATUS_OK) {
        atomic_store(&handle->active, A_FALSE);
        return status;
    }
    /* 官方位读取会清零完整 nmbs_bitfield，不能直接传应用的小缓冲区。 */
    error = client_read(handle, request->unit_id, access->area,
        access->address, access->quantity,
        aModbusAreaIsBits(access->area) ? (void *)bits : access->data);
    if (error == NMBS_ERROR_NONE && aModbusAreaIsBits(access->area)) {
        memcpy(access->data, bits, (access->quantity + 7U) / 8U);
    }
    return operation_end(handle, error, request->result);
}

aStatus_t aModbusClientWrite(aModbusHandle_t *handle,
    const aModbusClientWriteRequest_t *request)
{
    const aModbusAddressWriteRequest_t *access;
    aStatus_t status;
    nmbs_error error;
    if (request == NULL) return A_STATUS_INVALID_PARAM;
    if (request->result != NULL) request->result->exception = 0U;
    access = &request->access;
    status = aModbusAccessCheck(access->area, access->address,
        access->quantity, access->data, access->size, A_TRUE);
    if (status != A_STATUS_OK) return status;
    status = operation_begin(handle, AMODBUS_ROLE_CLIENT,
                              access->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    status = destination_check(handle, request->unit_id, A_TRUE);
    if (status != A_STATUS_OK) {
        atomic_store(&handle->active, A_FALSE);
        return status;
    }
    error = client_write(handle, request->unit_id, access->area,
        access->address, access->quantity, access->data);
    return operation_end(handle, error, request->result);
}

static aStatus_t client_sig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request, aBool_t write)
{
    aModbusValueInfo_t info;
    uint16_t registers[AMODBUS_MAX_REGISTERS];
    nmbs_bitfield bits;
    union {
        max_align_t alignment;
        uint8_t bytes[AMODBUS_MAX_VALUE_SIZE];
    } value;
    void *protocol;
    aStatus_t status;
    aStatus_t finished;
    nmbs_error error = NMBS_ERROR_NONE;

    if (request == NULL) return A_STATUS_INVALID_PARAM;
    status = operation_begin(handle, AMODBUS_ROLE_CLIENT,
                              request->timeout, request->result);
    if (status != A_STATUS_OK) return status;
    status = destination_check(handle, request->unit_id, write);
    if (status == A_STATUS_OK) {
        status = aModbusBusTargetInfo(handle, &request->target,
            request->area, request->word_order, &info);
    }
    protocol = aModbusAreaIsBits(request->area) ? (void *)bits : registers;
    if (status == A_STATUS_OK) {
        status = aModbusAccessCheck(request->area, request->address,
            info.quantity, protocol, sizeof(registers), write);
    }
    if (status == A_STATUS_OK && write) {
        status = aModbusBusReadValue(handle, &request->target,
                                     value.bytes, info.size);
        if (status == A_STATUS_OK) {
            status = aModbusValueEncode(&info, request->area,
                request->word_order, value.bytes, protocol);
        }
    }
    if (status == A_STATUS_OK) {
        error = write ? client_write(handle, request->unit_id,
            request->area, request->address, info.quantity, protocol) :
            client_read(handle, request->unit_id, request->area,
                request->address, info.quantity, protocol);
        if (error == NMBS_ERROR_NONE && !write) {
            status = aModbusValueDecode(&info, request->area,
                request->word_order, protocol, value.bytes);
            if (status == A_STATUS_OK) {
                status = aModbusBusWriteValue(handle, &request->target,
                                              value.bytes, info.size);
            }
        }
    }
    finished = operation_end(handle, error, request->result);
    return finished != A_STATUS_OK ? finished : status;
}

aStatus_t aModbusClientReadSig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request)
{
    return client_sig(handle, request, A_FALSE);
}

aStatus_t aModbusClientWriteSig(aModbusHandle_t *handle,
    const aModbusClientSigRequest_t *request)
{
    return client_sig(handle, request, A_TRUE);
}
#endif
