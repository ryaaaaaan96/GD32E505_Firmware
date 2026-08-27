#include "aDrv_usart.h"

#include "aDrv_usart_internal.h"

static const aDrvPrivateUsartMapping_t s_usart_mappings[] = {
    {USART0, RCU_USART0, USART0_IRQn},
    {USART1, RCU_USART1, USART1_IRQn},
    {USART2, RCU_USART2, USART2_IRQn},
    {UART3, RCU_UART3, UART3_IRQn},
    {UART4, RCU_UART4, UART4_IRQn},
    {USART5, RCU_USART5, USART5_IRQn},
};

static aDrvUsartHandle_t *s_usart_handles[
    ADRV_ARRAY_COUNT(s_usart_mappings)];

const aDrvPrivateUsartMapping_t *aDrvPrivateUsartMappingGet(
    aDrvUsartId_t id)
{
    if ((size_t)id >= ADRV_ARRAY_COUNT(s_usart_mappings)) {
        return NULL;
    }
    return &s_usart_mappings[id];
}

aDrvUsartHandle_t *aDrvPrivateUsartHandleGet(aDrvUsartId_t id)
{
    if ((size_t)id >= ADRV_ARRAY_COUNT(s_usart_handles)) {
        return NULL;
    }
    return s_usart_handles[id];
}

void aDrvPrivateUsartHandleSet(aDrvUsartId_t id,
                               aDrvUsartHandle_t *handle)
{
    if ((size_t)id < ADRV_ARRAY_COUNT(s_usart_handles)) {
        s_usart_handles[id] = handle;
    }
}

