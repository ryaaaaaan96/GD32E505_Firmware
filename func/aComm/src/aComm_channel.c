#include "aComm_instance.h"
#include "aOS.h"
#include <string.h>

void aCommChannelConfigStructInit(aCommChannelConfig_t *config)
{
    if (config != NULL) {
        const aCommChannelConfig_t defaults = {
            .mode = ACOMM_CHANNEL_STREAM
        };
        *config = defaults;
    }
}

/* 除法只发生在初始化；收字节时只比较计数差。 */
static uint64_t divide_up(uint64_t value, uint64_t divisor)
{
    return (value + divisor - 1U) / divisor;
}

static aStatus_t instance_init(const aCommChannelConfig_t *config,
                              aCommChannel_t *handle)
{
    uint64_t character, byte_gap, frame_gap, frequency;
    if (config == NULL || handle == NULL || config->stream.write == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (config->mode == ACOMM_CHANNEL_STREAM) {
        if (config->stream.read == NULL || config->frame_check != NULL) {
            return A_STATUS_INVALID_PARAM;
        }
        memset(handle, 0, sizeof(*handle));
        handle->config = *config;
        handle->ready = A_TRUE;
        return A_STATUS_OK;
    }
    if (config->mode != ACOMM_CHANNEL_GAP ||
        config->io.ticks_per_second == 0U || config->io.ticks == NULL ||
        config->io.enter == NULL || config->io.exit == NULL ||
        config->io.wait_transmit_complete == NULL ||
        config->timing.frame_gap_us == 0U ||
        config->timing.byte_gap_us > config->timing.frame_gap_us) {
        return A_STATUS_INVALID_PARAM;
    }
    frequency = config->io.ticks_per_second;
    character = divide_up(frequency * config->timing.character_us, 1000000U);
    byte_gap = divide_up(frequency * config->timing.byte_gap_us, 1000000U);
    frame_gap = divide_up(frequency * config->timing.frame_gap_us, 1000000U);
    if (frame_gap + character > UINT32_MAX / 2U) {
        return A_STATUS_UNSUPPORTED;
    }
    uint64_t idle_ms = divide_up((frame_gap + character) * 1000U,
                                 frequency) + 1U;
    if (idle_ms > INT32_MAX) return A_STATUS_UNSUPPORTED;
    aCommChannelStorage_t *storage = handle->storage;
    if (storage == NULL) return A_STATUS_INVALID_PARAM;
    memset(handle, 0, sizeof(*handle));
    handle->storage = storage;
    handle->config = *config;
    handle->character_ticks = (uint32_t)character;
    handle->byte_gap = (uint32_t)byte_gap;
    handle->frame_gap = (uint32_t)frame_gap;
    /* 毫秒时基只判断长空闲，额外一毫秒覆盖量化误差。 */
    handle->idle_ms = (uint32_t)idle_ms;
    handle->last_rx_ticks = handle->last_tx_ticks =
        config->io.ticks(config->io.context);
    handle->last_rx_ms = handle->last_tx_ms = aOSGetUptimeMs();
    handle->ready = A_TRUE;
    return A_STATUS_OK;
}

#if ACOMM_STATIC_ENABLE
aStatus_t aCommChannelInitStatic(const aCommChannelConfig_t *config,
                             aCommChannel_t *handle)
{
    return instance_init(config, handle);
}

aStatus_t aCommChannelDeInitStatic(aCommChannel_t *handle)
{
    if (handle == NULL || handle->dynamic) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    handle->ready = A_FALSE;
    return A_STATUS_OK;
}
#endif

#if ACOMM_DYNAMIC_ENABLE
aStatus_t aCommChannelCreate(const aCommChannelConfig_t *config,
                         aCommChannel_t **handle_out)
{
    aCommChannel_t *handle;
    aStatus_t status;
    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    const size_t size = sizeof(*handle) +
        (config->mode == ACOMM_CHANNEL_GAP ? sizeof(*handle->storage) : 0U);
    handle = aOSAlloc(size);
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    *handle = (aCommChannel_t){
        .storage = config->mode == ACOMM_CHANNEL_GAP ?
            (void *)(handle + 1) : NULL
    };
    status = instance_init(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    handle->dynamic = A_TRUE;
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aCommChannelDestroy(aCommChannel_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    aOSFree(handle);
    return A_STATUS_OK;
}
#endif

static aBool_t elapsed(aCommChannel_t *handle, uint32_t ticks,
                       uint32_t ms, uint32_t limit)
{
    if ((uint32_t)(aOSGetUptimeMs() - ms) > handle->idle_ms) return A_TRUE;
    return (uint32_t)(handle->config.io.ticks(handle->config.io.context) -
                       ticks) >= limit;
}

/* 生产者或已排除生产者的消费任务调用；满队列丢弃新帧。 */
static void finish_frame(aCommChannel_t *handle)
{
    if (handle->building != 0U && !handle->invalid_frame) {
        handle->storage->frames[handle->head].size = handle->building;
        handle->head = (handle->head + 1U) % ACOMM_CHANNEL_FRAME_COUNT;
        handle->count++;
    } else if (handle->invalid_frame) {
        handle->dropped_frames++;
    }
    handle->building = 0U;
    handle->invalid_frame = A_FALSE;
}

void aCommChannelReceiveAt(aCommChannel_t *handle, uint8_t byte,
                       aStatus_t status, uint32_t rx_ticks, uint32_t rx_ms)
{
    uint32_t delta;
    if (handle == NULL || !handle->ready ||
        handle->config.mode != ACOMM_CHANNEL_GAP) return;
    delta = rx_ticks - handle->last_rx_ticks;
    /* 时间戳在字符完成时采集，比较间隔时须补上一个字符时间。 */
    if ((uint32_t)(rx_ms - handle->last_rx_ms) > handle->idle_ms ||
        delta >= handle->frame_gap + handle->character_ticks) {
        finish_frame(handle);
    } else if (handle->building != 0U && handle->byte_gap != 0U &&
               delta >= handle->byte_gap + handle->character_ticks) {
        handle->invalid_frame = A_TRUE;
    }
    handle->last_rx_ticks = rx_ticks;
    handle->last_rx_ms = rx_ms;
    if (status != A_STATUS_OK || handle->count == ACOMM_CHANNEL_FRAME_COUNT ||
        handle->building == ACOMM_CHANNEL_FRAME_SIZE) {
        handle->invalid_frame = A_TRUE;
    }
    if (!handle->invalid_frame) {
        handle->storage->frames[handle->head].data[handle->building++] = byte;
    }
}

void aCommChannelReceive(void *context, uint8_t byte, aStatus_t status)
{
    aCommChannel_t *handle = context;
    uint32_t rx_ticks, rx_ms;
    if (handle == NULL || !handle->ready ||
        handle->config.mode != ACOMM_CHANNEL_GAP) return;
    rx_ticks = handle->config.io.ticks(handle->config.io.context);
    rx_ms = aOSGetUptimeMs();
    aCommChannelReceiveAt(handle, byte, status, rx_ticks, rx_ms);
}

static void port_finish(void *context)
{
    aCommChannel_t *handle = context;
    handle->frame_size = handle->frame_position = 0U;
    handle->frame_ready = A_FALSE;
}

static aStatus_t receive_frame(aCommChannel_t *handle, aTimeout_t timeout)
{
    aTimepoint_t deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    const aCommChannelIo_t *io = &handle->config.io;
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
            handle->frame_size = handle->storage->frames[tail].size;
            memcpy(handle->storage->frame, handle->storage->frames[tail].data,
                   handle->frame_size);
            io->enter(io->context);
            handle->tail = (tail + 1U) % ACOMM_CHANNEL_FRAME_COUNT;
            handle->count--;
            io->exit(io->context);
            aStatus_t status = A_STATUS_OK;
            if (handle->config.frame_check != NULL) {
                status = handle->config.frame_check(
                    handle->config.frame_context, handle->storage->frame,
                    handle->frame_size);
            }
            if (status == A_STATUS_OK) {
                handle->frame_ready = A_TRUE;
                return A_STATUS_OK;
            }
            if (status != A_STATUS_NOT_FOUND) return status;
        }
        if (aTimepointExpired(&deadline, aOSGetUptimeMs())) {
            return A_STATUS_TIMEOUT;
        }
        if (available == 0U) aOSDelayMs(1U);
    }
}

static aSSize_t stream_read(void *context, void *data, size_t size,
                           aTimeout_t timeout)
{
    aCommChannel_t *handle = context;
    aStatus_t status;
    size_t available;
    if (handle == NULL || (size != 0U && data == NULL) ||
        !aTimeoutIsValid(timeout)) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->ready) return aOSFailWithStatus(A_STATUS_NOT_READY);
    if (size == 0U) return 0;
    if (handle->config.mode == ACOMM_CHANNEL_STREAM) {
        const aStream_t *stream = &handle->config.stream;
        return stream->read(stream->context, data, size, timeout);
    }
    if (!handle->frame_ready) {
        status = receive_frame(handle, timeout);
        if (status == A_STATUS_TIMEOUT) return aOSFailWithTimeout(timeout);
        if (status != A_STATUS_OK) return aOSFailWithStatus(status);
    }
    available = handle->frame_size - handle->frame_position;
    /* 不能越过帧边界补数据；继续索取已耗尽帧是报文不完整，不是流 EOF。 */
    if (available == 0U) return aOSFailWithStatus(A_STATUS_ERROR);
    if (size > available) size = available;
    memcpy(data, handle->storage->frame + handle->frame_position, size);
    handle->frame_position += size;
    return (aSSize_t)size;
}

