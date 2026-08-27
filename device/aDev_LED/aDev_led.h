#ifndef ADEV_LED_H
#define ADEV_LED_H

#include "aDrv_gpio.h"
#include "aLib.h"

#include <stdbool.h>

typedef enum {
    ADEV_LED_ACTIVE_LOW,
    ADEV_LED_ACTIVE_HIGH,
} aDevLedActiveLevel_t;

typedef struct {
    aDrvGpioPin_t pin;
    aDevLedActiveLevel_t active_level;
    bool initially_on;
} aDevLedConfig_t;

typedef struct {
    aDrvGpioPin_t pin;
    aDevLedActiveLevel_t active_level;
    bool initialized;
} aDevLedHandle_t;

void aDevLedConfigStructInit(aDevLedConfig_t *config);
void aDevLedHandleStructInit(aDevLedHandle_t *handle);
aStatus_t aDevLedInit(const aDevLedConfig_t *config,
                      aDevLedHandle_t *handle);
aStatus_t aDevLedSet(aDevLedHandle_t *handle, bool on);
aStatus_t aDevLedOn(aDevLedHandle_t *handle);
aStatus_t aDevLedOff(aDevLedHandle_t *handle);
aStatus_t aDevLedToggle(aDevLedHandle_t *handle);
aStatus_t aDevLedGet(const aDevLedHandle_t *handle, bool *on);

#endif
