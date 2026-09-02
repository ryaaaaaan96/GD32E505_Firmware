#include "aDev_usart.h"

#include "aOS.h"

#include <limits.h>

static void notify_event(aDevUsartHandle_t *handle,
                         aDevUsartEvent_t event)
{
    if (handle->event_callback != NULL) {
        handle->event_callback(event, handle->event_argument);
    }
}

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
    notify_event(handle, ADEV_USART_EVENT_RX_READY);
}

static void irq_transmit(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    if (handle->tx_count == 0U) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        return;
    }

    if (aDrvUsartTryWriteByte(&handle->drv_handle,
                              handle->tx_buffer[handle->tx_tail]) ==
        A_STATUS_OK) {
        handle->tx_tail = (handle->tx_tail + 1U) % handle->tx_buffer_size;
        --handle->tx_count;
        aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
        notify_event(handle, ADEV_USART_EVENT_TX_SPACE);
        if (handle->tx_count == 0U) {
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_TRUE);
        }
    }
}

static void irq_transmit_complete(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    (void)aDrvUsartSetInterruptEnabled(
        &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
    aOSWaitObjectNotifyFromISR(handle->tx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_TX_COMPLETE);
}

static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_RX_IDLE);
}

static aStatus_t dma_rx_commit(aDevUsartHandle_t *handle)
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
        if (status != A_STATUS_OK) {
            handle->rx_overflow = A_TRUE;
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

    if (status != A_STATUS_OK) {
        handle->rx_overflow = A_TRUE;
    }
    return status;
}

static void dma_rx_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    (void)dma_rx_commit(handle);
    ++handle->idle_event_count;
    aOSWaitObjectNotifyFromISR(handle->rx_wait_object);
    notify_event(handle, ADEV_USART_EVENT_RX_READY);
    notify_event(handle, ADEV_USART_EVENT_RX_IDLE);
}

static aStatus_t register_irq_callback(aDevUsartHandle_t *handle,
                                       aDrvUsartExti_t trigger,
                                       aDrvInterruptCallback_t callback,
                                       uint8_t priority, aBool_t enabled)
{
    aDrvUsartExtiConfig_t config;

    config.trigger = trigger;
    config.priority = priority;
    config.callback = callback;
    config.argument = handle;
    config.enabled = enabled;
    return aDrvUsartRegisterCallback(&handle->drv_handle, &config);
}

static aBool_t mode_is_valid(aDevUsartMode_t mode)
{
    const aDevUsartMode_t rx_mode = mode & ADEV_USART_RX_MASK;

    if ((mode & ~ADEV_USART_MODE_VALID_MASK) != 0U) {
        return A_FALSE;
    }
    if (((mode & ADEV_USART_OPTION_RX_IDLE) != 0U) &&
        (rx_mode == ADEV_USART_RX_POLLING)) {
        return A_FALSE;
    }

    switch (mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_POLLING:
    case ADEV_USART_TX_INTERRUPT_BUFFERED:
    case ADEV_USART_TX_DMA_DIRECT:
        break;
    default:
        return A_FALSE;
    }

    switch (rx_mode) {
    case ADEV_USART_RX_POLLING:
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
    case ADEV_USART_RX_DMA_CIRCULAR:
        return A_TRUE;
    default:
        return A_FALSE;
    }
}

