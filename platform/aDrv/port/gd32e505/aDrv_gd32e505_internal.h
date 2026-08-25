#ifndef ADRV_GD32E505_INTERNAL_H
#define ADRV_GD32E505_INTERNAL_H
#include "aDrv_basic.h"
#include "gd32e50x.h"
typedef struct { uint32_t port, pin_mask; rcu_periph_enum clock; uint8_t pin_number; } aDrvGd32Gpio_t;
aStatus_t aDrvGd32ResolvePin(aDrvGpioPin_t pin, aDrvGd32Gpio_t *gpio);
#endif
