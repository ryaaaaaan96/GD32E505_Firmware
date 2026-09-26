#include "aDev_usart_internal.h"

#include "aOS.h"

#include <limits.h>

static aSSize_t fail_with_wait_status(aStatus_t status,
                                      aTimeout_t timeout)
{
    return ((status == A_STATUS_BUSY) || (status == A_STATUS_TIMEOUT))
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

static void direct_tx_interrupts_disable(aDevUsartHandle_t *handle)
{
    const aDevUsartMode_t tx_mode = handle->mode & ADEV_USART_TX_MASK;

    if (tx_mode == ADEV_USART_TX_INTERRUPT_BUFFERED) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TXE, A_FALSE);
    }
    if ((tx_mode == ADEV_USART_TX_INTERRUPT_BUFFERED) ||
        (tx_mode == ADEV_USART_TX_DMA_BUFFERED) || handle->rs485.enabled) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_TC, A_FALSE);
    }
}

aSSize_t aDevUsartWriteDirect(aDevUsartHandle_t *handle,
                              const void *data, size_t data_size,
                              aTimeout_t timeout)
{
    aTimepoint_t end;
    aStatus_t status;
    size_t count = 0U;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (data_size > (size_t)PTRDIFF_MAX) ||
        ((data == NULL) && (data_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (!handle->drv_handle.initialized) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (data_size == 0U) {
        return 0;
    }
    if (!aDevUsartIsSupported(handle, ADEV_USART_CAP_TX_DIRECT)) {
        return aOSFailWithStatus(A_STATUS_UNSUPPORTED);
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(
        handle->tx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->tx_queue_owner != NULL) ||
        (handle->tx_callback != NULL) ||
        (handle->tx_count != 0U) || (handle->tx_dma_active != 0U)) {
        aDrvUsartEnableInterrupt(&handle->drv_handle);
        (void)aOSMutexUnlock(handle->tx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }
    handle->tx_state = ADEV_USART_TX_DIRECT;
    direct_tx_interrupts_disable(handle);
    status = handle->tx_error;
    if (status == A_STATUS_OK) {
#if ADEV_USART_HAS_RS485
        status = aDevUsartRS485Begin(handle);
#endif
    }
    aDrvUsartEnableInterrupt(&handle->drv_handle);

    while ((status == A_STATUS_OK) && (count < data_size)) {
        size_t started = 0U;
        size_t remaining = 0U;

        status = aDrvUsartAsyncTxStart(
            &handle->drv_handle, (const uint8_t *)data + count,
            data_size - count, &started);
        if (status != A_STATUS_OK) break;

        for (;;) {
            status = aDrvUsartAsyncTxGetRemaining(
                &handle->drv_handle, &remaining);
            if (status != A_STATUS_OK) {
                count += started - remaining;
                break;
            }
            if (remaining == 0U) {
                count += started;
                break;
            }
            if (aOSPollWaitExpired(&end)) {
                (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
                count += started - remaining;
                status = A_STATUS_TIMEOUT;
                break;
            }
        }
        if (status != A_STATUS_OK) break;
    }

    /* Return only after DMA has stopped accessing the caller's buffer. */
    (void)aDrvUsartAsyncTxAbort(&handle->drv_handle);
    aDrvUsartDisableInterrupt(&handle->drv_handle);
#if ADEV_USART_HAS_RS485
    aDevUsartRs485ArmComplete(handle);
#endif
    handle->tx_state = ADEV_USART_TX_IDLE;
    aDrvUsartEnableInterrupt(&handle->drv_handle);
    (void)aOSMutexUnlock(handle->tx_mutex);
    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}

static void direct_rx_interrupt_set(aDevUsartHandle_t *handle,
                                    aBool_t enabled)
{
    if ((handle->mode & ADEV_USART_RX_MASK) ==
        ADEV_USART_RX_INTERRUPT_BUFFERED) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_RXNE, enabled);
    }
}

aSSize_t aDevUsartReadDirect(aDevUsartHandle_t *handle, void *buffer,
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
    status = aOSMutexLock(
        handle->rx_mutex,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) {
        return fail_with_wait_status(status, timeout);
    }

    aDrvUsartDisableInterrupt(&handle->drv_handle);
    if ((handle->rx_state != ADEV_USART_RX_IDLE) ||
        (handle->rx_request_head != NULL) ||
        (handle->rx_complete_head != NULL) ||
        ((handle->mode & ADEV_USART_RX_MASK) ==
         ADEV_USART_RX_DMA_BUFFERED) ||
        (handle->rx_count != 0U)) {
        aDrvUsartEnableInterrupt(&handle->drv_handle);
        (void)aOSMutexUnlock(handle->rx_mutex);
        return aOSFailWithStatus(A_STATUS_BUSY);
    }
    handle->rx_state = ADEV_USART_RX_DIRECT;
    direct_rx_interrupt_set(handle, A_FALSE);
    aDrvUsartEnableInterrupt(&handle->drv_handle);

    while (count < buffer_size) {
        size_t transfer_size = buffer_size - count;
        size_t remaining;
        size_t received = 0U;

        if (transfer_size > 65535U) transfer_size = 65535U;
        status = aDrvUsartAsyncRxStart(
            &handle->drv_handle, (uint8_t *)buffer + count, transfer_size);
        if (status != A_STATUS_OK) break;

        for (;;) {
            status = aDrvUsartAsyncRxGetRemaining(
                &handle->drv_handle, &remaining);
            if ((status != A_STATUS_OK) || (remaining == 0U) ||
                aOSPollWaitExpired(&end)) {
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
                } else {
                    count += received;
                }
                if (expired && status == A_STATUS_OK) status = A_STATUS_TIMEOUT;
                if (status == A_STATUS_OK && received == 0U) status = A_STATUS_ERROR;
                break;
            }
        }
        if (status != A_STATUS_OK) break;
        if (count < buffer_size && aTimepointExpired(&end, aOSGetUptimeMs())) {
            status = A_STATUS_TIMEOUT;
            break;
        }
    }

    direct_rx_interrupt_set(handle, A_TRUE);
    handle->rx_state = ADEV_USART_RX_IDLE;
    (void)aOSMutexUnlock(handle->rx_mutex);
    if (count != 0U) return (aSSize_t)count;
    return status == A_STATUS_TIMEOUT
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}
