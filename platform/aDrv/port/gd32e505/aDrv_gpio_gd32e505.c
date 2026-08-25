#include "aDrv_gpio.h"
#include "aDrv_gd32e505_internal.h"
aStatus_t aDrvGd32ResolvePin(aDrvGpioPin_t pin, aDrvGd32Gpio_t *gpio)
{
    if ((gpio == NULL) || !aDrvIsGpioValid(pin)) return A_STATUS_INVALID_PARAM;
    static const uint32_t ports[] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF, GPIOG };
    static const rcu_periph_enum clocks[] = { RCU_GPIOA, RCU_GPIOB, RCU_GPIOC, RCU_GPIOD, RCU_GPIOE, RCU_GPIOF, RCU_GPIOG };
    const uint32_t index = pin / 16U; gpio->port = ports[index]; gpio->clock = clocks[index];
    gpio->pin_number = (uint8_t)(pin % 16U); gpio->pin_mask = 1UL << gpio->pin_number;
    return A_STATUS_OK;
}
void aDrvGpioConfigStructInit(aDrvGpioConfig_t *config)
{ if (config != NULL) { config->pin = ADRV_PIN_NONE; config->mode = ADRV_GPIO_INPUT; config->initial_level = ADRV_GPIO_LOW; } }
aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *config)
{
    aDrvGd32Gpio_t gpio;
    if ((config == NULL) || (aDrvGd32ResolvePin(config->pin, &gpio) != A_STATUS_OK)) return A_STATUS_INVALID_PARAM;
    uint32_t mode;
    switch (config->mode) {
    case ADRV_GPIO_INPUT: mode = GPIO_MODE_IN_FLOATING; break;
    case ADRV_GPIO_OUTPUT_PUSH_PULL: mode = GPIO_MODE_OUT_PP; break;
    case ADRV_GPIO_OUTPUT_OPEN_DRAIN: mode = GPIO_MODE_OUT_OD; break;
    case ADRV_GPIO_ALTERNATE_PUSH_PULL: mode = GPIO_MODE_AF_PP; break;
    case ADRV_GPIO_ALTERNATE_OPEN_DRAIN: mode = GPIO_MODE_AF_OD; break;
    case ADRV_GPIO_ANALOG: mode = GPIO_MODE_AIN; break;
    default: return A_STATUS_INVALID_PARAM;
    }
    rcu_periph_clock_enable(gpio.clock);
    if ((config->mode == ADRV_GPIO_OUTPUT_PUSH_PULL) || (config->mode == ADRV_GPIO_OUTPUT_OPEN_DRAIN))
        gpio_bit_write(gpio.port, gpio.pin_mask, config->initial_level == ADRV_GPIO_HIGH ? SET : RESET);
    gpio_init(gpio.port, mode, GPIO_OSPEED_50MHZ, gpio.pin_mask); return A_STATUS_OK;
}
aStatus_t aDrvGpioWrite(aDrvGpioPin_t pin, aDrvGpioLevel_t level)
{ aDrvGd32Gpio_t g; if (aDrvGd32ResolvePin(pin,&g)!=A_STATUS_OK) return A_STATUS_INVALID_PARAM; gpio_bit_write(g.port,g.pin_mask,level?SET:RESET); return A_STATUS_OK; }
aStatus_t aDrvGpioRead(aDrvGpioPin_t pin, aDrvGpioLevel_t *level)
{ aDrvGd32Gpio_t g; if ((level==NULL)||(aDrvGd32ResolvePin(pin,&g)!=A_STATUS_OK)) return A_STATUS_INVALID_PARAM; *level=gpio_input_bit_get(g.port,g.pin_mask)?ADRV_GPIO_HIGH:ADRV_GPIO_LOW; return A_STATUS_OK; }
aStatus_t aDrvGpioToggle(aDrvGpioPin_t pin)
{ aDrvGpioLevel_t level; aStatus_t s=aDrvGpioRead(pin,&level); return s==A_STATUS_OK?aDrvGpioWrite(pin,level==ADRV_GPIO_HIGH?ADRV_GPIO_LOW:ADRV_GPIO_HIGH):s; }
