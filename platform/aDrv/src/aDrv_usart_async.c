#include "aDrv_usart.h"

#include "aDrv_dma.h"
#include "aDrv_usart_internal.h"

#define ADRV_USART_ASYNC_MAX_TRANSFER 65535U

typedef struct {
    aDrvDmaHandle_t dma;
    uint8_t busy;
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

static aStatus_t async_dma_init(aDrvUsartId_t id,
                                aDrvPrivateUsartAsyncState_t *state)
{
    aDrvDmaConfig_t config;
    aDrvDmaChannel_t channel;
    aStatus_t status;

    if (state->dma.initialized != 0U) {
        return A_STATUS_OK;
    }

    status = tx_dma_channel_get(id, &channel);
    if (status != A_STATUS_OK) {
        return status;
    }

    aDrvDmaHandleStructInit(&state->dma);
    aDrvDmaConfigStructInit(&config);
    config.channel = channel;
    config.direction = ADRV_DMA_DIR_MEMORY_TO_PERIPH;
    config.priority = ADRV_DMA_PRIORITY_HIGH;
    return aDrvDmaInitStatic(&config, &state->dma);
}

static void tx_stop(aDrvUsartHandle_t *handle,
                    aDrvPrivateUsartAsyncState_t *state)
{
    (void)aDrvDmaTransDisable(&state->dma);
    usart_dma_transmit_config((uint32_t)handle->instance,
                              USART_TRANSMIT_DMA_DISABLE);
    state->busy = 0U;
    aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC);
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
    size_t transfer_size;
    aStatus_t status;

    if ((handle == NULL) || (data == NULL) || (size == 0U) ||
        (started == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = &s_async_states[handle->id];
    if (state->busy != 0U) {
        return A_STATUS_BUSY;
    }

    status = aDrvPrivateUsartOwnerAcquire(handle, ADRV_USART_OWNER_ASYNC);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = async_dma_init(handle->id, state);
    if (status != A_STATUS_OK) {
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC);
        return status;
    }

    transfer_size = size > ADRV_USART_ASYNC_MAX_TRANSFER
                        ? ADRV_USART_ASYNC_MAX_TRANSFER
                        : size;
    status = aDrvDmaTransDisable(&state->dma);
    if (status == A_STATUS_OK) {
        status = aDrvDmaSrcBufferSet(&state->dma, data);
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaDstBufferSet(
            &state->dma,
            (void *)(uintptr_t)&USART_DATA((uint32_t)handle->instance));
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaDstBufferLen(&state->dma,
                                     (uint32_t)transfer_size);
    }
    if (status == A_STATUS_OK) {
        usart_dma_transmit_config((uint32_t)handle->instance,
                                  USART_TRANSMIT_DMA_ENABLE);
        status = aDrvDmaTransEnable(&state->dma);
    }
    if (status != A_STATUS_OK) {
        tx_stop(handle, state);
        return status;
    }

    state->busy = 1U;
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
    if ((handle->owner != ADRV_USART_OWNER_ASYNC) ||
        (state->busy == 0U)) {
        return A_STATUS_NOT_READY;
    }

    *remaining = (size_t)aDrvDmaCurLenGet(&state->dma);
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
    if (handle->owner == ADRV_USART_OWNER_ASYNC) {
        tx_stop(handle, state);
    }
    if (state->dma.initialized != 0U) {
        (void)aDrvDmaDeInitStatic(&state->dma);
        state->busy = 0U;
    }
    return A_STATUS_OK;
}
