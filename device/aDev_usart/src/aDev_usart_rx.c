#include "aDev_usart_internal.h"

#include <limits.h>
#include <string.h>

#if ADEV_USART_ASYNC_ENABLE
static void async_rx_dispatch(aDevUsartHandle_t *handle);
#endif

#if ADEV_USART_NEEDS_EVENTS
static void rx_error_from_isr(aDevUsartHandle_t *handle)
{
    handle->rx.error = A_STATUS_ERROR;
    if (handle->settings.rx_byte_callback != NULL) {
        handle->settings.rx_byte_callback(handle->settings.rx_byte_context, 0U,
                                  A_STATUS_ERROR);
    }
#if ADEV_USART_ASYNC_ENABLE
    async_rx_dispatch(handle);
#endif
    if (handle->rx.wait_object != NULL)
        aOSWaitObjectNotifyFromISR(handle->rx.wait_object);
}

static void rx_error_callback(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    if (aDrvUsartTakeRxError(&handle->drv_handle) != A_STATUS_OK)
        rx_error_from_isr(handle);
}
#endif

#if ADEV_USART_INTERRUPT_ENABLE
static void rx_ready_callback(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    uint8_t data;

    aStatus_t status = aDrvUsartTryReadByte(&handle->drv_handle, &data);
    if (status != A_STATUS_OK) {
        if (status == A_STATUS_ERROR) rx_error_from_isr(handle);
        return;
    }
    if (handle->settings.rx_byte_callback != NULL) {
        handle->settings.rx_byte_callback(handle->settings.rx_byte_context,
            data, A_STATUS_OK);
        return;
    }
    aOSCriticalState_t key = aOSCriticalEnterFromISR();
    const aBool_t full = handle->rx.count >= handle->settings.rx_buffer_size;
    if (full) {
        handle->rx.overflow = A_TRUE;
    } else {
        handle->settings.rx_buffer[handle->rx.head] = data;
        if (++handle->rx.head == handle->settings.rx_buffer_size)
            handle->rx.head = 0U;
        ++handle->rx.count;
    }
    aOSCriticalExitFromISR(key);
#if ADEV_USART_ASYNC_ENABLE
    async_rx_dispatch(handle);
#endif
    if (!full) aOSWaitObjectNotifyFromISR(handle->rx.wait_object);
}

#endif

#if ADEV_USART_INTERRUPT_ENABLE
static void rx_idle_callback(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->rx.idle_event_count;
#if ADEV_USART_DMA_BACKEND_ENABLE
    if ((handle->settings.mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_DMA_BUFFERED)
        aDevUsartRxDmaNotifyFromISR(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx.wait_object);
}

#endif

#if ADEV_USART_DMA_BACKEND_ENABLE && ADEV_USART_INTERRUPT_ENABLE
static void rx_dma_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->rx.idle_event_count;
    aDevUsartRxDmaNotifyFromISR(handle);
    aOSWaitObjectNotifyFromISR(handle->rx.wait_object);
}

#endif

#if ADEV_USART_INTERRUPT_ENABLE
static aStatus_t rx_idle_detection_enable(aDevUsartHandle_t *handle)
{
    aDrvUsartEventCallback_t callback = rx_idle_callback;

    if (!aDrvUsartEventIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }

    return aDevUsartRegisterDriverCallback(
        handle, ADRV_USART_EVENT_RX_IDLE, callback, A_TRUE);
}

#endif

