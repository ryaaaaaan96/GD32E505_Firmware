#include "aDrv_usart.h"

#include "aDrv_dma.h"
#include "aDrv_usart_internal.h"

#define ADRV_USART_ASYNC_MAX_TRANSFER 65535U

typedef struct {
    aDrvDmaHandle_t tx_dma;
    aDrvDmaHandle_t rx_dma;
    size_t rx_size;
    uint8_t tx_busy;
    uint8_t rx_busy;
} aDrvPrivateUsartAsyncState_t;

static aDrvPrivateUsartAsyncState_t s_async_states[ADRV_USART_5 + 1U];

static aStatus_t tx_dma_channel_get(aDrvUsartId_t id,
                                    aDrvDmaChannel_t *channel)
{
    if (channel == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    switch (id) {
    case ADRV_USART_0:
        *channel = (aDrvDmaChannel_t)3U;  /* DMA0 Channel 3 */
        return A_STATUS_OK;
    case ADRV_USART_5:
        *channel = (aDrvDmaChannel_t)11U; /* DMA1 Channel 4 */
        return A_STATUS_OK;
    case ADRV_USART_1:
    case ADRV_USART_2:
    case ADRV_USART_3:
    case ADRV_USART_4:
    default:
        return A_STATUS_UNSUPPORTED;
    }
}

static aStatus_t rx_dma_channel_get(aDrvUsartId_t id,
                                    aDrvDmaChannel_t *channel)
{
    if (channel == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    switch (id) {
    case ADRV_USART_0:
        *channel = (aDrvDmaChannel_t)4U; /* DMA0 Channel 4 */
        return A_STATUS_OK;
    case ADRV_USART_5:
        *channel = (aDrvDmaChannel_t)9U; /* DMA1 Channel 2 */
        return A_STATUS_OK;
    case ADRV_USART_1:
    case ADRV_USART_2:
    case ADRV_USART_3:
    case ADRV_USART_4:
    default:
        return A_STATUS_UNSUPPORTED;
    }
}

static aStatus_t async_dma_init(aDrvDmaHandle_t *dma,
                                aDrvDmaChannel_t channel,
                                aDrvDmaDirection_t direction)
{
    aDrvDmaConfig_t config;

    if (dma->initialized != 0U) {
        return A_STATUS_OK;
    }

    aDrvDmaHandleStructInit(dma);
    aDrvDmaConfigStructInit(&config);
    config.channel = channel;
    config.direction = direction;
    config.priority = ADRV_DMA_PRIORITY_HIGH;
    return aDrvDmaInitStatic(&config, dma);
}

static void tx_stop(aDrvUsartHandle_t *handle,
                    aDrvPrivateUsartAsyncState_t *state)
{
    (void)aDrvDmaTransDisable(&state->tx_dma);
    usart_dma_transmit_config((uint32_t)handle->instance,
                              USART_TRANSMIT_DMA_DISABLE);
    state->tx_busy = 0U;
    aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_TX);
}

bool aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *handle)
{
    aDrvDmaChannel_t channel;

    return (handle != NULL) && (handle->initialized != 0U) &&
           (tx_dma_channel_get(handle->id, &channel) == A_STATUS_OK);
}

