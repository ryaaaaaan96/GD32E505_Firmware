#include "aDev_led.h"

static aDrvGpioLevel_t output_level(const aDevLedHandle_t *handle, aBool_t on)
{
    const aBool_t active_high =
        handle->active_level == ADEV_LED_ACTIVE_HIGH;
    return on == active_high ? ADRV_GPIO_HIGH : ADRV_GPIO_LOW;
}

void aDevLedConfigStructInit(aDevLedConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->pin = ADRV_PIN_NONE;
    config->active_level = ADEV_LED_ACTIVE_HIGH;
    config->initially_on = A_FALSE;
}

void aDevLedHandleStructInit(aDevLedHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->pin = ADRV_PIN_NONE;
    handle->active_level = ADEV_LED_ACTIVE_HIGH;
    handle->initialized = A_FALSE;
}

aStatus_t aDevLedInit(const aDevLedConfig_t *config,
                      aDevLedHandle_t *handle)
{
    aDrvGpioConfig_t gpio_config;

    if ((config == NULL) || (handle == NULL) ||
        (config->pin == ADRV_PIN_NONE) ||
        (config->active_level > ADEV_LED_ACTIVE_HIGH)) {
        return A_STATUS_INVALID_PARAM;
    }

    aDevLedHandleStructInit(handle);
    handle->pin = config->pin;
    handle->active_level = config->active_level;

    aDrvGpioConfigStructInit(&gpio_config);
    gpio_config.pin = config->pin;
    gpio_config.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    gpio_config.initial_level = output_level(handle, config->initially_on);
    const aStatus_t status = aDrvGpioInit(&gpio_config);
    if (status != A_STATUS_OK) {
        aDevLedHandleStructInit(handle);
        return status;
    }

    handle->initialized = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aDevLedSet(aDevLedHandle_t *handle, aBool_t on)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return A_STATUS_NOT_READY;
    }

    return aDrvGpioWrite(handle->pin, output_level(handle, on));
}

aStatus_t aDevLedOn(aDevLedHandle_t *handle)
{
    return aDevLedSet(handle, A_TRUE);
}

aStatus_t aDevLedOff(aDevLedHandle_t *handle)
{
    return aDevLedSet(handle, A_FALSE);
}

aStatus_t aDevLedToggle(aDevLedHandle_t *handle)
{
    aBool_t on;
    aStatus_t status;

    status = aDevLedGet(handle, &on);
    if (status != A_STATUS_OK) {
        return status;
    }
    return aDevLedSet(handle, !on);
}

aStatus_t aDevLedGet(const aDevLedHandle_t *handle, aBool_t *on)
{
    aDrvGpioLevel_t level;
    aStatus_t status;

    if ((handle == NULL) || (on == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (!handle->initialized) {
        return A_STATUS_NOT_READY;
    }

    status = aDrvGpioRead(handle->pin, &level);
    if (status != A_STATUS_OK) {
        return status;
    }

    *on = level == output_level(handle, A_TRUE);
    return A_STATUS_OK;
}
