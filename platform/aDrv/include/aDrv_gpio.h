#ifndef ADRV_GPIO_H
#define ADRV_GPIO_H
#include "aDrv_basic.h"
typedef enum { ADRV_GPIO_INPUT, ADRV_GPIO_OUTPUT_PUSH_PULL,
    ADRV_GPIO_OUTPUT_OPEN_DRAIN, ADRV_GPIO_ALTERNATE_PUSH_PULL,
    ADRV_GPIO_ALTERNATE_OPEN_DRAIN, ADRV_GPIO_ANALOG } aDrvGpioMode_t;
typedef enum { ADRV_GPIO_LOW = 0, ADRV_GPIO_HIGH = 1 } aDrvGpioLevel_t;
typedef struct { aDrvGpioPin_t pin; aDrvGpioMode_t mode; aDrvGpioLevel_t initial_level; } aDrvGpioConfig_t;
void aDrvGpioConfigStructInit(aDrvGpioConfig_t *config);
aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *config);
aStatus_t aDrvGpioWrite(aDrvGpioPin_t pin, aDrvGpioLevel_t level);
aStatus_t aDrvGpioRead(aDrvGpioPin_t pin, aDrvGpioLevel_t *level);
aStatus_t aDrvGpioToggle(aDrvGpioPin_t pin);
#endif