aStatus_t aDrvUsartAsyncTxStart(aDrvUsartHandle_t *handle,
                                const void *data, size_t size,
                                size_t *started)
{
    aDrvPrivateUsartAsyncState_t *state;
    aDrvDmaChannel_t channel;
    size_t transfer_size;
    aStatus_t status;

    if ((handle == NULL) || (data == NULL) || (size == 0U) ||
        (started == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->callbacks[ADRV_USART_EXTI_TXE].function != NULL) {
        return A_STATUS_BUSY;
    }

    state = &s_async_states[handle->id];
    if (state->tx_busy != 0U) {
        return A_STATUS_BUSY;
    }

    status = tx_dma_channel_get(handle->id, &channel);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = aDrvPrivateUsartOwnerAcquire(
        handle, ADRV_USART_OWNER_ASYNC_TX);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = async_dma_init(&state->tx_dma, channel,
                            ADRV_DMA_DIR_MEMORY_TO_PERIPH);
    if (status != A_STATUS_OK) {
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_TX);
        return status;
    }

    transfer_size = size > ADRV_USART_ASYNC_MAX_TRANSFER
                        ? ADRV_USART_ASYNC_MAX_TRANSFER
                        : size;
    status = aDrvDmaTransDisable(&state->tx_dma);
    if (status == A_STATUS_OK) {
        status = aDrvDmaSrcBufferSet(&state->tx_dma, data);
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaDstBufferSet(
            &state->tx_dma,
            (void *)(uintptr_t)&USART_DATA((uint32_t)handle->instance));
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaDstBufferLen(&state->tx_dma,
                                     (uint32_t)transfer_size);
    }
    if (status == A_STATUS_OK) {
        usart_dma_transmit_config((uint32_t)handle->instance,
                                  USART_TRANSMIT_DMA_ENABLE);
        status = aDrvDmaTransEnable(&state->tx_dma);
    }
    if (status != A_STATUS_OK) {
        tx_stop(handle, state);
        return status;
    }

    state->tx_busy = 1U;
    *started = transfer_size;
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncTxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining)
{
    aDrvPrivateUsartAsyncState_t *state;

    if ((handle == NULL) || (remaining == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = &s_async_states[handle->id];
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_TX) == 0U) ||
        (state->tx_busy == 0U)) {
        return A_STATUS_NOT_READY;
    }

    *remaining = (size_t)aDrvDmaCurLenGet(&state->tx_dma);
    if (*remaining == 0U) {
        tx_stop(handle, state);
    }
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncTxAbort(aDrvUsartHandle_t *handle)
{
    aDrvPrivateUsartAsyncState_t *state;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = &s_async_states[handle->id];
    if (((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_TX) != 0U) {
        tx_stop(handle, state);
    }
    if (state->tx_dma.initialized != 0U) {
        (void)aDrvDmaDeInitStatic(&state->tx_dma);
        state->tx_busy = 0U;
    }
    return A_STATUS_OK;
}

bool aDrvUsartAsyncRxIsSupported(const aDrvUsartHandle_t *handle)
{
    aDrvDmaChannel_t channel;

    return (handle != NULL) && (handle->initialized != 0U) &&
           (rx_dma_channel_get(handle->id, &channel) == A_STATUS_OK);
}

aStatus_t aDrvUsartAsyncRxStart(aDrvUsartHandle_t *handle,
                                void *buffer, size_t size)
{
    aDrvPrivateUsartAsyncState_t *state;
    aDrvDmaChannel_t channel;
    aStatus_t status;

    if ((handle == NULL) || (buffer == NULL) || (size == 0U) ||
        (size > ADRV_USART_ASYNC_MAX_TRANSFER)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->callbacks[ADRV_USART_EXTI_RXNE].function != NULL) {
        return A_STATUS_BUSY;
    }

    state = &s_async_states[handle->id];
    if (state->rx_busy != 0U) {
        return A_STATUS_BUSY;
    }

    status = rx_dma_channel_get(handle->id, &channel);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = aDrvPrivateUsartOwnerAcquire(
        handle, ADRV_USART_OWNER_ASYNC_RX);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = async_dma_init(&state->rx_dma, channel,
                            ADRV_DMA_DIR_PERIPH_TO_MEMORY);
    if (status == A_STATUS_OK) {
        status = aDrvDmaTransDisable(&state->rx_dma);
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaSrcBufferSet(&state->rx_dma, buffer);
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaDstBufferSet(
            &state->rx_dma,
            (void *)(uintptr_t)&USART_DATA((uint32_t)handle->instance));
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaDstBufferLen(&state->rx_dma, (uint32_t)size);
    }
    if (status == A_STATUS_OK) {
        usart_dma_receive_config((uint32_t)handle->instance,
                                 USART_RECEIVE_DMA_ENABLE);
        status = aDrvDmaTransEnable(&state->rx_dma);
    }
    if (status != A_STATUS_OK) {
        usart_dma_receive_config((uint32_t)handle->instance,
                                 USART_RECEIVE_DMA_DISABLE);
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
        return status;
    }

    state->rx_size = size;
    state->rx_busy = 1U;
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncRxStop(aDrvUsartHandle_t *handle,
                               size_t *received)
{
    aDrvPrivateUsartAsyncState_t *state;
    size_t remaining;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = &s_async_states[handle->id];
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) == 0U) ||
        (state->rx_busy == 0U)) {
        return A_STATUS_NOT_READY;
    }

    (void)aDrvDmaTransDisable(&state->rx_dma);
    usart_dma_receive_config((uint32_t)handle->instance,
                             USART_RECEIVE_DMA_DISABLE);
    remaining = (size_t)aDrvDmaCurLenGet(&state->rx_dma);
    if (received != NULL) {
        *received = state->rx_size - remaining;
    }
    state->rx_busy = 0U;
    aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncRxAbort(aDrvUsartHandle_t *handle)
{
    aDrvPrivateUsartAsyncState_t *state;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = &s_async_states[handle->id];
    if (state->rx_busy != 0U) {
        (void)aDrvUsartAsyncRxStop(handle, NULL);
    }
    if (state->rx_dma.initialized != 0U) {
        (void)aDrvDmaDeInitStatic(&state->rx_dma);
        state->rx_size = 0U;
    }
    return A_STATUS_OK;
}
