#include "aDrv_usart.h"

#include "aDrv_dma.h"
#include "aDrv_usart_internal.h"

#define ADRV_USART_DMA_MAX_TRANSFER 65535U

/* GD32E505 固定请求路由。UART3 与 USART5 共用通道，同向不能同时占用。 */
#define ADRV_USART0_TX_DMA_CHANNEL ((aDrvDmaChannel_t)3U)  /* DMA0 CH3 */
#define ADRV_USART0_RX_DMA_CHANNEL ((aDrvDmaChannel_t)4U)  /* DMA0 CH4 */
#define ADRV_UART3_TX_DMA_CHANNEL  ((aDrvDmaChannel_t)11U) /* DMA1 CH4 */
#define ADRV_UART3_RX_DMA_CHANNEL  ((aDrvDmaChannel_t)9U)  /* DMA1 CH2 */
#define ADRV_USART5_TX_DMA_CHANNEL ADRV_UART3_TX_DMA_CHANNEL
#define ADRV_USART5_RX_DMA_CHANNEL ADRV_UART3_RX_DMA_CHANNEL

typedef struct {
    aDrvDmaHandle_t tx_dma;
    aDrvDmaHandle_t rx_dma;
    size_t rx_size;
    aDrvUsartDmaCallback_t rx_callback;
    void *rx_callback_argument;
    aBool_t tx_busy;
    aBool_t rx_busy;
    aBool_t rx_circular;
} aDrvPrivateUsartDmaState_t;

/* 只有三个串口具备当前芯片的 DMA 路由，只为它们保留状态。 */
static aDrvPrivateUsartDmaState_t s_dma_states[3];

static aDrvPrivateUsartDmaState_t *dma_state_get(aDrvUsartId_t id)
{
    switch (id) {
    case ADRV_USART_0: return &s_dma_states[0];
    case ADRV_USART_3: return &s_dma_states[1];
    case ADRV_USART_5: return &s_dma_states[2];
    default: return NULL;
    }
}

