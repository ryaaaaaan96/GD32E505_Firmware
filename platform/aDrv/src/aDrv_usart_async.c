#include "aDrv_usart.h"

#include "aDrv_dma.h"
#include "aDrv_usart_internal.h"

#define ADRV_USART_ASYNC_MAX_TRANSFER 65535U

/*
 * GD32E505 fixed USART DMA request mapping.
 *
 * UART3 and USART5 share both DMA channels. They may use DMA independently in
 * opposite directions, but the same direction cannot be active at the same
 * time on both peripherals.
 */
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
    volatile size_t rx_wrap_count;
    IRQn_Type rx_dma_irq;
    uint8_t rx_irq_priority;
    aBool_t tx_busy;
    aBool_t rx_busy;
    aBool_t rx_circular;
    volatile aBool_t rx_error;
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

static aBool_t shared_dma_is_busy(aDrvUsartId_t id, aBool_t transmit)
{
    const aDrvPrivateUsartAsyncState_t *other;

    if (id == ADRV_USART_3) {
        other = &s_async_states[ADRV_USART_5];
    } else if (id == ADRV_USART_5) {
        other = &s_async_states[ADRV_USART_3];
    } else {
        return A_FALSE;
    }

    return transmit ? other->tx_busy : other->rx_busy;
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

static IRQn_Type dma_irq_get(const aDrvDmaHandle_t *dma)
{
    if (dma->controller == DMA0) {
        return (IRQn_Type)((int32_t)DMA0_Channel0_IRQn + dma->channel);
    }
    if (dma->controller == DMA1) {
        return (IRQn_Type)((int32_t)DMA1_Channel0_IRQn + dma->channel);
    }
    return (IRQn_Type)-1;
}

static void rx_dma_flags_service(aDrvPrivateUsartAsyncState_t *state)
{
    const uint32_t controller = (uint32_t)state->rx_dma.controller;
    const dma_channel_enum channel =
        (dma_channel_enum)state->rx_dma.channel;

    if (dma_flag_get(controller, channel, DMA_FLAG_FTF) != RESET) {
        dma_flag_clear(controller, channel, DMA_FLAG_FTF);
        ++state->rx_wrap_count;
    }
    if (dma_flag_get(controller, channel, DMA_FLAG_ERR) != RESET) {
        dma_flag_clear(controller, channel, DMA_FLAG_ERR);
        state->rx_error = A_TRUE;
    }
}

static void rx_dma_irq_dispatch(aDrvUsartId_t id)
{
    aDrvPrivateUsartAsyncState_t *state = &s_async_states[id];

    if (!state->rx_busy || !state->rx_circular) {
        nvic_irq_disable(state->rx_dma_irq);
        return;
    }
    rx_dma_flags_service(state);
}

static void tx_stop(aDrvUsartHandle_t *handle,
                    aDrvPrivateUsartAsyncState_t *state)
{
    (void)aDrvDmaTransDisable(&state->tx_dma);
    usart_dma_transmit_config((uint32_t)handle->instance,
                              USART_TRANSMIT_DMA_DISABLE);
    state->tx_busy = A_FALSE;
    aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_TX);
}

aBool_t aDrvUsartAsyncTxIsSupported(const aDrvUsartHandle_t *handle)
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
    if (state->tx_busy || shared_dma_is_busy(handle->id, A_TRUE)) {
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

    state->tx_busy = A_TRUE;
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
        !state->tx_busy) {
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
        state->tx_busy = A_FALSE;
    }
    return A_STATUS_OK;
}