static aSSize_t stream_write(void *context, const void *data, size_t size,
                            aTimeout_t timeout)
{
    aCommChannel_t *handle = context;
    const aCommChannelIo_t *io;
    aSSize_t count;

    if (handle == NULL || (size != 0U && data == NULL) ||
        !aTimeoutIsValid(timeout)) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->ready) return aOSFailWithStatus(A_STATUS_NOT_READY);
    if (size == 0U) return 0;
    io = &handle->config.io;
    count = handle->config.stream.write(
        handle->config.stream.context, data, size, timeout);
    if (count == 0) return aOSFailWithStatus(A_STATUS_ERROR);
    if (count > 0 && handle->config.mode == ACOMM_CHANNEL_GAP) {
        handle->tx_pending = A_TRUE;
        handle->last_tx_ticks = io->ticks(io->context);
        handle->last_tx_ms = aOSGetUptimeMs();
    }
    return count;
}

static aStatus_t port_wait(void *context, aTimeout_t timeout)
{
    aCommChannel_t *handle = context;
    const aCommChannelIo_t *io = &handle->config.io;
    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    aStatus_t status = io->wait_transmit_complete != NULL ?
        io->wait_transmit_complete(io->context, timeout) : A_STATUS_OK;
    if (status == A_STATUS_OK && handle->tx_pending) {
        handle->last_tx_ticks = io->ticks(io->context);
        handle->last_tx_ms = aOSGetUptimeMs();
        handle->tx_pending = A_FALSE;
    }
    return status;
}