static aStatus_t tx_mode_init(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config)
{
    switch (config->mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_POLLING:
        return A_STATUS_OK;
    case ADEV_USART_TX_INTERRUPT_BUFFERED: {
        aStatus_t status;

        if (!aDrvUsartInterruptIsSupported()) {
            return A_STATUS_UNSUPPORTED;
        }
        if ((config->tx_buffer == NULL) ||
            (config->tx_buffer_size < 2U)) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->tx_buffer = config->tx_buffer;
        handle->tx_buffer_size = config->tx_buffer_size;
        status = register_irq_callback(
            handle, ADRV_USART_EXTI_TXE, irq_transmit,
            config->interrupt_priority, A_FALSE);
        if (status != A_STATUS_OK) {
            return status;
        }
        return register_irq_callback(
            handle, ADRV_USART_EXTI_TC, irq_transmit_complete,
            config->interrupt_priority, A_FALSE);
    }
    case ADEV_USART_TX_DMA_DIRECT:
        return aDrvUsartAsyncTxIsSupported(&handle->drv_handle)
                   ? A_STATUS_OK
                   : A_STATUS_UNSUPPORTED;
    default:
        return A_STATUS_INVALID_PARAM;
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
    return register_irq_callback(
        handle, ADRV_USART_EXTI_IDLE, callback,
        config->interrupt_priority, A_TRUE);
}

static aStatus_t rx_mode_init(aDevUsartHandle_t *handle,
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
        status = register_irq_callback(
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
            handle->rx_buffer_size, config->interrupt_priority);
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

static aStatus_t wait_objects_create(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((handle->mode & ADEV_USART_TX_MASK) ==
        ADEV_USART_TX_INTERRUPT_BUFFERED) {
        status = aOSWaitObjectCreate(&handle->tx_wait_object);
        if (status != A_STATUS_OK) {
            return status;
        }
    }

    if (((handle->mode & ADEV_USART_RX_MASK) ==
         ADEV_USART_RX_INTERRUPT_BUFFERED) ||
        (((handle->mode & ADEV_USART_RX_MASK) ==
          ADEV_USART_RX_DMA_CIRCULAR) &&
         ((handle->mode & ADEV_USART_OPTION_RX_IDLE) != 0U))) {
        status = aOSWaitObjectCreate(&handle->rx_wait_object);
        if (status != A_STATUS_OK) {
            aOSWaitObjectDestroy(&handle->tx_wait_object);
            return status;
        }
    }

    return A_STATUS_OK;
}

static void wait_objects_destroy(aDevUsartHandle_t *handle)
{
    aOSWaitObjectDestroy(&handle->rx_wait_object);
    aOSWaitObjectDestroy(&handle->tx_wait_object);
}

void aDevUsartConfigStructInit(aDevUsartConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    aDrvUsartConfigStructInit(&config->drv_config);
    config->mode = ADEV_USART_TX_POLLING | ADEV_USART_RX_POLLING;
    config->interrupt_priority = 5U;
    config->rx_buffer = NULL;
    config->rx_buffer_size = 0U;
    config->tx_buffer = NULL;
    config->tx_buffer_size = 0U;
}

void aDevUsartHandleStructInit(aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    aDrvUsartHandleStructInit(&handle->drv_handle);
    handle->mode = ADEV_USART_TX_POLLING | ADEV_USART_RX_POLLING;
    handle->rx_buffer = NULL;
    handle->rx_buffer_size = 0U;
    handle->rx_dma_observed = 0U;
    handle->rx_head = 0U;
    handle->rx_tail = 0U;
    handle->rx_count = 0U;
    handle->tx_buffer = NULL;
    handle->tx_buffer_size = 0U;
    handle->tx_head = 0U;
    handle->tx_tail = 0U;
    handle->tx_count = 0U;
    handle->rx_wait_object = NULL;
    handle->tx_wait_object = NULL;
    handle->event_callback = NULL;
    handle->event_argument = NULL;
    handle->idle_event_count = 0U;
    handle->rx_overflow = A_FALSE;
}

aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((config == NULL) || (handle == NULL) ||
        !mode_is_valid(config->mode)) {
        return A_STATUS_INVALID_PARAM;
    }

    status = aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    handle->mode = config->mode;

    status = wait_objects_create(handle);
    if (status == A_STATUS_OK) {
        status = tx_mode_init(handle, config);
    }
    if (status == A_STATUS_OK) {
        status = rx_mode_init(handle, config);
    }

    if (status != A_STATUS_OK) {
        (void)aDrvUsartDeInitStatic(&handle->drv_handle);
        wait_objects_destroy(handle);
        aDevUsartHandleStructInit(handle);
    }
    return status;
}

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    status = aDrvUsartDeInitStatic(&handle->drv_handle);
    if (status == A_STATUS_OK) {
        wait_objects_destroy(handle);
        aDevUsartHandleStructInit(handle);
    }
    return status;
}