static aStatus_t tx_dma_channel_get(aDrvUsartId_t id,
                                    aDrvDmaChannel_t *channel)
{
    if (channel == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    switch (id) {
    case ADRV_USART_0:
        *channel = ADRV_USART0_TX_DMA_CHANNEL;
        return A_STATUS_OK;
    case ADRV_USART_3:
        *channel = ADRV_UART3_TX_DMA_CHANNEL;
        return A_STATUS_OK;
    case ADRV_USART_5:
        *channel = ADRV_USART5_TX_DMA_CHANNEL;
        return A_STATUS_OK;
    case ADRV_USART_1:
    case ADRV_USART_2:
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
        *channel = ADRV_USART0_RX_DMA_CHANNEL;
        return A_STATUS_OK;
    case ADRV_USART_3:
        *channel = ADRV_UART3_RX_DMA_CHANNEL;
        return A_STATUS_OK;
    case ADRV_USART_5:
        *channel = ADRV_USART5_RX_DMA_CHANNEL;
        return A_STATUS_OK;
    case ADRV_USART_1:
    case ADRV_USART_2:
    case ADRV_USART_4:
    default:
        return A_STATUS_UNSUPPORTED;
    }
}

static aStatus_t dma_init_channel(aDrvDmaHandle_t *dma,
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

/* DMA 负责标志、圈数和中断；此处只把通知传给串口设备层。 */
static void rx_dma_event(void *argument, uint32_t events)
{
    aDrvPrivateUsartDmaState_t *state = argument;
    (void)events;
    if (state->rx_busy && state->rx_callback != NULL)
        state->rx_callback(state->rx_callback_argument);
}

static aStatus_t rx_callback_configure(aDrvPrivateUsartDmaState_t *state,
    uint8_t priority, aDrvUsartDmaCallback_t callback, void *argument,
    aBool_t circular)
{
    aDrvDmaInterruptConfig_t config = {
        .callback = rx_dma_event,
        .argument = state,
        .events = ADRV_DMA_EVENT_COMPLETE | ADRV_DMA_EVENT_ERROR,
        .priority = priority,
    };

    state->rx_callback = callback;
    state->rx_callback_argument = argument;
    if (circular) config.events |= ADRV_DMA_EVENT_HALF;
    return aDrvDmaConfigureInterrupt(&state->rx_dma,
                                     callback != NULL ? &config : NULL);
}

static void tx_stop(aDrvUsartHandle_t *handle,
                    aDrvPrivateUsartDmaState_t *state)
{
    (void)aDrvDmaTransDisable(&state->tx_dma);
    usart_dma_transmit_config((uint32_t)handle->instance,
                              USART_TRANSMIT_DMA_DISABLE);
    state->tx_busy = A_FALSE;
    aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_TX);
}

aBool_t aDrvUsartDmaTxIsSupported(aDrvUsartId_t id)
{
    aDrvDmaChannel_t channel;

    return tx_dma_channel_get(id, &channel) == A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncTxStart(aDrvUsartHandle_t *handle,
                                const void *data, size_t size,
                                size_t *started)
{
    aDrvPrivateUsartDmaState_t *state;
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
    if ((handle->interrupt_enabled_mask &
         (1UL << ADRV_USART_EXTI_TXE)) != 0U) {
        return A_STATUS_BUSY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if (state->tx_busy) {
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
    status = dma_init_channel(&state->tx_dma, channel,
                            ADRV_DMA_DIR_MEMORY_TO_PERIPH);
    if (status != A_STATUS_OK) {
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_TX);
        return status;
    }

    transfer_size = size > ADRV_USART_DMA_MAX_TRANSFER
                        ? ADRV_USART_DMA_MAX_TRANSFER
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

    state->tx_busy = A_TRUE;
    *started = transfer_size;
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncTxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining)
{
    aDrvPrivateUsartDmaState_t *state;

    if ((handle == NULL) || (remaining == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_TX) == 0U) ||
        !state->tx_busy) {
        return A_STATUS_NOT_READY;
    }

    aDrvDmaProgress_t progress;
    const aStatus_t status = aDrvDmaGetProgress(&state->tx_dma, &progress);
    if (status == A_STATUS_ERROR) {
        *remaining = progress.remaining;
        tx_stop(handle, state);
        return status;
    }
    if (status != A_STATUS_OK) return status;
    *remaining = progress.remaining;
    if (*remaining == 0U) {
        tx_stop(handle, state);
    }
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncTxAbort(aDrvUsartHandle_t *handle)
{
    aDrvPrivateUsartDmaState_t *state;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if (((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_TX) != 0U) {
        tx_stop(handle, state);
    }
    if (state->tx_dma.initialized != 0U) {
        (void)aDrvDmaDeInitStatic(&state->tx_dma);
        state->tx_busy = A_FALSE;
    }
    return A_STATUS_OK;
}

aBool_t aDrvUsartDmaRxIsSupported(aDrvUsartId_t id)
{
    aDrvDmaChannel_t channel;

    return rx_dma_channel_get(id, &channel) == A_STATUS_OK;
}

static aStatus_t rx_start(aDrvUsartHandle_t *handle,
    void *buffer, size_t size, uint8_t priority,
    aDrvUsartDmaCallback_t callback, void *argument)
{
    aDrvPrivateUsartDmaState_t *state;
    aDrvDmaChannel_t channel;
    aStatus_t status;

    if ((handle == NULL) || (buffer == NULL) || (size == 0U) ||
        (size > ADRV_USART_DMA_MAX_TRANSFER)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if ((handle->interrupt_enabled_mask &
         (1UL << ADRV_USART_EXTI_RXNE)) != 0U) {
        return A_STATUS_BUSY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if (state->rx_busy) {
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
    status = dma_init_channel(&state->rx_dma, channel,
                            ADRV_DMA_DIR_PERIPH_TO_MEMORY);
    if (status == A_STATUS_OK) {
        status = aDrvDmaTransDisable(&state->rx_dma);
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaCircularSet(&state->rx_dma, A_FALSE);
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
        state->rx_size = size;
        state->rx_circular = A_FALSE;
        state->rx_busy = A_TRUE;
        status = rx_callback_configure(state, priority, callback, argument,
                                        A_FALSE);
    }
    if (status == A_STATUS_OK) {
        usart_dma_receive_config((uint32_t)handle->instance,
                                 USART_RECEIVE_DMA_ENABLE);
        status = aDrvDmaTransEnable(&state->rx_dma);
    }
    if (status != A_STATUS_OK) {
        state->rx_busy = A_FALSE;
        if (state->rx_dma.initialized)
            (void)aDrvDmaConfigureInterrupt(&state->rx_dma, NULL);
        usart_dma_receive_config((uint32_t)handle->instance,
                                 USART_RECEIVE_DMA_DISABLE);
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
        return status;
    }

    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncRxStart(aDrvUsartHandle_t *handle,
                                void *buffer, size_t size)
{
    return rx_start(handle, buffer, size, 0U, NULL, NULL);
}

aStatus_t aDrvUsartRxDmaStart(
    aDrvUsartHandle_t *handle, void *buffer, size_t size,
    uint8_t interrupt_priority, aDrvUsartDmaCallback_t callback,
    void *argument)
{
    if (callback == NULL || interrupt_priority > 15U)
        return A_STATUS_INVALID_PARAM;
    return rx_start(handle, buffer, size, interrupt_priority,
                    callback, argument);
}

aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *handle,
                                        void *buffer, size_t size,
                                        uint8_t interrupt_priority,
                                        aDrvUsartDmaCallback_t callback,
                                        void *argument)
{
    aDrvPrivateUsartDmaState_t *state;
    aDrvDmaChannel_t channel;
    aStatus_t status;

    if ((handle == NULL) || (buffer == NULL) || (size < 2U) ||
        (callback == NULL) ||
        (size > ADRV_USART_DMA_MAX_TRANSFER) ||
        (interrupt_priority > 15U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if ((handle->interrupt_enabled_mask &
         (1UL << ADRV_USART_EXTI_RXNE)) != 0U) {
        return A_STATUS_BUSY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if (state->rx_busy) {
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
    status = dma_init_channel(&state->rx_dma, channel,
                            ADRV_DMA_DIR_PERIPH_TO_MEMORY);
    if (status == A_STATUS_OK) {
        status = aDrvDmaTransDisable(&state->rx_dma);
    }
    if (status == A_STATUS_OK) {
        status = aDrvDmaCircularSet(&state->rx_dma, A_TRUE);
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
    if (status != A_STATUS_OK) {
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
        return status;
    }

    state->rx_size = size;
    state->rx_circular = A_TRUE;
    state->rx_busy = A_TRUE;
    status = rx_callback_configure(state, interrupt_priority, callback,
                                    argument, A_TRUE);
    if (status == A_STATUS_OK) {
        usart_dma_receive_config((uint32_t)handle->instance,
                                 USART_RECEIVE_DMA_ENABLE);
        status = aDrvDmaTransEnable(&state->rx_dma);
    }
    if (status != A_STATUS_OK) {
        (void)aDrvDmaConfigureInterrupt(&state->rx_dma, NULL);
        usart_dma_receive_config((uint32_t)handle->instance,
                                 USART_RECEIVE_DMA_DISABLE);
        state->rx_busy = A_FALSE;
        state->rx_circular = A_FALSE;
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
    }
    return status;
}

aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *handle,
                                           size_t *received)
{
    aDrvUsartRxProgress_t progress;
    aStatus_t status;

    if (received == NULL) return A_STATUS_INVALID_PARAM;
    status = aDrvUsartAsyncRxGetProgress(handle, &progress);
    if (status == A_STATUS_OK || status == A_STATUS_ERROR)
        *received = progress.received;
    return status;
}

aStatus_t aDrvUsartAsyncRxGetProgress(aDrvUsartHandle_t *handle,
                                      aDrvUsartRxProgress_t *progress)
{
    aDrvPrivateUsartDmaState_t *state;
    aDrvDmaProgress_t dma_progress;
    aStatus_t status;

    if ((handle == NULL) || (progress == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) == 0U) ||
        !state->rx_busy || !state->rx_circular) {
        return A_STATUS_NOT_READY;
    }

    status = aDrvDmaGetProgress(&state->rx_dma, &dma_progress);
    if (status == A_STATUS_OK || status == A_STATUS_ERROR) {
        progress->received = dma_progress.transferred;
        progress->position = dma_progress.remaining == 0U ? 0U :
                              state->rx_size - dma_progress.remaining;
    }
    return status;
}

aStatus_t aDrvUsartAsyncRxGetRemaining(aDrvUsartHandle_t *handle,
                                       size_t *remaining)
{
    aDrvPrivateUsartDmaState_t *state;

    if ((handle == NULL) || (remaining == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) == 0U) ||
        !state->rx_busy || state->rx_circular) {
        return A_STATUS_NOT_READY;
    }

    aDrvDmaProgress_t progress;
    const aStatus_t status = aDrvDmaGetProgress(&state->rx_dma, &progress);
    if (status == A_STATUS_OK || status == A_STATUS_ERROR)
        *remaining = progress.remaining;
    return status;
}

aStatus_t aDrvUsartAsyncRxStop(aDrvUsartHandle_t *handle,
                               size_t *received)
{
    aDrvPrivateUsartDmaState_t *state;
    aDrvDmaProgress_t progress;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) == 0U) ||
        !state->rx_busy) {
        return A_STATUS_NOT_READY;
    }

    (void)aDrvDmaTransDisable(&state->rx_dma);
    usart_dma_receive_config((uint32_t)handle->instance,
                             USART_RECEIVE_DMA_DISABLE);
    const aStatus_t status = aDrvDmaGetProgress(&state->rx_dma, &progress);
    (void)aDrvDmaConfigureInterrupt(&state->rx_dma, NULL);
    if (received != NULL &&
        (status == A_STATUS_OK || status == A_STATUS_ERROR)) {
        *received = progress.transferred;
    }
    state->rx_busy = A_FALSE;
    state->rx_circular = A_FALSE;
    aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncRxAbort(aDrvUsartHandle_t *handle)
{
    aDrvPrivateUsartDmaState_t *state;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = dma_state_get(handle->id);
    if (state == NULL) return A_STATUS_UNSUPPORTED;
    if (state->rx_busy) {
        (void)aDrvUsartAsyncRxStop(handle, NULL);
    }
    if (state->rx_dma.initialized != 0U) {
        (void)aDrvDmaDeInitStatic(&state->rx_dma);
        state->rx_size = 0U;
        state->rx_callback = NULL;
        state->rx_callback_argument = NULL;
        state->rx_circular = A_FALSE;
    }
    return A_STATUS_OK;
}
