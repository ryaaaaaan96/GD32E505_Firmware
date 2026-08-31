#include "aDev_usart.h"

#include "aOS.h"

#include <limits.h>

static void irq_receive(void *argument)
{
    aDevUsartHandle_t *handle = argument;
    uint8_t data;

    if (aDrvUsartTryReadByte(&handle->drv_handle, &data) != A_STATUS_OK) {
        return;
    }

    if (handle->rx_count >= handle->rx_buffer_size) {
        handle->rx_overflow = 1U;
        return;
    }

    handle->rx_buffer[handle->rx_head] = data;
    handle->rx_head = (handle->rx_head + 1U) % handle->rx_buffer_size;
    ++handle->rx_count;
}

static void irq_transmit(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    if (handle->tx_count == 0U) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TXE, false);
        return;
    }

    if (aDrvUsartTryWriteByte(&handle->drv_handle,
                              handle->tx_buffer[handle->tx_tail]) ==
        A_STATUS_OK) {
        handle->tx_tail = (handle->tx_tail + 1U) % handle->tx_buffer_size;
        --handle->tx_count;
    }
}

static void irq_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    ++handle->idle_event_count;
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
            handle->rx_overflow = 1U;
        }
        return status;
    }

    free_space = handle->rx_buffer_size - handle->rx_count;
    if (added >= handle->rx_buffer_size) {
        handle->rx_tail = received % handle->rx_buffer_size;
        handle->rx_count = handle->rx_buffer_size;
        handle->rx_overflow = 1U;
    } else if (added > free_space) {
        const size_t discarded = added - free_space;

        handle->rx_tail =
            (handle->rx_tail + discarded) % handle->rx_buffer_size;
        handle->rx_count = handle->rx_buffer_size;
        handle->rx_overflow = 1U;
    } else {
        handle->rx_count += added;
    }
    handle->rx_head = received % handle->rx_buffer_size;

    if (status != A_STATUS_OK) {
        handle->rx_overflow = 1U;
    }
    return status;
}

static void dma_rx_idle(void *argument)
{
    aDevUsartHandle_t *handle = argument;

    (void)dma_rx_commit(handle);
    ++handle->idle_event_count;
}

static aStatus_t register_irq_callback(aDevUsartHandle_t *handle,
                                       aDrvUsartExti_t trigger,
                                       aDrvInterruptCallback_t callback,
                                       uint8_t priority, bool enabled)
{
    aDrvUsartExtiConfig_t config;

    config.trigger = trigger;
    config.priority = priority;
    config.callback = callback;
    config.argument = handle;
    config.enable = enabled ? 1U : 0U;
    return aDrvUsartRegisterCallback(&handle->drv_handle, &config);
}

static aStatus_t interrupt_mode_init(aDevUsartHandle_t *handle,
                                     const aDevUsartConfig_t *config)
{
    aStatus_t status;

    if (!aDrvUsartInterruptIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }
    if ((config->rx_buffer == NULL) || (config->rx_buffer_size < 2U) ||
        (config->tx_buffer == NULL) || (config->tx_buffer_size < 2U)) {
        return A_STATUS_INVALID_PARAM;
    }

    handle->rx_buffer = config->rx_buffer;
    handle->rx_buffer_size = config->rx_buffer_size;
    handle->tx_buffer = config->tx_buffer;
    handle->tx_buffer_size = config->tx_buffer_size;

    status = register_irq_callback(handle, ADRV_USART_EXTI_RXNE,
                                   irq_receive, config->interrupt_priority,
                                   true);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = register_irq_callback(handle, ADRV_USART_EXTI_TXE,
                                   irq_transmit, config->interrupt_priority,
                                   false);
    if (status != A_STATUS_OK) {
        return status;
    }
    return register_irq_callback(handle, ADRV_USART_EXTI_IDLE,
                                 irq_idle, config->interrupt_priority, true);
}

static aStatus_t dma_mode_init(aDevUsartHandle_t *handle,
                               const aDevUsartConfig_t *config)
{
    (void)config;
    return aDrvUsartAsyncTxIsSupported(&handle->drv_handle)
               ? A_STATUS_OK
               : A_STATUS_UNSUPPORTED;
}