aStatus_t aDevUsartRegisterEventCallback(
    aDevUsartHandle_t *handle,
    aDevUsartEventCallback_t callback,
    void *argument)
{
    if ((handle == NULL) || (callback == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    handle->event_argument = argument;
    handle->event_callback = callback;
    aDrvUsartEnableInterrupt(&handle->drv_handle);
    return A_STATUS_OK;
}

aStatus_t aDevUsartUnregisterEventCallback(
    aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    handle->event_callback = NULL;
    handle->event_argument = NULL;
    aDrvUsartEnableInterrupt(&handle->drv_handle);
    return A_STATUS_OK;
}

static aStatus_t wait_for_event(void *wait_object,
                                const aTimepoint_t *end)
{
    return aOSWaitObjectWait(
        wait_object, aTimepointRemaining(end, aOSGetUptimeMs()));
}

static aSSize_t fail_with_wait_status(aStatus_t status,
                                      aTimeout_t timeout)
{
    return ((status == A_STATUS_BUSY) ||
            (status == A_STATUS_TIMEOUT))
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

static aSSize_t polling_read(aDevUsartHandle_t *handle, void *buffer,
                             size_t buffer_size, aTimeout_t timeout)
{
    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    size_t count = 0U;

    while (count < buffer_size) {
        const aStatus_t status = aDrvUsartTryReadByte(
            &handle->drv_handle, (uint8_t *)buffer + count);

        if (status == A_STATUS_OK) {
            ++count;
        } else if (status != A_STATUS_BUSY) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        } else if (aOSPollWaitExpired(&end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(timeout);
        }
    }
    return (aSSize_t)count;
}

static aSSize_t buffered_read(aDevUsartHandle_t *handle, void *buffer,
                              size_t buffer_size, aTimeout_t timeout)
{
    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    size_t count = 0U;

    while (count < buffer_size) {
        aBool_t available;
        aStatus_t status = A_STATUS_OK;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
        if ((handle->mode & ADEV_USART_RX_MASK) ==
            ADEV_USART_RX_DMA_CIRCULAR) {
            status = dma_rx_commit(handle);
        }
        available = handle->rx_count != 0U;
        if (available) {
            ((uint8_t *)buffer)[count] = handle->rx_buffer[handle->rx_tail];
            handle->rx_tail = (handle->rx_tail + 1U) % handle->rx_buffer_size;
            --handle->rx_count;
        }
        aDrvUsartEnableInterrupt(&handle->drv_handle);

        if (available) {
            ++count;
        } else if (status != A_STATUS_OK) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        } else if (handle->rx_wait_object != NULL) {
            status = wait_for_event(handle->rx_wait_object, &end);
            if (status != A_STATUS_OK) {
                return count != 0U ? (aSSize_t)count
                                   : fail_with_wait_status(status, timeout);
            }
        } else if (aOSPollWaitExpired(&end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(timeout);
        }
    }
    return (aSSize_t)count;
}

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout)
{
    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (buffer_size > (size_t)PTRDIFF_MAX) ||
        ((buffer == NULL) && (buffer_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (buffer_size == 0U) {
        return 0;
    }

    return ((handle->mode & ADEV_USART_RX_MASK) !=
            ADEV_USART_RX_POLLING)
               ? buffered_read(handle, buffer, buffer_size, timeout)
               : polling_read(handle, buffer, buffer_size, timeout);
}

static aSSize_t polling_write(aDevUsartHandle_t *handle, const void *data,
                              size_t data_size, aTimeout_t timeout)
{
    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    size_t count = 0U;

    while (count < data_size) {
        const aStatus_t status = aDrvUsartTryWriteByte(
            &handle->drv_handle, ((const uint8_t *)data)[count]);

        if (status == A_STATUS_OK) {
            ++count;
        } else if (status != A_STATUS_BUSY) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        } else if (aOSPollWaitExpired(&end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(timeout);
        }
    }
    return (aSSize_t)count;
}

static aSSize_t dma_write(aDevUsartHandle_t *handle, const void *data,
                          size_t data_size, aTimeout_t timeout)
{
    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    size_t count = 0U;

    while (count < data_size) {
        size_t started = 0U;
        aStatus_t status;

        status = aDrvUsartAsyncTxStart(
            &handle->drv_handle, (const uint8_t *)data + count,
            data_size - count, &started);
        if (status != A_STATUS_OK) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithStatus(status);
        }

        for (;;) {
            size_t remaining;

            status = aDrvUsartAsyncTxGetRemaining(
                &handle->drv_handle, &remaining);
            if (status != A_STATUS_OK) {
                (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
                return count != 0U ? (aSSize_t)count
                                   : aOSFailWithStatus(status);
            }
            if (remaining == 0U) {
                count += started;
                break;
            }
            if (aOSPollWaitExpired(&end)) {
                (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
                count += started - remaining;
                return count != 0U ? (aSSize_t)count
                                   : aOSFailWithTimeout(timeout);
            }
        }
    }
    return (aSSize_t)count;
}

static aSSize_t interrupt_write(aDevUsartHandle_t *handle, const void *data,
                                size_t data_size, aTimeout_t timeout)
{
    const aTimepoint_t end = aTimepointCalc(timeout, aOSGetUptimeMs());
    size_t count = 0U;

    while (count < data_size) {
        aBool_t space_available;
        aStatus_t status = A_STATUS_OK;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
        space_available = handle->tx_count < handle->tx_buffer_size;
        if (space_available) {
            handle->tx_buffer[handle->tx_head] = ((const uint8_t *)data)[count];
            handle->tx_head = (handle->tx_head + 1U) % handle->tx_buffer_size;
            ++handle->tx_count;
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, A_TRUE);
        }
        aDrvUsartEnableInterrupt(&handle->drv_handle);

        if (space_available) {
            ++count;
        } else {
            status = wait_for_event(handle->tx_wait_object, &end);
            if (status != A_STATUS_OK) {
                return count != 0U ? (aSSize_t)count
                                   : fail_with_wait_status(status, timeout);
            }
        }
    }
    return (aSSize_t)count;
}

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout)
{
    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (data_size > (size_t)PTRDIFF_MAX) ||
        ((data == NULL) && (data_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (data_size == 0U) {
        return 0;
    }

    switch (handle->mode & ADEV_USART_TX_MASK) {
    case ADEV_USART_TX_DMA_DIRECT:
        return dma_write(handle, data, data_size, timeout);
    case ADEV_USART_TX_INTERRUPT_BUFFERED:
        return interrupt_write(handle, data, data_size, timeout);
    case ADEV_USART_TX_POLLING:
    default:
        return polling_write(handle, data, data_size, timeout);
    }
}

aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                         aTimeout_t timeout)
{
    aTimepoint_t end;

    if ((handle == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    for (;;) {
        aBool_t complete;
        const aStatus_t status = aDrvUsartIsTransmitComplete(
            &handle->drv_handle, &complete);

        if (status != A_STATUS_OK) {
            return status;
        }
        if (complete &&
            (((handle->mode & ADEV_USART_TX_MASK) !=
              ADEV_USART_TX_INTERRUPT_BUFFERED) ||
                         handle->tx_count == 0U)) {
            return A_STATUS_OK;
        }
        if ((handle->mode & ADEV_USART_TX_MASK) ==
            ADEV_USART_TX_INTERRUPT_BUFFERED) {
            const aStatus_t wait_status = wait_for_event(
                handle->tx_wait_object, &end);

            if (wait_status != A_STATUS_OK) {
                if ((wait_status == A_STATUS_BUSY) ||
                    (wait_status == A_STATUS_TIMEOUT)) {
                    return ((timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
                            (timeout.milliseconds == 0U))
                               ? A_STATUS_BUSY
                               : A_STATUS_TIMEOUT;
                }
                return wait_status;
            }
            continue;
        }
        if (aOSPollWaitExpired(&end)) {
            return timeout.milliseconds == 0U ? A_STATUS_BUSY
                                              : A_STATUS_TIMEOUT;
        }
    }
}

uint32_t aDevUsartGetIdleEventCount(const aDevUsartHandle_t *handle)
{
    return handle == NULL ? 0U : handle->idle_event_count;
}

aBool_t aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle)
{
    return (handle != NULL) && handle->rx_overflow;
}

void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle)
{
    if (handle != NULL) {
        handle->rx_overflow = A_FALSE;
    }
}
