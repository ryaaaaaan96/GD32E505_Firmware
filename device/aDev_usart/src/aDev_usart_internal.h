#ifndef ADEV_USART_INTERNAL_H
#define ADEV_USART_INTERNAL_H

#include "aDev_usart_instance.h"
#include "aOS.h"

#include <stdatomic.h>
#define ADEV_USART_DMA_BACKEND_ENABLE (ADRV_USART_DMA_ENABLE && \
    ADRV_USART_INTERRUPT_ENABLE)
#define ADEV_USART_NEEDS_IRQ (ADEV_USART_INTERRUPT_ENABLE || \
    ADEV_USART_DMA_BACKEND_ENABLE || ADEV_USART_RS485_ENABLE)

/* Begin/Complete 仅由 USART ISR 或屏蔽 USART IRQ 的 TX 路径调用。 */
aStatus_t aDevUsartRS485Init(aDevUsartHandle_t *handle,
                           const aDevUsartRS485Config_t *config);
aStatus_t aDevUsartRS485DeInit(aDevUsartHandle_t *handle);
aStatus_t aDevUsartRS485Begin(aDevUsartHandle_t *handle);
aStatus_t aDevUsartRS485Complete(aDevUsartHandle_t *handle);


void aDevUsartRxDmaComplete(void *argument);
void aDevUsartRxDmaNotifyFromISR(aDevUsartHandle_t *handle);
void aDevUsartAsyncTxTimeout(void *argument);
void aDevUsartAsyncTxDispatchFromISR(aDevUsartHandle_t *handle);
void aDevUsartAsyncRxCancelFromISR(aDevUsartHandle_t *handle);
aStatus_t aDevUsartRegisterIrqCallback(
    aDevUsartHandle_t *handle, aDrvUsartExti_t trigger,
    aDrvInterruptCallback_t callback, uint8_t priority, aBool_t enabled);
aStatus_t aDevUsartRxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config);
aStatus_t aDevUsartDmaRxRefresh(aDevUsartHandle_t *handle);
aStatus_t aDevUsartDmaRxCopy(aDevUsartHandle_t *handle, void *buffer,
                             size_t capacity, size_t *copied);
void aDevUsartRs485ArmComplete(aDevUsartHandle_t *handle);

aStatus_t aDevUsartTxModeInit(aDevUsartHandle_t *handle,
                              const aDevUsartConfig_t *config);

#if ADEV_USART_DMA_BACKEND_ENABLE || ADEV_USART_INTERRUPT_ENABLE
static inline aStatus_t wait_for_event(void *wait_object,
                                const aTimepoint_t *end)
{
    return aOSWaitObjectWait(
        wait_object, aTimepointRemaining(end, aOSGetUptimeMs()));
}

#endif

static inline aSSize_t fail_with_wait_status(aStatus_t status,
                                      aTimeout_t timeout)
{
    return ((status == A_STATUS_BUSY) ||
            (status == A_STATUS_TIMEOUT))
               ? aOSFailWithTimeout(timeout)
               : aOSFailWithStatus(status);
}


#if ADEV_USART_DMA_BACKEND_ENABLE
/* TC wakes TX and DMA completion/error wakes RX. A bounded sleeping check
 * also detects TX DMA faults on drivers without a DMA-error IRQ callback. */
static inline aStatus_t direct_wait(
    aOSWaitObject_t object,
    const aTimepoint_t *end,
    aBool_t check_tx_error)
{
    aTimeout_t remaining = aTimepointRemaining(end, aOSGetUptimeMs());
    if (aTimepointExpired(end, aOSGetUptimeMs())) return A_STATUS_TIMEOUT;
    if (check_tx_error && (remaining.type == A_TIMEOUT_TYPE_FOREVER ||
                          remaining.milliseconds > 10U))
        remaining = A_TIMEOUT_MS(10U);
    const aStatus_t status = aOSWaitObjectWait(object, remaining);
    if (status == A_STATUS_TIMEOUT && !aTimepointExpired(end, aOSGetUptimeMs()))
        return A_STATUS_OK;
    return status;
}

#endif

#endif
