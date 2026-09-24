#include "aDev_usart_internal.h"

#include "aOS.h"

static void irq_receive(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    uint8_t data;

    if (aDrvUsartTryReadByte(&handle->drv_handle, &data) != A_STATUS_OK) {
        return;
    }
    if (handle->rx_count >= handle->rx_buffer_size) {
        handle->rx_overflow = A_TRUE;
        return;
    }

    handle->rx_buffer[handle->rx_head] = data;
    handle->rx_head = (handle->rx_head + 1U) % handle->rx_buffer_size;
    ++handle->rx_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_READY);
}

static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_IDLE);
}

aStatus_t aDevUsartDmaRxCommit(aDevUsartHandle_t *handle)
{
    size_t received = handle->rx_dma_observed;
    size_t added;
    size_t free_space;
    aStatus_t status;

    status = aDrvUsartAsyncRxGetReceivedCount(&handle->drv_handle,
                                              &received);
    added = received - handle->rx_dma_observed;
    handle->rx_dma_observed = received;

    if (added == 0U) {
        if ((status != A_STATUS_OK) && (status != A_STATUS_BUSY)) {
            handle->rx_error = status;
        }
        return status;
    }

    free_space = handle->rx_buffer_size - handle->rx_count;
    if (added >= handle->rx_buffer_size) {
        handle->rx_tail = received % handle->rx_buffer_size;
        handle->rx_count = handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
    } else if (added > free_space) {
        const size_t discarded = added - free_space;

        handle->rx_tail =
            (handle->rx_tail + discarded) % handle->rx_buffer_size;
        handle->rx_count = handle->rx_buffer_size;
        handle->rx_overflow = A_TRUE;
    } else {
        handle->rx_count += added;
    }
    handle->rx_head = received % handle->rx_buffer_size;

    if ((status != A_STATUS_OK) && (status != A_STATUS_BUSY)) {
        handle->rx_error = status;
    }
    return status;
}

static void dma_rx_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    const aStatus_t status = aDevUsartDmaRxCommit(handle);

    ++handle->idle_event_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    if ((status == A_STATUS_OK) || (status == A_STATUS_BUSY)) {
        aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_READY);
    } else {
        aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_ERROR);
    }
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_IDLE);
}

static void dma_rx_progress(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    const aStatus_t status = aDevUsartDmaRxCommit(handle);

    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    if ((status == A_STATUS_OK) || (status == A_STATUS_BUSY)) {
        aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_READY);
    } else {
        aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_ERROR);
    }
}

static aStatus_t rx_idle_detection_enable(
    aDevUsartHandle_t *handle, const aDevUsartConfig_t *config)
{
    aDrvInterruptCallback_t callback = irq_idle;

    if (!aDrvUsartInterruptIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }
    if ((config->mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_DMA_CIRCULAR) {
        callback = dma_rx_idle;
    }
    return aDevUsartRegisterIrqCallback(
        handle, ADRV_USART_EXTI_IDLE, callback,
        config->interrupt_priority, A_TRUE);
}

aStatus_t aDevUsartRxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    aStatus_t status;

    switch (config->mode & ADEV_USART_RX_MASK) {
    case ADEV_USART_RX_POLLING:
        status = A_STATUS_OK;
        break;
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
        if (!aDrvUsartInterruptIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->rx_buffer == NULL) ||
            (config->rx_buffer_size < 2U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->rx_buffer = config->rx_buffer;
        handle->rx_buffer_size = config->rx_buffer_size;
        status = aDevUsartRegisterIrqCallback(
            handle, ADRV_USART_EXTI_RXNE, irq_receive,
            config->interrupt_priority, A_TRUE);
        break;
    case ADEV_USART_RX_DMA_CIRCULAR:
        if (!aDrvUsartAsyncRxIsSupported(&handle->drv_handle)) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->rx_buffer == NULL) ||
            (config->rx_buffer_size < 2U) ||
            (config->rx_buffer_size > 65535U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->rx_buffer = config->rx_buffer;
        handle->rx_buffer_size = config->rx_buffer_size;
        status = aDrvUsartAsyncRxCircularStart(
            &handle->drv_handle, handle->rx_buffer,
            handle->rx_buffer_size, config->interrupt_priority,
            dma_rx_progress, handle);
        break;
    default:
        return A_STATUS_INVALID_PARAM;
    }

    if ((status == A_STATUS_OK) &&
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U)) {
        status = rx_idle_detection_enable(handle, config);
    }
    return status;
}
