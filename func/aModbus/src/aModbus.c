#include "aModbus_internal.h"

#include <limits.h>
#include <string.h>

/* 动态参数通过函数校验；计算与静态配置宏共用，外部通道执行等待。 */
aStatus_t aModbusRtuTimingDefaults(uint32_t baud_rate, uint8_t character_bits,
    aModbusRtuTiming_t *timing)
{
    if (timing == NULL || baud_rate == 0U ||
        character_bits < 7U || character_bits > 16U) {
        return A_STATUS_INVALID_PARAM;
    }
    timing->character_us = AMODBUS_RTU_CHARACTER_US(baud_rate, character_bits);
    timing->byte_gap_us = AMODBUS_RTU_BYTE_GAP_US(baud_rate, character_bits);
    timing->frame_gap_us = AMODBUS_RTU_FRAME_GAP_US(baud_rate, character_bits);
    return A_STATUS_OK;
}

aStatus_t aModbusRtuFrameCheck(void *context, const void *data, size_t size)
{
    const aModbusRtuFilter_t *filter = context;
    const uint8_t *frame = data;
    uint8_t function;
    if (filter == NULL || data == NULL ||
        (filter->role != AMODBUS_ROLE_CLIENT &&
         filter->role != AMODBUS_ROLE_SERVER)) return A_STATUS_INVALID_PARAM;
#if !AMODBUS_CLIENT_ENABLE
    if (filter->role == AMODBUS_ROLE_CLIENT) return A_STATUS_UNSUPPORTED;
#endif
#if !AMODBUS_SERVER_ENABLE
    if (filter->role == AMODBUS_ROLE_SERVER) return A_STATUS_UNSUPPORTED;
#endif
    if (filter->role == AMODBUS_ROLE_SERVER &&
        (filter->unit_id == 0U || filter->unit_id > 247U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (size == 0U) return A_STATUS_ERROR;
    if (filter->role == AMODBUS_ROLE_SERVER && frame[0] != 0U &&
        frame[0] != filter->unit_id) return A_STATUS_NOT_FOUND;
    if (size < 4U || size > AMODBUS_RTU_MAX_FRAME_SIZE) return A_STATUS_ERROR;
    function = frame[1];
    if (filter->role == AMODBUS_ROLE_CLIENT) {
        if ((function & 0x80U) != 0U) {
            return size == 5U ? A_STATUS_OK : A_STATUS_ERROR;
        }
        if (function >= 1U && function <= 4U) {
            return size == 5U + frame[2] ? A_STATUS_OK : A_STATUS_ERROR;
        }
        if (function == 5U || function == 6U ||
            function == 15U || function == 16U) {
            return size == 8U ? A_STATUS_OK : A_STATUS_ERROR;
        }
    } else {
        if (function >= 1U && function <= 6U) {
            return size == 8U ? A_STATUS_OK : A_STATUS_ERROR;
        }
        if (function == 15U || function == 16U) {
            return size >= 9U && size == 9U + frame[6] ?
                A_STATUS_OK : A_STATUS_ERROR;
        }
    }
    return A_STATUS_OK;
}

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
            .transport_type = AMODBUS_TRANSPORT_RTU,
            .byte_timeout = A_TIMEOUT_MS(20U)
        };
        *config = defaults;
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

aTimeout_t aModbusRemaining(aModbusHandle_t *handle)
{
    return aTimepointRemaining(&handle->deadline, aOSGetUptimeMs());
}

int32_t aModbusTimeoutMs(aTimeout_t timeout)
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
    if (handle->role == AMODBUS_ROLE_SERVER) {
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
int32_t aModbusTransportRead(uint8_t *data, uint16_t size,
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
        aSSize_t count = handle->config->transport.stream.read(
            handle->config->transport.stream.context, data + done, size - done,
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
        if (count == 0) {
            io_fail(handle, A_STATUS_EOF);
            return -1;
        }
        done += (size_t)count;
        limit = aTimepointCalc(byte, aOSGetUptimeMs());
    }
    if (done == size &&
        handle->config->transport_type == AMODBUS_TRANSPORT_TCP &&
        ((data == handle->core.msg.buf + 1U && size == 7U &&
          !tcp_length_valid(handle, A_FALSE)) ||
         (data == handle->core.msg.buf + 8U &&
          !tcp_length_valid(handle, A_TRUE)))) {
        io_fail(handle, A_STATUS_ERROR);
        return -1;
    }
    return (int32_t)done;
}

int32_t aModbusTransportWrite(const uint8_t *data, uint16_t size,
                               int32_t byte_ms, void *context)
{
    aModbusHandle_t *handle = context;
    aModbusTransport_t *transport = &handle->config->transport;
    size_t done = 0U;
    aTimeout_t byte = byte_ms < 0 ? A_TIMEOUT_FOREVER :
                     A_TIMEOUT_MS((uint32_t)byte_ms);
    aTimepoint_t limit = aTimepointCalc(byte, aOSGetUptimeMs());
    aStatus_t status;

    if (handle->io_status != A_STATUS_OK) return -1;
    if (handle->role == AMODBUS_ROLE_SERVER) {
        size_t function = handle->config->transport_type ==
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
    /* 缓冲流写入失败后可能残留半帧；只有完整提交成功才允许继续复用。 */
    if (transport->stream.flush != NULL) handle->fault = A_TRUE;
    while (done < size) {
        aSSize_t count = transport->stream.write(
            transport->stream.context, data + done, size - done,
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
        if (count == 0) {
            io_fail(handle, A_STATUS_ERROR);
            return -1;
        }
        done += (size_t)count;
        limit = aTimepointCalc(byte, aOSGetUptimeMs());
    }
    if (done == size && transport->stream.flush != NULL) {
        status = transport->stream.flush(transport->stream.context,
                                         aModbusRemaining(handle));
        if (status != A_STATUS_OK) {
            io_fail(handle, status);
            return -1;
        }
        handle->fault = A_FALSE;
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
void aModbusTransportDiscard(nmbs_t *core, void *context)
{
    aModbusHandle_t *handle = context;
    aModbusTransport_t *transport = &handle->config->transport;
    (void)core;
    if (transport->discard_input != NULL) {
        aStatus_t status = transport->discard_input(transport->context,
            aModbusRemaining(handle));
        if (status != A_STATUS_OK) io_fail(handle, status);
    }
}

aStatus_t aModbusOperationBegin(aModbusHandle_t *handle,
    aModbusRole_t role, aTimeout_t timeout, aModbusResult_t *result)
{
    if (result != NULL) *result = (aModbusResult_t){0};
    if (handle == NULL || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (handle->role != role) return A_STATUS_UNSUPPORTED;
    if (aAtomicExchange(&handle->active, A_TRUE)) return A_STATUS_BUSY;
    if (handle->fault) {
        aAtomicStore(&handle->active, A_FALSE);
        return A_STATUS_NOT_READY;
    }
    if (result != NULL) result->started = A_TRUE;
    handle->deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    handle->io_status = A_STATUS_OK;
    handle->exception = 0U;
    nmbs_set_read_timeout(&handle->core, aModbusTimeoutMs(timeout));
    return A_STATUS_OK;
}

aStatus_t aModbusOperationEnd(aModbusHandle_t *handle, nmbs_error error,
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
        if (status == A_STATUS_EOF ||
            handle->config->transport_type == AMODBUS_TRANSPORT_TCP) {
            handle->fault = A_TRUE;
        } else {
            handle->recover_input = A_TRUE;
        }
    }
    if (result != NULL) {
        result->exception = handle->exception;
        result->retryable = handle->role == AMODBUS_ROLE_CLIENT &&
            !handle->fault &&
            handle->config->transport_type == AMODBUS_TRANSPORT_RTU &&
            handle->io_status == A_STATUS_OK &&
            (error == NMBS_ERROR_TIMEOUT || error == NMBS_ERROR_CRC ||
             error == NMBS_ERROR_INVALID_RESPONSE ||
             error == NMBS_ERROR_INVALID_UNIT_ID);
    }
    if (handle->config->transport.finish != NULL) {
        handle->config->transport.finish(handle->config->transport.context);
    }
    aAtomicStore(&handle->active, A_FALSE);
    return status;
}


/* 公共校验只处理传输和数据访问，角色专属规则由各自入口检查。 */
aStatus_t aModbusConfigCheck(const aModbusConfig_t *config)
{
    if (config == NULL ||
        config->transport.stream.read == NULL ||
        config->transport.stream.write == NULL ||
        !aTimeoutIsValid(config->byte_timeout)) return A_STATUS_INVALID_PARAM;
    if (config->transport_type == AMODBUS_TRANSPORT_RTU) {
        if (config->transport.discard_input == NULL ||
            config->transport.prepare_frame == NULL ||
            config->transport.wait_transmit_complete == NULL) {
            return A_STATUS_INVALID_PARAM;
        }
    } else if (config->transport_type != AMODBUS_TRANSPORT_TCP) {
        return A_STATUS_INVALID_PARAM;
    }
    return A_STATUS_OK;
}

aStatus_t aModbusStoragePrepare(aModbusHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->ready) return A_STATUS_BUSY;
    memset(handle, 0, sizeof(*handle));
    aAtomicInit(&handle->active, A_FALSE);
    return A_STATUS_OK;
}

aStatus_t aModbusGetInfo(const aModbusHandle_t *handle, aModbusInfo_t *info)
{
    if (handle == NULL || info == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    *info = (aModbusInfo_t){handle->role, handle->config->transport_type};
    return A_STATUS_OK;
}

aStatus_t aModbusCoreInit(aModbusHandle_t *handle, aModbusRole_t role,
    uint8_t unit_id)
{
    nmbs_platform_conf platform;
    nmbs_error error = NMBS_ERROR_INVALID_ARGUMENT;
    aModbusEndpointInit(handle);
    handle->role = role;
    handle->config = role == AMODBUS_ROLE_CLIENT ?
        &handle->client.modbus : &handle->server.modbus;
    aModbusConfig_t *config = handle->config;
    nmbs_platform_conf_create(&platform);
    platform.transport = config->transport_type == AMODBUS_TRANSPORT_RTU ?
                         NMBS_TRANSPORT_RTU : NMBS_TRANSPORT_TCP;
    platform.read = aModbusTransportRead;
    platform.write = aModbusTransportWrite;
    platform.flush = aModbusTransportDiscard;
    platform.arg = handle;
#if AMODBUS_CLIENT_ENABLE
    if (role == AMODBUS_ROLE_CLIENT) {
        error = nmbs_client_create(&handle->core, &platform);
    }
#endif
#if AMODBUS_SERVER_ENABLE
    if (role == AMODBUS_ROLE_SERVER) {
        nmbs_callbacks callbacks;
        aModbusServerCallbacks(handle, &callbacks);
        error = nmbs_server_create(&handle->core, unit_id,
            &platform, &callbacks);
    }
#else
    (void)unit_id;
#endif
    if (error != NMBS_ERROR_NONE) return A_STATUS_ERROR;
    nmbs_set_byte_timeout(&handle->core,
        aModbusTimeoutMs(config->byte_timeout));
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aModbusCoreDeInit(aModbusHandle_t *handle)
{
    if (!handle->ready) return A_STATUS_NOT_READY;
    if (aAtomicLoad(&handle->active)) return A_STATUS_BUSY;
    handle->ready = A_FALSE;
    return A_STATUS_OK;
}

#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusDeInitStatic(aModbusHandle_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    return aModbusCoreDeInit(handle);
}
#endif

#if AMODBUS_DYNAMIC_ENABLE
aModbusHandle_t *aModbusAllocate(void)
{
    aModbusHandle_t *handle = aOSAlloc(sizeof(*handle));
    if (handle != NULL) memset(handle, 0, sizeof(*handle));
    return handle;
}

aStatus_t aModbusDestroy(aModbusHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    aStatus_t status = aModbusCoreDeInit(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

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
