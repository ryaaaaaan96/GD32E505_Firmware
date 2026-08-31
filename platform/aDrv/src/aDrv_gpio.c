#include "aDrv_gpio.h"

#include "aDrv_internal.h"

typedef struct {
    uint32_t port;
    rcu_periph_enum clock;
} gpioMapping_t;

static const gpioMapping_t gpio_mappings[] = {
    {GPIOA, RCU_GPIOA},
    {GPIOB, RCU_GPIOB},
    {GPIOC, RCU_GPIOC},
    {GPIOD, RCU_GPIOD},
    {GPIOE, RCU_GPIOE},
    {GPIOF, RCU_GPIOF},
    {GPIOG, RCU_GPIOG},
};

static int32_t is_gpio_valid(aDrvGpioPin_t pin)
{
    return (pin / 16U) < ADRV_ARRAY_COUNT(gpio_mappings);
}

aStatus_t aDrvResolvePin(aDrvGpioPin_t pin, aDrvPrivateGpio_t *gpio)
{
    uint32_t port_index;

    if ((gpio == NULL) || !is_gpio_valid(pin)) {
        return A_STATUS_INVALID_PARAM;
    }

    port_index = pin / 16U;
    gpio->port = gpio_mappings[port_index].port;
    gpio->clock = gpio_mappings[port_index].clock;
    gpio->pin_number = (uint8_t)(pin % 16U);
    gpio->pin_mask = 1UL << gpio->pin_number;

    return A_STATUS_OK;
}

void aDrvGpioConfigStructInit(aDrvGpioConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->pin = ADRV_PIN_NONE;
    config->mode = ADRV_GPIO_INPUT;
    config->initial_level = ADRV_GPIO_LOW;
}

void aDrvGpioHandleStructInit(aDrvGpioHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->pin = ADRV_PIN_NONE;
    handle->initialized = A_FALSE;
}

aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *config,
                       aDrvGpioHandle_t *handle)
{
    aDrvPrivateGpio_t gpio;
    uint32_t mode;

    if ((config == NULL) || (handle == NULL) ||
        (aDrvResolvePin(config->pin, &gpio) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    switch (config->mode) {
    case ADRV_GPIO_INPUT:
        mode = GPIO_MODE_IN_FLOATING;
        break;
    case ADRV_GPIO_OUTPUT_PUSH_PULL:
        mode = GPIO_MODE_OUT_PP;
        break;
    case ADRV_GPIO_OUTPUT_OPEN_DRAIN:
        mode = GPIO_MODE_OUT_OD;
        break;
    case ADRV_GPIO_ALTERNATE_PUSH_PULL:
        mode = GPIO_MODE_AF_PP;
        break;
    case ADRV_GPIO_ALTERNATE_OPEN_DRAIN:
        mode = GPIO_MODE_AF_OD;
        break;
    case ADRV_GPIO_ANALOG:
        mode = GPIO_MODE_AIN;
        break;
    default:
        return A_STATUS_INVALID_PARAM;
    }

    rcu_periph_clock_enable(gpio.clock);
    if ((config->mode == ADRV_GPIO_OUTPUT_PUSH_PULL) ||
        (config->mode == ADRV_GPIO_OUTPUT_OPEN_DRAIN)) {
        gpio_bit_write(gpio.port, gpio.pin_mask,
                       config->initial_level == ADRV_GPIO_HIGH ? SET : RESET);
    }
    gpio_init(gpio.port, mode, GPIO_OSPEED_50MHZ, gpio.pin_mask);

    handle->pin = config->pin;
    handle->initialized = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDrvGpioDeInit(aDrvGpioHandle_t *handle)
{
    aDrvPrivateGpio_t gpio;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return A_STATUS_NOT_READY;
    }
    if (aDrvResolvePin(handle->pin, &gpio) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }

    gpio_init(gpio.port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ,
              gpio.pin_mask);
    aDrvGpioHandleStructInit(handle);
    return A_STATUS_OK;
}

aStatus_t aDrvGpioWrite(const aDrvGpioHandle_t *handle,
                        aDrvGpioLevel_t level)
{
    aDrvPrivateGpio_t gpio;

    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return A_STATUS_NOT_READY;
    }
    if (aDrvResolvePin(handle->pin, &gpio) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((level != ADRV_GPIO_LOW) && (level != ADRV_GPIO_HIGH)) {
        return A_STATUS_INVALID_PARAM;
    }

    gpio_bit_write(gpio.port, gpio.pin_mask,
                   level == ADRV_GPIO_HIGH ? SET : RESET);
    return A_STATUS_OK;
}

aStatus_t aDrvGpioRead(const aDrvGpioHandle_t *handle,
                       aDrvGpioLevel_t *level)
{
    aDrvPrivateGpio_t gpio;

    if ((handle == NULL) || (level == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return A_STATUS_NOT_READY;
    }
    if (aDrvResolvePin(handle->pin, &gpio) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }

    *level = gpio_input_bit_get(gpio.port, gpio.pin_mask) != RESET
                 ? ADRV_GPIO_HIGH
                 : ADRV_GPIO_LOW;
    return A_STATUS_OK;
}

aStatus_t aDrvGpioToggle(const aDrvGpioHandle_t *handle)
{
    aDrvGpioLevel_t level;
    aStatus_t status;

    status = aDrvGpioRead(handle, &level);
    if (status != A_STATUS_OK) {
        return status;
    }

    level = level == ADRV_GPIO_HIGH ? ADRV_GPIO_LOW : ADRV_GPIO_HIGH;
    return aDrvGpioWrite(handle, level);
}