aStatus_t aDrvPrivateUsartOwnerAcquire(aDrvUsartHandle_t *handle,
                                       aDrvUsartOwner_t owner)
{
    if ((handle == NULL) || (owner == ADRV_USART_OWNER_NONE)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if ((handle->owner != ADRV_USART_OWNER_NONE) &&
        (handle->owner != owner)) {
        return A_STATUS_BUSY;
    }

    handle->owner = owner;
    return A_STATUS_OK;
}

void aDrvPrivateUsartOwnerRelease(aDrvUsartHandle_t *handle,
                                  aDrvUsartOwner_t owner)
{
    if ((handle != NULL) && (handle->owner == owner)) {
        handle->owner = ADRV_USART_OWNER_NONE;
    }
}

static uint32_t map_parity(aDrvUsartParity_t parity)
{
    switch (parity) {
    case ADRV_USART_PARITY_EVEN:
        return USART_PM_EVEN;
    case ADRV_USART_PARITY_ODD:
        return USART_PM_ODD;
    case ADRV_USART_PARITY_NONE:
    default:
        return USART_PM_NONE;
    }
}

void aDrvUsartConfigStructInit(aDrvUsartConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->id = ADRV_USART_0;
    config->baud_rate = 115200U;
    config->parity = ADRV_USART_PARITY_NONE;
    config->stop_bits = ADRV_USART_STOP_1;
    config->tx_pin = ADRV_PIN_NONE;
    config->rx_pin = ADRV_PIN_NONE;
}

void aDrvUsartHandleStructInit(aDrvUsartHandle_t *handle)
{
    size_t index;

    if (handle == NULL) {
        return;
    }

    handle->instance = 0U;
    handle->baud_rate = 0U;
    handle->parity = ADRV_USART_PARITY_NONE;
    handle->stop_bits = ADRV_USART_STOP_1;
    handle->id = ADRV_USART_0;
    for (index = 0U; index < ADRV_USART_EXTI_MAX; ++index) {
        handle->callbacks[index].function = NULL;
        handle->callbacks[index].argument = NULL;
    }
    handle->owner = ADRV_USART_OWNER_NONE;
    handle->irq_priority = 5U;
    handle->initialized = 0U;
}

aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *config,
                              aDrvUsartHandle_t *handle)
{
    const aDrvPrivateUsartMapping_t *mapping;
    aDrvPrivateGpio_t tx_gpio;
    aDrvPrivateGpio_t rx_gpio;

    if ((config == NULL) || (handle == NULL) ||
        (config->baud_rate == 0U) ||
        (config->parity > ADRV_USART_PARITY_ODD) ||
        (config->stop_bits > ADRV_USART_STOP_2) ||
        (aDrvResolvePin(config->tx_pin, &tx_gpio) != A_STATUS_OK) ||
        (aDrvResolvePin(config->rx_pin, &rx_gpio) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    mapping = aDrvPrivateUsartMappingGet(config->id);
    if (mapping == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (aDrvPrivateUsartHandleGet(config->id) != NULL) {
        return A_STATUS_BUSY;
    }

    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(mapping->clock);
    rcu_periph_clock_enable(tx_gpio.clock);
    rcu_periph_clock_enable(rx_gpio.clock);

    gpio_init(tx_gpio.port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ,
              tx_gpio.pin_mask);
    gpio_init(rx_gpio.port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ,
              rx_gpio.pin_mask);

    usart_deinit(mapping->instance);
    usart_baudrate_set(mapping->instance, config->baud_rate);
    usart_word_length_set(mapping->instance, USART_WL_8BIT);
    usart_stop_bit_set(mapping->instance,
                       config->stop_bits == ADRV_USART_STOP_2
                           ? USART_STB_2BIT
                           : USART_STB_1BIT);
    usart_parity_config(mapping->instance, map_parity(config->parity));
    usart_transmit_config(mapping->instance, USART_TRANSMIT_ENABLE);
    usart_receive_config(mapping->instance, USART_RECEIVE_ENABLE);
    usart_enable(mapping->instance);

    handle->instance = mapping->instance;
    handle->baud_rate = config->baud_rate;
    handle->parity = config->parity;
    handle->stop_bits = config->stop_bits;
    handle->id = config->id;
    handle->owner = ADRV_USART_OWNER_NONE;
    handle->initialized = 1U;
    aDrvPrivateUsartHandleSet(config->id, handle);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartDeInitStatic(aDrvUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    (void)aDrvUsartAsyncTxAbort(handle);
    aDrvUsartDisableInterrupt(handle);
    aDrvPrivateUsartHandleSet(handle->id, NULL);
    usart_disable((uint32_t)handle->instance);
    aDrvUsartHandleStructInit(handle);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartTryWriteByte(aDrvUsartHandle_t *handle, uint8_t data)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->owner == ADRV_USART_OWNER_ASYNC) {
        return A_STATUS_BUSY;
    }
    if (usart_flag_get((uint32_t)handle->instance, USART_FLAG_TBE) == RESET) {
        return A_STATUS_BUSY;
    }

    usart_data_transmit((uint32_t)handle->instance, data);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartTryReadByte(aDrvUsartHandle_t *handle, uint8_t *data)
{
    if ((handle == NULL) || (data == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (usart_flag_get((uint32_t)handle->instance, USART_FLAG_RBNE) == RESET) {
        return A_STATUS_BUSY;
    }

    *data = (uint8_t)usart_data_receive((uint32_t)handle->instance);
    return A_STATUS_OK;
}

aStatus_t aDrvUsartIsTransmitComplete(const aDrvUsartHandle_t *handle,
                                      bool *complete)
{
    if ((handle == NULL) || (complete == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    *complete = usart_flag_get((uint32_t)handle->instance, USART_FLAG_TC) !=
                RESET;
    return A_STATUS_OK;
}

aStatus_t aDrvUsartSetBaudrate(aDrvUsartHandle_t *handle,
                               uint32_t baud_rate)
{
    if ((handle == NULL) || (baud_rate == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->owner != ADRV_USART_OWNER_NONE) {
        return A_STATUS_BUSY;
    }

    usart_baudrate_set((uint32_t)handle->instance, baud_rate);
    handle->baud_rate = baud_rate;
    return A_STATUS_OK;
}

void aDrvUsartGetBaudrate(const aDrvUsartHandle_t *handle,
                          uint32_t *baud_rate)
{
    if ((handle != NULL) && (baud_rate != NULL)) {
        *baud_rate = handle->baud_rate;
    }
}

aStatus_t aDrvUsartSetStopbits(aDrvUsartHandle_t *handle,
                               aDrvUsartStopBits_t stop_bits)
{
    if ((handle == NULL) || (stop_bits > ADRV_USART_STOP_2)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->owner != ADRV_USART_OWNER_NONE) {
        return A_STATUS_BUSY;
    }

    usart_stop_bit_set((uint32_t)handle->instance,
                       stop_bits == ADRV_USART_STOP_2 ? USART_STB_2BIT
                                                     : USART_STB_1BIT);
    handle->stop_bits = stop_bits;
    return A_STATUS_OK;
}

void aDrvUsartGetStopbits(const aDrvUsartHandle_t *handle,
                          aDrvUsartStopBits_t *stop_bits)
{
    if ((handle != NULL) && (stop_bits != NULL)) {
        *stop_bits = handle->stop_bits;
    }
}

aStatus_t aDrvUsartSetParity(aDrvUsartHandle_t *handle,
                             aDrvUsartParity_t parity)
{
    if ((handle == NULL) || (parity > ADRV_USART_PARITY_ODD)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    if (handle->owner != ADRV_USART_OWNER_NONE) {
        return A_STATUS_BUSY;
    }

    usart_parity_config((uint32_t)handle->instance, map_parity(parity));
    handle->parity = parity;
    return A_STATUS_OK;
}

void aDrvUsartGetParity(const aDrvUsartHandle_t *handle,
                        aDrvUsartParity_t *parity)
{
    if ((handle != NULL) && (parity != NULL)) {
        *parity = handle->parity;
    }
}