static aStatus_t port_prepare(void *context, aTimeout_t timeout)
{
    aCommChannel_t *handle = context;
    const aCommChannelIo_t *io = &handle->config.io;
    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    if (handle->config.mode == ACOMM_CHANNEL_STREAM) return A_STATUS_OK;
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
    aCommChannel_t *handle = context;
    const aCommChannelIo_t *io = &handle->config.io;
    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    if (handle->config.mode == ACOMM_CHANNEL_STREAM) {
        return io->discard_input != NULL ?
            io->discard_input(io->context, timeout) : A_STATUS_UNSUPPORTED;
    }
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

static aStatus_t stream_flush(void *context, aTimeout_t timeout)
{
    const aCommChannel_t *handle = context;
    const aStream_t *output = &handle->config.stream;
    return output->flush(output->context, timeout);
}

aStatus_t aCommChannelBindTransport(aCommChannel_t *handle,
    aCommTransport_t *transport)
{
    if (handle == NULL || transport == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->ready) return A_STATUS_NOT_READY;
    const aBool_t framed = handle->config.mode == ACOMM_CHANNEL_GAP;
    transport->stream.context = handle;
    transport->stream.read = stream_read;
    transport->stream.write = stream_write;
    transport->stream.flush = handle->config.stream.flush != NULL ?
                              stream_flush : NULL;
    transport->context = handle;
    /* 缺少能力用 NULL 表示，不能让流协议误执行输入清理或帧间等待。 */
    transport->discard_input = framed ||
        handle->config.io.discard_input != NULL ? port_discard : NULL;
    transport->prepare_frame = framed ? port_prepare : NULL;
    transport->wait_transmit_complete =
        handle->config.io.wait_transmit_complete != NULL ? port_wait : NULL;
    transport->finish = framed ? port_finish : NULL;
    return A_STATUS_OK;
}
