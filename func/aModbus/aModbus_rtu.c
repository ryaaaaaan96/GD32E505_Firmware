#include "aModbus_rtu_internal.h"
#include "aOS.h"
#include <string.h>

void aModbusRtuConfigStructInit(aModbusRtuConfig_t *config)
{
    if (config != NULL) {
        const aModbusRtuConfig_t defaults = {
            .role = AMODBUS_SERVER_ENABLE ? AMODBUS_ROLE_SERVER :
                                           AMODBUS_ROLE_CLIENT,
            .unit_id = 1U,
            .character_bits = 10U,
            .baud_rate = 115200U
        };
        *config = defaults;
    }
}

/* 除法只发生在初始化；收字节时只比较计数差。 */
static uint64_t divide_up(uint64_t value, uint64_t divisor)
{
    return (value + divisor - 1U) / divisor;
}

aStatus_t aModbusRtuInstanceInit(const aModbusRtuConfig_t *config,
                               aModbusRtuHandle_t *handle)
{
    uint64_t character, byte_gap, frame_gap, frequency;
    if (config == NULL || handle == NULL || config->baud_rate == 0U ||
        config->character_bits < 7U || config->character_bits > 16U ||
        config->io.ticks_per_second == 0U || config->io.ticks == NULL ||
        config->io.enter == NULL || config->io.exit == NULL ||
        config->io.write == NULL ||
        config->io.wait_transmit_complete == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (config->role != AMODBUS_ROLE_CLIENT &&
        config->role != AMODBUS_ROLE_SERVER) return A_STATUS_INVALID_PARAM;
    if (config->role == AMODBUS_ROLE_SERVER &&
        (config->unit_id == 0U || config->unit_id > 247U)) {
        return A_STATUS_INVALID_PARAM;
    }
#if !AMODBUS_CLIENT_ENABLE
    if (config->role == AMODBUS_ROLE_CLIENT) return A_STATUS_UNSUPPORTED;
#endif
#if !AMODBUS_SERVER_ENABLE
    if (config->role == AMODBUS_ROLE_SERVER) return A_STATUS_UNSUPPORTED;
#endif
    frequency = config->io.ticks_per_second;
    character = divide_up(frequency * config->character_bits,
                          config->baud_rate);
    if (config->baud_rate > 19200U) {
        byte_gap = divide_up(frequency * 750U, 1000000U);
        frame_gap = divide_up(frequency * 1750U, 1000000U);
    } else {
        byte_gap = divide_up(frequency * config->character_bits * 3U,
                             (uint64_t)config->baud_rate * 2U);
        frame_gap = divide_up(frequency * config->character_bits * 7U,
                              (uint64_t)config->baud_rate * 2U);
    }
    if (frame_gap + character > UINT32_MAX / 2U) {
        return A_STATUS_UNSUPPORTED;
    }
    memset(handle, 0, sizeof(*handle));
    handle->config = *config;
    handle->character_ticks = (uint32_t)character;
    handle->byte_gap = (uint32_t)byte_gap;
    handle->frame_gap = (uint32_t)frame_gap;
    /* 毫秒时基只判断长空闲，额外一毫秒覆盖量化误差。 */
    handle->idle_ms = (uint32_t)divide_up(
        (frame_gap + character) * 1000U, frequency) + 1U;
    handle->last_rx_ticks = handle->last_tx_ticks =
        config->io.ticks(config->io.context);
    handle->last_rx_ms = handle->last_tx_ms = aOSGetUptimeMs();
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}

#if AMODBUS_STATIC_ENABLE
aStatus_t aModbusRtuInitStatic(const aModbusRtuConfig_t *config,
                             aModbusRtuHandle_t *handle)
{
    return aModbusRtuInstanceInit(config, handle);
}

aStatus_t aModbusRtuDeInitStatic(aModbusRtuHandle_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    handle->ready = A_FALSE;
    return A_STATUS_OK;
}
#endif

#if AMODBUS_DYNAMIC_ENABLE
aStatus_t aModbusRtuCreate(const aModbusRtuConfig_t *config,
                         aModbusRtuHandle_t **handle_out)
{
    aModbusRtuHandle_t *handle;
    aStatus_t status;
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = aModbusRtuInstanceInit(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    handle->dynamic = A_TRUE;
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aModbusRtuDestroy(aModbusRtuHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    aOSFree(handle);
    return A_STATUS_OK;
}
#endif

static aBool_t elapsed(aModbusRtuHandle_t *handle, uint32_t ticks,
                       uint32_t ms, uint32_t limit)
{
    if ((uint32_t)(aOSGetUptimeMs() - ms) > handle->idle_ms) return A_TRUE;
    return (uint32_t)(handle->config.io.ticks(handle->config.io.context) -
                       ticks) >= limit;
}

/* 生产者或已排除生产者的消费任务调用；满队列丢弃新帧。 */
static void finish_frame(aModbusRtuHandle_t *handle)
{
    if (handle->building != 0U && !handle->invalid_frame) {
        handle->frames[handle->head].size = handle->building;
        handle->head = (handle->head + 1U) % AMODBUS_RTU_FRAME_COUNT;
        handle->count++;
    } else if (handle->invalid_frame) {
        handle->dropped_frames++;
    }
    handle->building = 0U;
    handle->invalid_frame = A_FALSE;
}

void aModbusRtuReceive(void *context, uint8_t byte, aStatus_t status)
{
    aModbusRtuHandle_t *handle = context;
    uint32_t now_ticks, now_ms, delta;
    if (handle == NULL || !handle->ready) return;
    /* 一次采样用于本字节的全部判断，减少 ISR 中的回调及读时钟次数。 */
    now_ticks = handle->config.io.ticks(handle->config.io.context);
    now_ms = aOSGetUptimeMs();
    delta = now_ticks - handle->last_rx_ticks;
    /* 时间戳在字符完成时采集，比较间隔时须补上一个字符时间。 */
    if ((uint32_t)(now_ms - handle->last_rx_ms) > handle->idle_ms ||
        delta >= handle->frame_gap + handle->character_ticks) {
        finish_frame(handle);
    } else if (handle->building != 0U &&
               delta >= handle->byte_gap + handle->character_ticks) {
        handle->invalid_frame = A_TRUE;
    }
    handle->last_rx_ticks = now_ticks;
    handle->last_rx_ms = now_ms;
    if (status != A_STATUS_OK || handle->count == AMODBUS_RTU_FRAME_COUNT ||
        handle->building == AMODBUS_RTU_FRAME_SIZE) {
        handle->invalid_frame = A_TRUE;
    }
    if (!handle->invalid_frame) {
        handle->frames[handle->head].data[handle->building++] = byte;
    }
}

/* 完整 ADU 长度先于业务写入检查；CRC 和协议异常由核心处理。 */
static aBool_t frame_length_valid(aModbusRtuHandle_t *handle)
{
    const uint8_t *frame = handle->frame;
    size_t size = handle->frame_size;
    uint8_t function;
    if (size < 4U) return A_FALSE;
    function = frame[1];
    if (handle->config.role == AMODBUS_ROLE_CLIENT) {
        if ((function & 0x80U) != 0U) return size == 5U;
        if (function >= 1U && function <= 4U) return size == 5U + frame[2];
        if (function == 5U || function == 6U ||
            function == 15U || function == 16U) return size == 8U;
    } else {
        if (function >= 1U && function <= 6U) return size == 8U;
        if (function == 15U || function == 16U) {
            return size >= 9U && size == 9U + frame[6];
        }
    }
    return A_TRUE;
}

static void port_finish(void *context)
{
    aModbusRtuHandle_t *handle = context;
    handle->frame_size = handle->frame_position = 0U;
    handle->frame_ready = A_FALSE;
}

static aStatus_t receive_frame(aModbusRtuHandle_t *handle, aTimeout_t timeout)
{
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    const aModbusRtuIo_t *io = &handle->config.io;
    port_finish(handle);
    for (;;) {
        size_t available, tail;
        io->enter(io->context);
        if (elapsed(handle, handle->last_rx_ticks, handle->last_rx_ms,
                    handle->frame_gap + handle->character_ticks)) {
            finish_frame(handle);
        }
        available = handle->count;
        tail = handle->tail;
        io->exit(io->context);
        if (available != 0U) {
            /* 槽位归还前生产者不会覆盖它；整帧复制不占用临界区。 */
            handle->frame_size = handle->frames[tail].size;
            memcpy(handle->frame, handle->frames[tail].data,
                   handle->frame_size);
            io->enter(io->context);
            handle->tail = (tail + 1U) % AMODBUS_RTU_FRAME_COUNT;
            handle->count--;
            io->exit(io->context);
            if (handle->config.role != AMODBUS_ROLE_SERVER ||
                handle->frame[0] == 0U ||
                handle->frame[0] == handle->config.unit_id) {
                if (!frame_length_valid(handle)) return A_STATUS_ERROR;
                handle->frame_ready = A_TRUE;
                return A_STATUS_OK;
            }
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        if (available == 0U) aOSDelayMs(1U);
    }
}

static aSSize_t port_read(void *context, void *data, size_t size,
                         aTimeout_t timeout)
{
    aModbusRtuHandle_t *handle = context;
    aStatus_t status;
    size_t available;
    if (!handle->ready) return aOSFailWithStatus(A_STATUS_NOT_READY);
    if (size == 0U) return 0;
    if (!handle->frame_ready) {
        status = receive_frame(handle, timeout);
        if (status != A_STATUS_OK) return aOSFailWithStatus(status);
    }
    available = handle->frame_size - handle->frame_position;
    if (size > available) size = available;
    memcpy(data, handle->frame + handle->frame_position, size);
    handle->frame_position += size;
    return (aSSize_t)size;
}

static aSSize_t port_write(void *context, const void *data, size_t size,
                          aTimeout_t timeout)
{
    aModbusRtuHandle_t *handle = context;
    const aModbusRtuIo_t *io = &handle->config.io;
    aSSize_t count = io->write(io->context, data, size, timeout);
    if (count > 0) {
        handle->tx_pending = A_TRUE;
        handle->last_tx_ticks = io->ticks(io->context);
        handle->last_tx_ms = aOSGetUptimeMs();
    }
    return count;
}

static aStatus_t port_wait(void *context, aTimeout_t timeout)
{
    aModbusRtuHandle_t *handle = context;
    const aModbusRtuIo_t *io = &handle->config.io;
    aStatus_t status = io->wait_transmit_complete(io->context, timeout);
    if (status == A_STATUS_OK && handle->tx_pending) {
        handle->last_tx_ticks = io->ticks(io->context);
        handle->last_tx_ms = aOSGetUptimeMs();
        handle->tx_pending = A_FALSE;
    }
    return status;
}

static aStatus_t port_prepare(void *context, aTimeout_t timeout)
{
    aModbusRtuHandle_t *handle = context;
    const aModbusRtuIo_t *io = &handle->config.io;
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    /* 超时后串口可能仍在发送，先等线路完成再检查静默。 */
    aStatus_t status = port_wait(handle, timeout);
    if (status != A_STATUS_OK) return status;
    for (;;) {
        aBool_t quiet;
        io->enter(io->context);
        quiet = elapsed(handle, handle->last_rx_ticks, handle->last_rx_ms,
                        handle->frame_gap);
        io->exit(io->context);
        if (quiet && elapsed(handle, handle->last_tx_ticks, handle->last_tx_ms,
                             handle->frame_gap)) return A_STATUS_OK;
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        aOSDelayMs(1U);
    }
}

static aStatus_t port_discard(void *context, aTimeout_t timeout)
{
    aModbusRtuHandle_t *handle = context;
    const aModbusRtuIo_t *io = &handle->config.io;
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    port_finish(handle);
    for (;;) {
        aBool_t quiet;
        io->enter(io->context);
        handle->head = handle->tail = handle->count = handle->building = 0U;
        quiet = elapsed(handle, handle->last_rx_ticks, handle->last_rx_ms,
                        handle->frame_gap);
        handle->invalid_frame = !quiet;
        io->exit(io->context);
        if (quiet) {
            if (io->clear_error != NULL) io->clear_error(io->context);
            return A_STATUS_OK;
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        aOSDelayMs(1U);
    }
}

aStatus_t aModbusRtuGetTransport(aModbusRtuHandle_t *handle,
                               aModbusTransport_t *transport)
{
    if (handle == NULL || transport == NULL) return A_STATUS_INVALID_PARAM;
    aModbusTransportStructInit(transport);
    if (!handle->ready) return A_STATUS_NOT_READY;
    transport->context = handle;
    transport->read = port_read;
    transport->write = port_write;
    transport->discard_input = port_discard;
    transport->prepare_frame = port_prepare;
    transport->wait_transmit_complete = port_wait;
    transport->finish = port_finish;
    return A_STATUS_OK;
}
