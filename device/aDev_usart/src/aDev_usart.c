#include "aDev_usart_internal.h"

#include <limits.h>
#include <string.h>

#if ADEV_USART_NEEDS_IRQ
aStatus_t aDevUsartRegisterIrqCallback(aDevUsartHandle_t *handle,
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

#endif

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
    case ADEV_USART_TX_DMA_BUFFERED:
        break;
    default:
        return A_FALSE;
    }

    switch (rx_mode) {
    case ADEV_USART_RX_POLLING:
    case ADEV_USART_RX_INTERRUPT_BUFFERED:
    case ADEV_USART_RX_DMA_BUFFERED:
        return A_TRUE;
    default:
        return A_FALSE;
    }
}

static aStatus_t wait_objects_create(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((handle->mode & ADEV_USART_TX_MASK) != ADEV_USART_TX_POLLING) {
        status = aOSWaitObjectCreate(&handle->tx_wait_object);
        if (status != A_STATUS_OK) {
            return status;
        }
    }

    if ((handle->mode & ADEV_USART_RX_MASK) != ADEV_USART_RX_POLLING) {
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

static aStatus_t mutexes_create(aDevUsartHandle_t *handle)
{
    aStatus_t status = aOSMutexCreate(&handle->tx_mutex);

    if (status != A_STATUS_OK) {
        return status;
    }
    status = aOSMutexCreate(&handle->rx_mutex);
    if (status != A_STATUS_OK) {
        aOSMutexDestroy(&handle->tx_mutex);
        return status;
    }
    return status;
}

static void mutexes_destroy(aDevUsartHandle_t *handle)
{
    aOSMutexDestroy(&handle->rx_mutex);
    aOSMutexDestroy(&handle->tx_mutex);
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
    config->rs485.enabled = A_FALSE;
    config->rs485.de_pin = ADRV_PIN_NONE;
    config->rs485.de_active_level = ADRV_GPIO_HIGH;
}

static void handle_struct_init(aDevUsartHandle_t *handle, aBool_t dynamic)
{
    memset(handle, 0, sizeof(*handle));
    aDrvUsartHandleStructInit(&handle->drv_handle);
    aDrvGpioHandleStructInit(&handle->de_gpio);
    handle->mode = ADEV_USART_TX_POLLING | ADEV_USART_RX_POLLING;
    atomic_init(&handle->tx_completion_claimed, A_TRUE);
    handle->dynamic_storage = dynamic;
}

static aStatus_t handle_init(const aDevUsartConfig_t *config,
                             aDevUsartHandle_t *handle)
{
    aStatus_t status;
    if ((config == NULL) || !mode_is_valid(config->mode)) {
        return A_STATUS_INVALID_PARAM;
    }
    /* Product capabilities are checked before allocating or touching hardware. */
    const aDevUsartMode_t tx = config->mode & ADEV_USART_TX_MASK;
    const aDevUsartMode_t rx = config->mode & ADEV_USART_RX_MASK;
    if (((tx == ADEV_USART_TX_INTERRUPT_BUFFERED) &&
         !ADEV_USART_INTERRUPT_ENABLE) ||
        ((tx == ADEV_USART_TX_DMA_BUFFERED) && !ADEV_USART_DMA_BACKEND_ENABLE) ||
        ((rx == ADEV_USART_RX_INTERRUPT_BUFFERED) &&
         !ADEV_USART_INTERRUPT_ENABLE) ||
        ((rx == ADEV_USART_RX_DMA_BUFFERED) &&
         !ADEV_USART_DMA_BACKEND_ENABLE) ||
        ((config->mode & ADEV_USART_OPTION_RX_IDLE) &&
         !ADEV_USART_INTERRUPT_ENABLE) ||
        (config->rs485.enabled && !ADEV_USART_RS485_ENABLE)) {
        return A_STATUS_UNSUPPORTED;
    }
    if (aOSValidateIsrPriority(config->interrupt_priority) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }

    if (config->rs485.enabled &&
        ((config->rs485.de_pin == config->drv_config.tx_pin) ||
         (config->rs485.de_pin == config->drv_config.rx_pin))) {
        return A_STATUS_INVALID_PARAM;
    }

    status = aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    handle->mode = config->mode;
    handle->interrupt_priority = config->interrupt_priority;

#if ADEV_USART_DMA_BACKEND_ENABLE
    if (((config->mode & ADEV_USART_TX_MASK) == ADEV_USART_TX_DMA_BUFFERED &&
         !aDrvUsartAsyncTxIsSupported(&handle->drv_handle)) ||
        ((config->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED &&
         !aDrvUsartAsyncRxIsSupported(&handle->drv_handle))) {
        (void)aDrvUsartDeInitStatic(&handle->drv_handle);
        return A_STATUS_UNSUPPORTED;
    }
#endif

#if ADEV_USART_RS485_ENABLE
    status = aDevUsartRS485Init(handle, &config->rs485);
#endif
    if (status == A_STATUS_OK) {
        status = mutexes_create(handle);
    }
    if (status == A_STATUS_OK) {
        status = wait_objects_create(handle);
    }
    if (status == A_STATUS_OK) {
        status = aDevUsartTxModeInit(handle, config);
    }
    if (status == A_STATUS_OK) {
        status = aDevUsartRxModeInit(handle, config);
    }

    if (status != A_STATUS_OK) {
        (void)aDrvUsartDeInitStatic(&handle->drv_handle);
#if ADEV_USART_RS485_ENABLE
        (void)aDevUsartRS485DeInit(handle);
#endif
        wait_objects_destroy(handle);
        mutexes_destroy(handle);
        const aBool_t dynamic = handle->dynamic_storage;
        handle_struct_init(handle, dynamic);
    }
    return status;
}

aStatus_t aDevUsartInitStatic(const aDevUsartConfig_t *config,
                              aDevUsartStorage_t *storage,
                              aDevUsartHandle_t **handle_out)
{
    aDevUsartHandle_t *handle;
    aStatus_t status;

    if ((config == NULL) || (storage == NULL) || (handle_out == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    handle = (aDevUsartHandle_t *)(void *)storage->bytes;
    handle_struct_init(handle, A_FALSE);
    status = handle_init(config, handle);
    if (status == A_STATUS_OK) {
        *handle_out = handle;
    }
    return status;
}

aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                          aDevUsartHandle_t **handle_out)
{
    aDevUsartHandle_t *handle;
    aStatus_t status;

    if ((config == NULL) || (handle_out == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    *handle_out = NULL;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) {
        return A_STATUS_NO_MEMORY;
    }
    handle_struct_init(handle, A_TRUE);
    status = handle_init(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    /* A worker must never wait on another item in its own serialized queue. */
    if (handle->rx_dispatching) return A_STATUS_BUSY;

    if (!handle->drv_handle.initialized) {
        return A_STATUS_NOT_READY;
    }
    if ((handle->tx_state != ADEV_USART_TX_IDLE) ||
        (handle->rx_state != ADEV_USART_RX_IDLE) ||
        handle->rx_dispatching ||
        handle->rs485_transmitting ||
        ((handle->tx_count != 0U) && (handle->tx_error == A_STATUS_OK)) ||
        (handle->tx_dma_active != 0U)) {
        return A_STATUS_BUSY;
    }

#if ADEV_USART_DMA_BACKEND_ENABLE
    if (handle->rx_dma_active) {
        (void)aDrvUsartSetInterruptEnabled(
            &handle->drv_handle, ADRV_USART_EXTI_IDLE, A_FALSE);
        status = aDrvUsartAsyncRxAbort(&handle->drv_handle);
        if (status != A_STATUS_OK) return status;
        handle->rx_dma_active = A_FALSE;
    }

#endif

    /* First quiesce timer callbacks, then drain any work they submitted. */
    status = aOSTimerDestroy(&handle->tx_deadline_timer);
    if (status != A_STATUS_OK) return status;

    status = aDrvUsartDeInitStatic(&handle->drv_handle);
    if (status == A_STATUS_OK) {
#if ADEV_USART_RS485_ENABLE
        status = aDevUsartRS485DeInit(handle);
#endif
        wait_objects_destroy(handle);
        mutexes_destroy(handle);
        const aBool_t dynamic = handle->dynamic_storage;
        handle_struct_init(handle, dynamic);
    }
    return status;
}

aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
{
    aStatus_t status;

    if ((handle == NULL) || !handle->dynamic_storage) {
        return A_STATUS_INVALID_PARAM;
    }
    status = handle->drv_handle.initialized
                 ? aDevUsartDeInit(handle)
                 : A_STATUS_OK;
    if (status == A_STATUS_OK) {
        aOSFree(handle);
    }
    return status;
}

aBool_t aDevUsartIsSupported(const aDevUsartHandle_t *handle,
                             aDevUsartCapability_t capability)
{
    if ((handle == NULL) || !handle->drv_handle.initialized) {
        return A_FALSE;
    }

#if ADEV_USART_DIRECT_ENABLE
    switch (capability) {
    case ADEV_USART_CAP_TX_DIRECT:
        if ((handle->mode & ADEV_USART_TX_MASK) == ADEV_USART_TX_POLLING)
            return A_TRUE;
#if ADEV_USART_DMA_BACKEND_ENABLE
        return (handle->mode & ADEV_USART_TX_MASK) == ADEV_USART_TX_DMA_BUFFERED &&
               aDrvUsartAsyncTxIsSupported(&handle->drv_handle);
#else
        return A_FALSE;
#endif
    case ADEV_USART_CAP_RX_DIRECT:
        if ((handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_POLLING)
            return A_TRUE;
#if ADEV_USART_DMA_BACKEND_ENABLE
        return (handle->mode & ADEV_USART_RX_MASK) == ADEV_USART_RX_DMA_BUFFERED &&
               aDrvUsartAsyncRxIsSupported(&handle->drv_handle);
#else
        return A_FALSE;
#endif
    default:
        return A_FALSE;
    }
#else
    (void)capability;
    return A_FALSE;
#endif
}
