#ifndef ADRV_INTERNAL_H
#define ADRV_INTERNAL_H

#include "aDrv_gpio.h"
#include "gd32e50x.h"

#define ADRV_ARRAY_COUNT(array_) (sizeof(array_) / sizeof((array_)[0]))

typedef struct {
    uint32_t port;
    uint32_t pin_mask;
    rcu_periph_enum clock;
    uint8_t pin_number;
} aDrvPrivateGpio_t;

aStatus_t aDrvResolvePin(aDrvGpioPin_t pin, aDrvPrivateGpio_t *gpio);

#endif