static aStatus_t buffered_tx_dma_rx_init(
    aDevUsartHandle_t *handle, const aDevUsartConfig_t *config)
{
    aStatus_t status;

    if (!aDrvUsartInterruptIsSupported() ||
        !aDrvUsartAsyncRxIsSupported(&handle->drv_handle)) {
        return A_STATUS_UNSUPPORTED;
    }
    if ((config->rx_buffer == NULL) || (config->rx_buffer_size < 2U) ||
        (config->rx_buffer_size > 65535U) ||
        (config->tx_buffer == NULL) || (config->tx_buffer_size < 2U)) {
        return A_STATUS_INVALID_PARAM;
    }

    handle->rx_buffer = config->rx_buffer;
    handle->rx_buffer_size = config->rx_buffer_size;
    handle->tx_buffer = config->tx_buffer;
    handle->tx_buffer_size = config->tx_buffer_size;

    status = register_irq_callback(handle, ADRV_USART_EXTI_TXE,
                                   irq_transmit,
                                   config->interrupt_priority, false);
    if (status == A_STATUS_OK) {
        status = register_irq_callback(handle, ADRV_USART_EXTI_IDLE,
                                       dma_rx_idle,
                                       config->interrupt_priority, false);
    }
    if (status == A_STATUS_OK) {
        status = aDrvUsartAsyncRxCircularStart(
            &handle->drv_handle, handle->rx_buffer,
            handle->rx_buffer_size, config->interrupt_priority);
    }
    if (status == A_STATUS_OK) {
        status = aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_IDLE, true);
    }
    return status;
}

void aDevUsartConfigStructInit(aDevUsartConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    aDrvUsartConfigStructInit(&config->drv_config);
    config->mode = ADEV_USART_MODE_POLLING;
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
    handle->mode = ADEV_USART_MODE_POLLING;
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
    handle->idle_event_count = 0U;
    handle->rx_overflow = 0U;
}

aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((config == NULL) || (handle == NULL) ||
        (config->mode > ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE)) {
        return A_STATUS_INVALID_PARAM;
    }

    status = aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    handle->mode = config->mode;

    if (config->mode == ADEV_USART_MODE_DMA_TX) {
        status = dma_mode_init(handle, config);
    } else if (config->mode == ADEV_USART_MODE_INTERRUPT_IDLE) {
        status = interrupt_mode_init(handle, config);
    } else if (config->mode ==
               ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE) {
        status = buffered_tx_dma_rx_init(handle, config);
    }

    if (status != A_STATUS_OK) {
        (void)aDrvUsartDeInitStatic(&handle->drv_handle);
    }
    return status;
}

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    if (handle->mode == ADEV_USART_MODE_DMA_TX) {
        (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
    } else if (handle->mode ==
               ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE) {
        (void)aDrvUsartAsyncRxAbort(&handle->drv_handle);
    }
    return aDrvUsartDeInitStatic(&handle->drv_handle);
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
        bool available;
        aStatus_t status = A_STATUS_OK;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
        if (handle->mode ==
            ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE) {
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

    return (handle->mode == ADEV_USART_MODE_INTERRUPT_IDLE) ||
                   (handle->mode ==
                    ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE)
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
        bool space_available;

        aDrvUsartDisableInterrupt(&handle->drv_handle);
        space_available = handle->tx_count < handle->tx_buffer_size;
        if (space_available) {
            handle->tx_buffer[handle->tx_head] = ((const uint8_t *)data)[count];
            handle->tx_head = (handle->tx_head + 1U) % handle->tx_buffer_size;
            ++handle->tx_count;
        }
        aDrvUsartEnableInterrupt(&handle->drv_handle);

        if (space_available) {
            ++count;
            (void)aDrvUsartSetInterruptEnabled(
                &handle->drv_handle, ADRV_USART_EXTI_TXE, true);
        } else if (aOSPollWaitExpired(&end)) {
            return count != 0U ? (aSSize_t)count
                               : aOSFailWithTimeout(timeout);
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

    switch (handle->mode) {
    case ADEV_USART_MODE_DMA_TX:
        return dma_write(handle, data, data_size, timeout);
    case ADEV_USART_MODE_INTERRUPT_IDLE:
    case ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE:
        return interrupt_write(handle, data, data_size, timeout);
    case ADEV_USART_MODE_POLLING:
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
        bool complete;
        const aStatus_t status = aDrvUsartIsTransmitComplete(
            &handle->drv_handle, &complete);

        if (status != A_STATUS_OK) {
            return status;
        }
        if (complete &&
            ((handle->mode != ADEV_USART_MODE_INTERRUPT_IDLE &&
              handle->mode != ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE) ||
                         handle->tx_count == 0U)) {
            return A_STATUS_OK;
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

bool aDevUsartHasRxOverflowed(const aDevUsartHandle_t *handle)
{
    return (handle != NULL) && (handle->rx_overflow != 0U);
}

void aDevUsartClearRxOverflow(aDevUsartHandle_t *handle)
{
    if (handle != NULL) {
        handle->rx_overflow = 0U;
    }
}
