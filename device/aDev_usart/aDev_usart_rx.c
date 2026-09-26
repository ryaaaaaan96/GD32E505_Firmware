#include "aDev_usart_internal.h"

#include "aOS.h"

#if ADEV_USART_HAS_INTERRUPT
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

#endif

#if ADEV_USART_HAS_INTERRUPT
static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
#if ADEV_USART_HAS_DMA
    if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED)
        aDevUsartRxDmaNotifyFromISR(handle);
#endif
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_IDLE);
}

#endif

#if ADEV_USART_HAS_DMA && ADEV_USART_HAS_INTERRUPT
static void rx_dma_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
    aDevUsartRxDmaNotifyFromISR(handle);
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    aDevUsartNotifyEvent(handle, ADEV_USART_EVENT_RX_IDLE);
}

#endif

#if ADEV_USART_HAS_INTERRUPT
static aStatus_t rx_idle_detection_enable(
    aDevUsartHandle_t *handle, const aDevUsartConfig_t *config)
{
    aDrvInterruptCallback_t callback = irq_idle;

    if (!aDrvUsartInterruptIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }

    return aDevUsartRegisterIrqCallback(
        handle, ADRV_USART_EXTI_IDLE, callback,
        config->interrupt_priority, A_TRUE);
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
#if ADEV_USART_HAS_INTERRUPT
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
#endif

#if ADEV_USART_HAS_DMA
    case ADEV_USART_RX_DMA_BUFFERED:
        if (!ADEV_USART_HAS_DMA ||
            !aDrvUsartAsyncRxIsSupported(&handle->drv_handle)) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->rx_buffer == NULL) || (config->rx_buffer_size < 2U) ||
            (config->rx_buffer_size > 65535U)) {
            return A_STATUS_INVALID_PARAM;
        }
        if ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U &&
            !aDrvUsartInterruptIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        handle->rx_buffer = config->rx_buffer;
        handle->rx_buffer_size = config->rx_buffer_size;
        status = aDrvUsartAsyncRxCircularStart(
            &handle->drv_handle, handle->rx_buffer, handle->rx_buffer_size,
            config->interrupt_priority, aDevUsartRxDmaComplete, handle);
        if (status == A_STATUS_OK) {
            handle->rx_dma_active = A_TRUE;
        }
        break;
#endif

    default:
        return A_STATUS_INVALID_PARAM;
    }

#if ADEV_USART_HAS_INTERRUPT
    if ((status == A_STATUS_OK) &&
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) != 0U)) {
#if ADEV_USART_HAS_DMA
        if ((config->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_DMA_BUFFERED) {
            status = aDevUsartRegisterIrqCallback(
                handle, ADRV_USART_EXTI_IDLE, rx_dma_idle,
                config->interrupt_priority, A_TRUE);
        } else
#endif
        {
            status = rx_idle_detection_enable(handle, config);
        }
    }
#endif

    return status;
}