aStatus_t aDevUsartRxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    aStatus_t status;
    (void)handle;

    switch (config->mode & ADEV_USART_RX_MASK) {
    case ADEV_USART_RX_POLLING:
        status = A_STATUS_OK;
        break;
#if ADEV_USART_INTERRUPT_ENABLE
    case ADEV_USART_RX_INTERRUPT_CALLBACK:
        handle->settings.rx_byte_callback = config->rx_byte_callback;
        handle->settings.rx_byte_context = config->rx_byte_context;
        status = aDevUsartRegisterDriverCallback(
            handle, ADRV_USART_EVENT_RX_READY, rx_ready_callback, A_TRUE);
        break;
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
        if (!aDrvUsartEventIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->rx_buffer == NULL) ||
            (config->rx_buffer_size < 2U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->settings.rx_buffer = config->rx_buffer;
        handle->settings.rx_buffer_size = config->rx_buffer_size;
        status = aDevUsartRegisterDriverCallback(
            handle, ADRV_USART_EVENT_RX_READY, rx_ready_callback, A_TRUE);
        break;
#endif

#if ADEV_USART_DMA_BACKEND_ENABLE
    case ADEV_USART_RX_DMA_BUFFERED:
        if (!ADEV_USART_DMA_BACKEND_ENABLE ||
            !aDrvUsartDmaRxIsSupported(handle->drv_handle.id)) {
            return A_STATUS_UNSUPPORTED;
        }
        /* 不提供环形缓冲区时，DMA 仅供 Direct 请求使用。 */
        if (config->rx_buffer == NULL && config->rx_buffer_size == 0U) {
            if (config->mode & ADEV_USART_OPTION_RX_IDLE)
                return A_STATUS_INVALID_PARAM;
            status = A_STATUS_OK;
            break;
        }
        if ((config->rx_buffer == NULL) || (config->rx_buffer_size < 2U) ||
            (config->rx_buffer_size > 65535U)) {
            return A_STATUS_INVALID_PARAM;
        }
        if ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U &&
            !aDrvUsartEventIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        handle->settings.rx_buffer = config->rx_buffer;
        handle->settings.rx_buffer_size = config->rx_buffer_size;
        /* 启动后回调即可执行；先备好状态，关闭驱动后才允许重置。 */
        handle->rx.dma_active = A_TRUE;
        status = aDrvUsartAsyncRxCircularStart(
            &handle->drv_handle, handle->settings.rx_buffer,
                handle->settings.rx_buffer_size,
            aDevUsartRxDmaComplete, handle);
        break;
#endif

    default:
        return A_STATUS_INVALID_PARAM;
    }

#if ADEV_USART_NEEDS_EVENTS
    if (status == A_STATUS_OK &&
        (config->mode & ADEV_USART_RX_MASK) != ADEV_USART_RX_POLLING) {
        status = aDevUsartRegisterDriverCallback(
            handle, ADRV_USART_EVENT_RX_ERROR, rx_error_callback, A_TRUE);
    }
#endif
#if ADEV_USART_INTERRUPT_ENABLE
    if ((status == A_STATUS_OK) &&
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U)) {
#if ADEV_USART_DMA_BACKEND_ENABLE
        if ((config->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_DMA_BUFFERED) {
            status = aDevUsartRegisterDriverCallback(
                handle, ADRV_USART_EVENT_RX_IDLE, rx_dma_idle, A_TRUE);
        } else
#endif
        {
            status = rx_idle_detection_enable(handle);
        }
    }
#endif

    return status;
}

