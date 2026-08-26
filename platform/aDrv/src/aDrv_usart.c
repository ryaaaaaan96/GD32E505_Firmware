#include "aDrv_usart.h"

#include "aDrv_internal.h"

typedef struct {
    uint32_t instance;
    rcu_periph_enum clock;
} usartMapping_t;

static const usartMapping_t usart_mappings[] = {
    {USART0, RCU_USART0},
    {USART1, RCU_USART1},
    {USART2, RCU_USART2},
    {UART3, RCU_UART3},
    {UART4, RCU_UART4},
    {USART5, RCU_USART5},
};

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

    config->id = ADRV_USART_1;
    config->baud_rate = 115200U;
    config->parity = ADRV_USART_PARITY_NONE;
    config->stop_bits = ADRV_USART_STOP_1;
    config->tx_pin = ADRV_PIN_NONE;
    config->rx_pin = ADRV_PIN_NONE;
}

void aDrvUsartHandleStructInit(aDrvUsartHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->instance = 0U;
    handle->baud_rate = 0U;
    handle->parity = ADRV_USART_PARITY_NONE;
    handle->stop_bits = ADRV_USART_STOP_1;
    handle->initialized = 0U;
}

aStatus_t aDrvUsartInitStatic(const aDrvUsartConfig_t *config,
                              aDrvUsartHandle_t *handle)
{
    const usartMapping_t *mapping;
    aDrvPrivateGpio_t tx_gpio;
    aDrvPrivateGpio_t rx_gpio;

    if ((config == NULL) || (handle == NULL) ||
        ((size_t)config->id >= ADRV_ARRAY_COUNT(usart_mappings)) ||
        (config->baud_rate == 0U) ||
        (config->parity > ADRV_USART_PARITY_ODD) ||
        (config->stop_bits > ADRV_USART_STOP_2) ||
        (aDrvResolvePin(config->tx_pin, &tx_gpio) != A_STATUS_OK) ||
        (aDrvResolvePin(config->rx_pin, &rx_gpio) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    mapping = &usart_mappings[config->id];
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
    handle->initialized = 1U;
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

aStatus_t aDrvUsartRegisterCallback(
    aDrvUsartHandle_t *handle, const aDrvUsartExtiConfig_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    return A_STATUS_UNSUPPORTED;
}

aStatus_t aDrvUsartUnregisterCallback(aDrvUsartHandle_t *handle,
                                      aDrvUsartExti_t trigger)
{
    (void)trigger;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }

    return A_STATUS_UNSUPPORTED;
}

void aDrvUsartEnableInterrupt(aDrvUsartHandle_t *handle)
{
    (void)handle;
}

void aDrvUsartDisableInterrupt(aDrvUsartHandle_t *handle)
{
    (void)handle;
}

aStatus_t aDrvUsartSetBaudrate(aDrvUsartHandle_t *handle, uint32_t baud_rate)
{
    if ((handle == NULL) || (baud_rate == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
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