aBool_t aDrvUsartAsyncRxIsSupported(const aDrvUsartHandle_t *handle)
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
    if (state->rx_busy || shared_dma_is_busy(handle->id, A_FALSE)) {
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
    state->rx_wrap_count = 0U;
    state->rx_circular = A_FALSE;
    state->rx_error = A_FALSE;
    state->rx_busy = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDrvUsartAsyncRxCircularStart(aDrvUsartHandle_t *handle,
                                        void *buffer, size_t size,
                                        uint8_t interrupt_priority)
{
    aDrvPrivateUsartAsyncState_t *state;
    aDrvDmaChannel_t channel;
    aStatus_t status;

    if ((handle == NULL) || (buffer == NULL) || (size < 2U) ||
        (size > ADRV_USART_ASYNC_MAX_TRANSFER) ||
        (interrupt_priority > 15U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->callbacks[ADRV_USART_EXTI_RXNE].function != NULL) {
        return A_STATUS_BUSY;
    }

    state = &s_async_states[handle->id];
    if (state->rx_busy || shared_dma_is_busy(handle->id, A_FALSE)) {
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
    state->rx_wrap_count = 0U;
    state->rx_dma_irq = dma_irq_get(&state->rx_dma);
    state->rx_irq_priority = interrupt_priority;
    state->rx_circular = A_TRUE;
    state->rx_error = A_FALSE;
    state->rx_busy = A_TRUE;

    dma_flag_clear((uint32_t)state->rx_dma.controller,
                   (dma_channel_enum)state->rx_dma.channel, DMA_FLAG_G);
    dma_interrupt_enable((uint32_t)state->rx_dma.controller,
                         (dma_channel_enum)state->rx_dma.channel,
                         DMA_INT_FTF | DMA_INT_ERR);
    nvic_irq_enable(state->rx_dma_irq, interrupt_priority, 0U);
    usart_dma_receive_config((uint32_t)handle->instance,
                             USART_RECEIVE_DMA_ENABLE);
    status = aDrvDmaTransEnable(&state->rx_dma);
    if (status != A_STATUS_OK) {
        dma_interrupt_disable((uint32_t)state->rx_dma.controller,
                              (dma_channel_enum)state->rx_dma.channel,
                              DMA_INT_FTF | DMA_INT_ERR);
        nvic_irq_disable(state->rx_dma_irq);
        state->rx_busy = A_FALSE;
        state->rx_circular = A_FALSE;
        aDrvPrivateUsartOwnerRelease(handle, ADRV_USART_OWNER_ASYNC_RX);
    }
    return status;
}

aStatus_t aDrvUsartAsyncRxGetReceivedCount(aDrvUsartHandle_t *handle,
                                           size_t *received)
{
    aDrvPrivateUsartAsyncState_t *state;
    size_t remaining;
    aBool_t error;

    if ((handle == NULL) || (received == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    state = &s_async_states[handle->id];
    if ((((uint32_t)handle->owner & ADRV_USART_OWNER_ASYNC_RX) == 0U) ||
        !state->rx_busy || !state->rx_circular) {
        return A_STATUS_NOT_READY;
    }

    nvic_irq_disable(state->rx_dma_irq);
    rx_dma_flags_service(state);
    remaining = (size_t)aDrvDmaCurLenGet(&state->rx_dma);
    *received = state->rx_wrap_count * state->rx_size +
                (state->rx_size - remaining);
    error = state->rx_error;
    nvic_irq_enable(state->rx_dma_irq, state->rx_irq_priority, 0U);

    return error != 0U ? A_STATUS_ERROR : A_STATUS_OK;
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
        !state->rx_busy) {
        return A_STATUS_NOT_READY;
    }

    (void)aDrvDmaTransDisable(&state->rx_dma);
    usart_dma_receive_config((uint32_t)handle->instance,
                             USART_RECEIVE_DMA_DISABLE);
    remaining = (size_t)aDrvDmaCurLenGet(&state->rx_dma);
    if (state->rx_circular) {
        nvic_irq_disable(state->rx_dma_irq);
        rx_dma_flags_service(state);
        dma_interrupt_disable((uint32_t)state->rx_dma.controller,
                              (dma_channel_enum)state->rx_dma.channel,
                              DMA_INT_FTF | DMA_INT_ERR);
    }
    if (received != NULL) {
        *received = state->rx_wrap_count * state->rx_size +
                    (state->rx_size - remaining);
    }
    state->rx_busy = A_FALSE;
    state->rx_circular = A_FALSE;
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
    if (state->rx_busy) {
        (void)aDrvUsartAsyncRxStop(handle, NULL);
    }
    if (state->rx_dma.initialized != 0U) {
        (void)aDrvDmaDeInitStatic(&state->rx_dma);
        state->rx_size = 0U;
        state->rx_wrap_count = 0U;
        state->rx_irq_priority = 0U;
        state->rx_circular = A_FALSE;
        state->rx_error = A_FALSE;
    }
    return A_STATUS_OK;
}

void DMA0_Channel4_IRQHandler(void)
{
    rx_dma_irq_dispatch(ADRV_USART_0);
}

void DMA1_Channel2_IRQHandler(void)
{
    if ((s_async_states[ADRV_USART_3].rx_busy != 0U) &&
        (s_async_states[ADRV_USART_3].rx_circular != 0U)) {
        rx_dma_irq_dispatch(ADRV_USART_3);
    } else if ((s_async_states[ADRV_USART_5].rx_busy != 0U) &&
               (s_async_states[ADRV_USART_5].rx_circular != 0U)) {
        rx_dma_irq_dispatch(ADRV_USART_5);
    } else {
        nvic_irq_disable(DMA1_Channel2_IRQn);
    }
}