/* 流式读取。 */
static aSSize_t polling_read(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, const aTimepoint_t *end,
                             aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        const aStatus_t status = aDrvUsartTryReadByte(
            &handle->drv_handle, (uint8_t *)buffer + count);

        if (status == A_STATUS_OK) {
            ++count;
        } else if (count != 0U) {
            return (aSSize_t)count;
        } else if (status != A_STATUS_BUSY) {
            return aOSFailWithStatus(status);
        } else if (aOSPollWaitExpired(end)) {
            return aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#if ADEV_USART_INTERRUPT_ENABLE
static aSSize_t buffered_read(aDevUsartHandle_t *handle, void *buffer,
                              size_t buffer_size,
                              const aTimepoint_t *end,
                              aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        size_t available;
        size_t tail;
        aStatus_t status = A_STATUS_OK;

        /* 当前任务独占 tail，回调只增加 count；旧快照只会少读。 */
        status = handle->rx.error;
        available = handle->rx.count;
        tail = handle->rx.tail;

        if (status != A_STATUS_OK)
            return count != 0U ? (aSSize_t)count : aOSFailWithStatus(status);

        if (available != 0U) {
            size_t length = handle->settings.rx_buffer_size - tail;

            if (length > available) length = available;
            if (length > buffer_size - count) length = buffer_size - count;
            /* 调用者保证单消费者；复制完成前不释放占用，ISR 只能
             * 写空闲位置，满时丢弃新字节。因此复制期间无需屏蔽中断。 */
            memcpy((uint8_t *)buffer + count,
                   handle->settings.rx_buffer + tail, length);
            tail += length;
            if (tail == handle->settings.rx_buffer_size) tail = 0U;
            aOSCriticalEnter();
            handle->rx.tail = tail;
            handle->rx.count -= length;
            aOSCriticalExit();
            count += length;
        } else if (count != 0U) {
            return (aSSize_t)count;
        } else if (handle->rx.wait_object != NULL) {
            status = wait_for_event(handle->rx.wait_object, end);
            if (status != A_STATUS_OK) {
                return fail_with_wait_status(status, original_timeout);
            }
        } else if (aOSPollWaitExpired(end)) {
            return aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#endif

#if ADEV_USART_DMA_BACKEND_ENABLE
static aSSize_t dma_buffered_read(aDevUsartHandle_t *handle, void *buffer,
                                  size_t buffer_size,
                                  const aTimepoint_t *end,
                                  aTimeout_t original_timeout)
{
    size_t count = 0U;

    while (count < buffer_size) {
        size_t copied = 0U;
        aStatus_t status = aDevUsartGetRxError(handle);
        if (status != A_STATUS_OK)
            return count != 0U ? (aSSize_t)count :
                   aOSFailWithStatus(status);
        status = aDevUsartDmaRxCopy(
            handle, (uint8_t *)buffer + count, buffer_size - count, &copied);
        count += copied;

        if (count == buffer_size) break;
        if (count != 0U) return (aSSize_t)count;
        if (status != A_STATUS_OK && status != A_STATUS_BUSY) {
            return aOSFailWithStatus(status);
        }
        if (handle->rx.wait_object != NULL) {

            status = wait_for_event(handle->rx.wait_object, end);

            if (status != A_STATUS_OK) {
                return fail_with_wait_status(status, original_timeout);
            }
        } else if (aOSPollWaitExpired(end)) {
            return aOSFailWithTimeout(original_timeout);
        }
    }
    return (aSSize_t)count;
}

#endif

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aSSize_t result;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (buffer_size > (size_t)PTRDIFF_MAX) ||
        ((buffer == NULL) && (buffer_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->drv_handle.initialized) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (buffer_size == 0U) {
        return 0;
    }
    if ((handle->settings.mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_INTERRUPT_CALLBACK)
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);
    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    aStatus_t status = handle->rx.overflow
        ? A_STATUS_ERROR : handle->rx.error;
#if ADEV_USART_ASYNC_ENABLE
    /* 无新订阅并发提交；旧忙状态只会保守返回 BUSY。 */
    if (status == A_STATUS_OK && handle->rx.state != ADEV_USART_RX_IDLE)
        status = A_STATUS_BUSY;
#endif
#if ADEV_USART_DMA_BACKEND_ENABLE
    if (status == A_STATUS_OK &&
        (handle->settings.mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED &&
        !handle->rx.dma_active)
        status = A_STATUS_UNSUPPORTED;
#endif
    if (status != A_STATUS_OK) return aOSFailWithStatus(status);

#if ADEV_USART_DMA_BACKEND_ENABLE
    if ((handle->settings.mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_DMA_BUFFERED) {
        result = dma_buffered_read(handle, buffer, buffer_size, &end, timeout);
    } else
#endif
#if ADEV_USART_INTERRUPT_ENABLE
    if ((handle->settings.mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_INTERRUPT_BUFFERED) {
        result = buffered_read(handle, buffer, buffer_size, &end, timeout);
    } else
#endif
    {
        result = polling_read(handle, buffer, buffer_size, &end, timeout);
    }
    const aBool_t overflow = handle->rx.overflow;
    /* 流已丢字节：即使本次复制了部分数据，也不能按成功交付。 */
    if (overflow)
        return aOSFailWithStatus(A_STATUS_ERROR);
    return result;
}

#if ADEV_USART_DMA_BACKEND_ENABLE
/* 共享 DMA 环形缓冲区。 */
/* 调用方独占 DMA 消费进度，或持有任务/ISR 临界区。 */
aStatus_t aDevUsartDmaRxRefresh(aDevUsartHandle_t *handle)
{
    aDrvUsartRxProgress_t progress;
    const aStatus_t status = aDrvUsartAsyncRxGetProgress(
        &handle->drv_handle, &progress);
    if (status != A_STATUS_OK) return status;

    handle->rx.dma_produced = progress.received;
    if (progress.received - handle->rx.dma_consumed >
        handle->settings.rx_buffer_size) {
        handle->rx.dma_consumed =
            progress.received - handle->settings.rx_buffer_size;
        handle->rx.tail = progress.position;
        handle->rx.overflow = A_TRUE;
        handle->rx.error = A_STATUS_ERROR;
    }
    return A_STATUS_OK;
}

/* CPU 加锁不会停止 DMA。先复制，再根据原始读取位置检查生产进度，
 * 确认未被覆盖后才交付数据；不直接借出正在被 DMA 写入的环形缓冲区。
 * 无异步订阅的同步读取期间，仅当前任务更新消费进度，回调只通知，不消费。
 * 调用者须保证接收操作串行化，操作期间必须保持 DMA 中断使能。 */
aStatus_t aDevUsartDmaRxCopy(aDevUsartHandle_t *handle, void *buffer,
                             size_t capacity, size_t *copied)
{
    *copied = 0U;
    aStatus_t status = aDevUsartDmaRxRefresh(handle);
    const size_t start = handle->rx.dma_consumed;
    size_t length = handle->rx.dma_produced - start;
    if (length > capacity) length = capacity;
    const size_t offset = handle->rx.tail;
    if (length > handle->settings.rx_buffer_size - offset)
        length = handle->settings.rx_buffer_size - offset;
    if (status != A_STATUS_OK) return status;
    if (length == 0U) return A_STATUS_OK;
    memcpy(buffer, handle->settings.rx_buffer + offset, length);
    aAtomicThreadFence(A_ATOMIC_SEQ_CST);
    status = aDevUsartDmaRxRefresh(handle);
    if (status == A_STATUS_OK &&
        handle->rx.dma_produced - start > handle->settings.rx_buffer_size) {
        /* 复制的数据可能已被覆盖而不一致；不报告有效字节，保留溢出锁存状态。 */
        status = A_STATUS_ERROR;
    }
    if (status == A_STATUS_OK) {
        handle->rx.dma_consumed = start + length;
        handle->rx.tail = offset + length == handle->settings.rx_buffer_size ?
                          0U : offset + length;
        *copied = length;
    }
    return status;
}

void aDevUsartRxDmaNotifyFromISR(aDevUsartHandle_t *handle)
{
    if (!handle->rx.dma_active) return;
#if ADEV_USART_ASYNC_ENABLE
    async_rx_dispatch(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx.wait_object);
}

void aDevUsartRxDmaComplete(void *argument)
{
    aDevUsartRxDmaNotifyFromISR(argument);
}

#endif /* DMA 缓冲后端 */

#if ADEV_USART_DIRECT_ENABLE && ADEV_USART_DMA_BACKEND_ENABLE
/* 直接接收到用户缓冲区的同步 DMA 实现。 */
static void direct_rx_complete(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    aOSWaitObjectNotifyFromISR(handle->rx.wait_object);
}

static aSSize_t dma_read_direct(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    size_t count = 0U;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (buffer_size > (size_t)PTRDIFF_MAX) ||
        ((buffer == NULL) && (buffer_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->drv_handle.initialized) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (buffer_size == 0U) return 0;
    if (!aDevUsartIsSupported(handle, ADEV_USART_CAP_RX_DIRECT)) {
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());

    /* 仅无 ring 的 DMA RX 可直收，该配置不能建立异步订阅。
     * dma_active 在初始化后不变；同方向调用由应用串行化。 */
    if (handle->rx.dma_active) {
        return aOSFailWithStatus(A_STATUS_BUSY);
    }

    while (count < buffer_size) {
        size_t transfer_size = buffer_size - count;
        size_t remaining;
        size_t received = 0U;

        if (transfer_size > 65535U) transfer_size = 65535U;
        status = aDrvUsartRxDmaStart(
            &handle->drv_handle, (uint8_t *)buffer + count, transfer_size,
            direct_rx_complete, handle);
        if (status != A_STATUS_OK) break;

        for (;;) {
            status = aDrvUsartAsyncRxGetRemaining(
                &handle->drv_handle, &remaining);
            const aStatus_t error = aDevUsartGetRxError(handle);
            if (error != A_STATUS_OK) status = error;
            if (status == A_STATUS_OK && remaining != 0U)
                status = direct_wait(handle->rx.wait_object, &end, A_FALSE);
            if ((status != A_STATUS_OK) || (remaining == 0U) ||
                aTimepointExpired(&end, aOSGetUptimeMs())) {
                const aBool_t expired =
                    (status == A_STATUS_OK) && (remaining != 0U);

                const aStatus_t stop_status = aDrvUsartAsyncRxStop(
                    &handle->drv_handle, &received);
                if (stop_status != A_STATUS_OK) {
                    (void)aDrvUsartAsyncRxAbort(&handle->drv_handle);
                    if (status == A_STATUS_OK) status = stop_status;
                }
                if (received > transfer_size) {
                    status = A_STATUS_ERROR;
                } else if (aDevUsartGetRxError(handle) == A_STATUS_OK) {
                    count += received;
                } else {
                    /* 无法确定 DMA 数据中的出错位置，本次不发布字节。 */
                    count = 0U;
                }
                if (expired && status == A_STATUS_OK) status = A_STATUS_TIMEOUT;
                if (status == A_STATUS_OK && received == 0U) status =
                    A_STATUS_ERROR;
                break;
            }
        }
        if (status != A_STATUS_OK) break;
        if (count < buffer_size && aTimepointExpired(&end, aOSGetUptimeMs())) {
            status = A_STATUS_TIMEOUT;
            break;
        }
    }

    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

#endif

#if ADEV_USART_DIRECT_ENABLE
aSSize_t aDevUsartReadDirect(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout)
{
    if (handle == NULL || !aTimeoutIsValid(timeout) ||
        buffer_size > (size_t)PTRDIFF_MAX || (buffer == NULL && buffer_size !=
            0U))
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    if (!handle->drv_handle.initialized)
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    if (buffer_size == 0U) return 0;
    const aStatus_t ready = handle->rx.error;
    if (ready != A_STATUS_OK) return aOSFailWithStatus(ready);
#if ADEV_USART_DMA_BACKEND_ENABLE
    if ((handle->settings.mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_DMA_BUFFERED)
        return dma_read_direct(handle, buffer, buffer_size, timeout);
#endif
    if ((handle->settings.mode & ADEV_USART_RX_MASK) != ADEV_USART_RX_POLLING)
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);

    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    aStatus_t status = A_STATUS_OK;

    size_t count = 0U;
    while (count < buffer_size) {
        status = aDrvUsartTryReadByte(&handle->drv_handle, (uint8_t *)buffer +
            count);
        if (status == A_STATUS_OK) {
            ++count;
        } else if (status != A_STATUS_BUSY) {
            break;
        } else if (aOSPollWaitExpired(&end)) {
            status = A_STATUS_TIMEOUT;
            break;
        }
    }
    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT ? aOSFailWithTimeout(timeout)
                                      : aOSFailWithStatus(status);
}
#endif

#if ADEV_USART_ASYNC_ENABLE
/* 仅中断路径交付 RX 事件。快照缓冲区由当前订阅独占，
 * DMA 不会写入该区域，回调执行期间也不例外。 */
static void async_rx_dispatch(aDevUsartHandle_t *handle)
{
    aOSCriticalState_t key = aOSCriticalEnterFromISR();
    if (handle->rx.state != ADEV_USART_RX_ASYNC || handle->rx.dispatching ||
        handle->rx.cancel_pending) {
        aOSCriticalExitFromISR(key);
        return;
    }
    handle->rx.dispatching = A_TRUE;
    aDevUsartRxCallback_t callback = handle->rx.callback;
    void *argument = handle->rx.callback_argument;
    aOSCriticalExitFromISR(key);
    /* aDrv 串行派发回调，dispatching 阻止任务切换消费者。
     * 本次消费期间独占 ring/DMA 游标，只有交接订阅时需要保护。 */
    aStatus_t status = handle->rx.error;

    if (!handle->rx.dma_active) {
        size_t available = handle->rx.count;
        if (handle->rx.overflow)
            status = A_STATUS_ERROR;
        for (unsigned span = 0U; status == A_STATUS_OK && available &&
            span < 2U; ++span) {
            const size_t start = handle->rx.tail;
            size_t length = handle->settings.rx_buffer_size - start;
            if (length > available) length = available;
            const aDevUsartRxEvent_t event = {
                .type = ADEV_USART_RX_EVENT_DATA_READY,
                .buffer = handle->settings.rx_buffer + start,
                .length = length, .status = A_STATUS_OK,
            };
            /* 回调返回前保持占用；满缓冲时生产者丢弃新字节。 */
            callback(handle, &event, argument);
            handle->rx.tail =
                (start + length) % handle->settings.rx_buffer_size;
            handle->rx.count -= length;
            available -= length;
            if (handle->rx.overflow)
                status = A_STATUS_ERROR;
        }
        goto finish;
    }
#if ADEV_USART_DMA_BACKEND_ENABLE
    if (status == A_STATUS_OK) status = aDevUsartDmaRxRefresh(handle);
    if (handle->rx.overflow)
        status = A_STATUS_ERROR;
    const size_t start = handle->rx.dma_consumed;
    const size_t available = handle->rx.dma_produced - start;
    if (status == A_STATUS_OK && available != 0U) {
        const size_t offset = handle->rx.tail;
        uint8_t *snapshot = handle->rx.snapshot;
        size_t first = handle->settings.rx_buffer_size - offset;
        if (first > available) first = available;
        /* dispatching 保持订阅及快照有效；复制期间不持有 CPU 临界区。 */
        memcpy(snapshot, handle->settings.rx_buffer + offset, first);
        memcpy(snapshot + first, handle->settings.rx_buffer, available - first);
        aAtomicThreadFence(A_ATOMIC_SEQ_CST);
        status = aDevUsartDmaRxRefresh(handle);
        if (status == A_STATUS_OK &&
            handle->rx.dma_produced - start > handle->settings.rx_buffer_size)
            status = A_STATUS_ERROR;
        /* CPU 保护不停止 DMA，确认未覆盖后才交付快照。 */
        if (status == A_STATUS_OK) {
            handle->rx.dma_consumed = start + available;
            handle->rx.tail =
                (offset + available) % handle->settings.rx_buffer_size;
            const aDevUsartRxEvent_t event = {
                .type = ADEV_USART_RX_EVENT_DATA_READY,
                .buffer = snapshot,
                .offset = 0U, .length = available, .status = A_STATUS_OK,
            };
            callback(handle, &event, argument);
            status = aDevUsartDmaRxRefresh(handle);
            if (handle->rx.overflow)
                status = A_STATUS_ERROR;
        }
    }
#else
    status = A_STATUS_UNSUPPORTED;
#endif
finish:
    if (status != A_STATUS_OK) {
        handle->rx.error = status;
        const aDevUsartRxEvent_t event = {
            .type = ADEV_USART_RX_EVENT_ERROR, .status = status,
        };
        callback(handle, &event, argument);
    }
    key = aOSCriticalEnterFromISR();
    if (status != A_STATUS_OK) {
        handle->rx.snapshot = NULL;
        handle->rx.callback = NULL;
        handle->rx.callback_argument = NULL;
        handle->rx.state = ADEV_USART_RX_IDLE;
    }
    handle->rx.dispatching = A_FALSE;
    aOSCriticalExitFromISR(key);
}

aStatus_t aDevUsartReadAsync(aDevUsartHandle_t *handle,
                            const aDevUsartReadRequest_t *request)
{
    if (handle == NULL || request == NULL || request->callback == NULL)
        return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    if ((handle->settings.mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_POLLING ||
        handle->settings.rx_buffer == NULL) return A_STATUS_UNSUPPORTED;
    if (handle->rx.dma_active) {
        if (request->buffer == NULL ||
            request->buffer_size < handle->settings.rx_buffer_size)
            return A_STATUS_INVALID_PARAM;
        /* 使用整数差值，避免地址加法溢出及无关联指针的大小比较。 */
        const uintptr_t snapshot = (uintptr_t)request->buffer;
        const uintptr_t ring = (uintptr_t)handle->settings.rx_buffer;
        if (snapshot >= ring ? snapshot - ring < handle->settings.rx_buffer_size
                             : ring - snapshot < request->buffer_size)
            return A_STATUS_INVALID_PARAM;
    }
    aStatus_t status = A_STATUS_OK;
    aOSCriticalEnter();
    if (handle->rx.state != ADEV_USART_RX_IDLE || handle->rx.dispatching) {
        status = A_STATUS_BUSY;
    } else {
#if ADEV_USART_DMA_BACKEND_ENABLE
        if (handle->rx.dma_active) status = aDevUsartDmaRxRefresh(handle);
#endif
        /* 切换接收数据的消费者时，不得静默丢弃流中已有的字节。 */
        if (status == A_STATUS_OK && (handle->rx.count != 0U ||
            handle->rx.dma_produced != handle->rx.dma_consumed))
            status = A_STATUS_BUSY;
        if (status == A_STATUS_OK)
            status = handle->rx.error;
        if (status == A_STATUS_OK) {
            handle->rx.snapshot = request->buffer;
            handle->rx.callback = request->callback;
            handle->rx.callback_argument = request->argument;
            handle->rx.state = ADEV_USART_RX_ASYNC;
        }
    }
    aOSCriticalExit();

    return status;
}

aStatus_t aDevUsartReadAsyncCancel(aDevUsartHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;
    aStatus_t status = A_STATUS_OK;
    aOSCriticalEnter();
    if (handle->rx.state != ADEV_USART_RX_ASYNC || handle->rx.dispatching ||
        handle->rx.cancel_pending) {
        status = (handle->rx.dispatching || handle->rx.cancel_pending ||
                  handle->rx.state != ADEV_USART_RX_IDLE)
            ? A_STATUS_BUSY : A_STATUS_NOT_READY;
        aOSCriticalExit();

        return status;
    }
    handle->rx.cancel_pending = A_TRUE;
    aOSCriticalExit();

    (void)aDrvUsartRequestDispatch(&handle->drv_handle);
    return A_STATUS_OK;
}

void aDevUsartAsyncRxCancelFromISR(aDevUsartHandle_t *handle)
{
    aOSCriticalState_t key = aOSCriticalEnterFromISR();
    if (!handle->rx.cancel_pending || handle->rx.dispatching) {
        aOSCriticalExitFromISR(key);
        return;
    }
    handle->rx.dispatching = A_TRUE;
    handle->rx.cancel_pending = A_FALSE;
    aDevUsartRxCallback_t callback = handle->rx.callback;
    void *argument = handle->rx.callback_argument;
    const aDevUsartRxEvent_t event = {
        .type = ADEV_USART_RX_EVENT_CANCELLED, .status = A_STATUS_CANCELLED,
    };
    aOSCriticalExitFromISR(key);
    callback(handle, &event, argument);
    key = aOSCriticalEnterFromISR();
    handle->rx.snapshot = NULL;
    handle->rx.callback = NULL;
    handle->rx.callback_argument = NULL;
    handle->rx.state = ADEV_USART_RX_IDLE;
    handle->rx.dispatching = A_FALSE;
    aOSCriticalExitFromISR(key);
}

#endif

aStatus_t aDevUsartDiscardRx(aDevUsartHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->drv_handle.initialized) return A_STATUS_NOT_READY;

    aStatus_t status = A_STATUS_OK;
    aOSCriticalEnter();
    if (handle->rx.state != ADEV_USART_RX_IDLE || handle->rx.dispatching ||
        handle->rx.cancel_pending) {
        status = A_STATUS_BUSY;
    } else {
        switch (handle->settings.mode & ADEV_USART_RX_MASK) {
#if ADEV_USART_INTERRUPT_ENABLE
        case ADEV_USART_RX_INTERRUPT_BUFFERED:
            handle->rx.tail = handle->rx.head;
            handle->rx.count = 0U;
            break;
#endif
#if ADEV_USART_DMA_BACKEND_ENABLE
        case ADEV_USART_RX_DMA_BUFFERED: {
            aDrvUsartRxProgress_t progress;
            if (!handle->rx.dma_active) {
                status = A_STATUS_UNSUPPORTED;
                break;
            }
            status = aDrvUsartAsyncRxGetProgress(&handle->drv_handle, &progress);
            if (status == A_STATUS_OK) {
                /* DMA 不因临界区停止；只丢弃快照之前的数据，保留后续进度。 */
                handle->rx.dma_produced = progress.received;
                handle->rx.dma_consumed = progress.received;
                handle->rx.tail = progress.position;
            }
            break;
        }
#endif
        default:
            status = A_STATUS_UNSUPPORTED;
            break;
        }
        if (status == A_STATUS_OK) {
            handle->rx.overflow = A_FALSE;
            handle->rx.error = A_STATUS_OK;
        }
    }
    aOSCriticalExit();
    return status;
}

uint32_t aDevUsartGetIdleEventCount(const aDevUsartHandle_t *handle)
{
    if (handle == NULL) return 0U;
    return handle->rx.idle_event_count;
}

aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle)
{
    if (handle == NULL) return A_FALSE;
    return handle->rx.overflow;
}

void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle)
{
    if (handle == NULL) return;
    handle->rx.overflow = A_FALSE;
}

void aDevUsartClearRxError(aDevUsartHandle_t *handle)
{
    if (handle == NULL) return;
    handle->rx.error = A_STATUS_OK;
}

aStatus_t aDevUsartGetRxError(const aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }
    return handle->rx.error;
}
