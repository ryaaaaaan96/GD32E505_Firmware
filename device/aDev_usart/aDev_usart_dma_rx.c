#include "aDev_usart_internal.h"
#include <string.h>

/* Updates the DMA producer count and preserves the newest ring contents. */
aStatus_t aDevUsartDmaRxRefresh(aDevUsartHandle_t *handle)
{
    size_t produced = handle->rx_dma_produced;
    const aBool_t overflow_was_set = handle->rx_overflow;
    const aStatus_t status = aDrvUsartAsyncRxGetReceivedCount(
        &handle->drv_handle, &produced);
    if (status != A_STATUS_OK) return status;

    handle->rx_dma_produced = produced;
    if (produced - handle->rx_dma_consumed > handle->rx_buffer_size) {
        handle->rx_dma_consumed = produced - handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
        handle->rx_error = A_STATUS_ERROR;
        if (!overflow_was_set) {
            atomic_fetch_or_explicit(
                &handle->pending_events,
                1UL << ADEV_USART_EVENT_RX_ERROR,
                memory_order_release);
            (void)aOSWorkSubmit(&handle->event_work,
                                aDevUsartEventWork, handle);
        }
    }
    return A_STATUS_OK;
}


/* CPU locks do not stop DMA. Copy then validate the producer against the
 * original cursor before publishing any bytes. Never lend the live DMA ring.
 * Caller holds rx_mutex; DMA IRQ must remain enabled during this operation. */
aStatus_t aDevUsartDmaRxCopy(aDevUsartHandle_t *handle, void *buffer,
                             size_t capacity, size_t *copied)
{
    *copied = 0U;
    aStatus_t status = aDevUsartDmaRxRefresh(handle);
    if (status != A_STATUS_OK) return status;
    const size_t start = handle->rx_dma_consumed;
    size_t length = handle->rx_dma_produced - start;
    if (length > capacity) length = capacity;
    const size_t offset = start % handle->rx_buffer_size;
    if (length > handle->rx_buffer_size - offset)
        length = handle->rx_buffer_size - offset;
    if (length == 0U) return A_STATUS_OK;
    memcpy(buffer, handle->rx_buffer + offset, length);
    atomic_thread_fence(memory_order_seq_cst);
    status = aDevUsartDmaRxRefresh(handle);
    if (status != A_STATUS_OK) return status;
    if (handle->rx_dma_produced - start > handle->rx_buffer_size) {
        /* The copied span may be torn. Report no bytes, keep overflow latched. */
        return A_STATUS_ERROR;
    }
    handle->rx_dma_consumed = start + length;
    *copied = length;
    return A_STATUS_OK;
}

void aDevUsartRxDmaNotifyFromISR(aDevUsartHandle_t *handle)
{
    if (!handle->rx_dma_active) return;
#if ADEV_USART_HAS_ASYNC
    (void)aOSWorkSubmitFromISR(&handle->rx_completion_work,
                               aDevUsartAsyncRxWork, handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_READY);
}

void aDevUsartRxDmaComplete(void *argument)
{
    aDevUsartRxDmaNotifyFromISR(argument);
}
