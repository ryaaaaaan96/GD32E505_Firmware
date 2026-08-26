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

aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *config)
{
    aDrvPrivateGpio_t gpio;
    uint32_t mode;

    if ((config == NULL) ||
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

    return A_STATUS_OK;
}

aStatus_t aDrvGpioWrite(aDrvGpioPin_t pin, aDrvGpioLevel_t level)
{
    aDrvPrivateGpio_t gpio;

    if (aDrvResolvePin(pin, &gpio) != A_STATUS_OK) {
        return A_STATUS_INVALID_PARAM;
    }
    if ((level != ADRV_GPIO_LOW) && (level != ADRV_GPIO_HIGH)) {
        return A_STATUS_INVALID_PARAM;
    }

    gpio_bit_write(gpio.port, gpio.pin_mask,
                   level == ADRV_GPIO_HIGH ? SET : RESET);
    return A_STATUS_OK;
}

aStatus_t aDrvGpioRead(aDrvGpioPin_t pin, aDrvGpioLevel_t *level)
{
    aDrvPrivateGpio_t gpio;

    if ((level == NULL) || (aDrvResolvePin(pin, &gpio) != A_STATUS_OK)) {
        return A_STATUS_INVALID_PARAM;
    }

    *level = gpio_input_bit_get(gpio.port, gpio.pin_mask) != RESET
                 ? ADRV_GPIO_HIGH
                 : ADRV_GPIO_LOW;
    return A_STATUS_OK;
}

aStatus_t aDrvGpioToggle(aDrvGpioPin_t pin)
{
    aDrvGpioLevel_t level;
    aStatus_t status;

    status = aDrvGpioRead(pin, &level);
    if (status != A_STATUS_OK) {
        return status;
    }

    level = level == ADRV_GPIO_HIGH ? ADRV_GPIO_LOW : ADRV_GPIO_HIGH;
    return aDrvGpioWrite(pin, level);
}
