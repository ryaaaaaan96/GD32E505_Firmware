#include "aDev_led_instance.h"

#if ADEV_LED_DYNAMIC_ENABLE
#include "aOS.h"
#endif

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
    config->speed = ADRV_GPIO_SPEED_HIGH;
}

void aDevLedHandleStructInit(aDevLedHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    aDrvGpioHandleStructInit(&handle->gpio);
    handle->active_level = ADEV_LED_ACTIVE_HIGH;
#if ADEV_LED_DYNAMIC_ENABLE
    handle->dynamic_storage = A_FALSE;
#endif
}

static aStatus_t handle_init(const aDevLedConfig_t *config,
                            aDevLedHandle_t *handle)
{
    aDrvGpioConfig_t gpio_config;
    aStatus_t status;

    if ((config == NULL) ||
        (config->pin == ADRV_PIN_NONE) ||
        ((config->active_level != ADEV_LED_ACTIVE_LOW) &&
         (config->active_level != ADEV_LED_ACTIVE_HIGH))) {
        return A_STATUS_INVALID_PARAM;
    }

    aDrvGpioConfigStructInit(&gpio_config);
    gpio_config.pin = config->pin;
    gpio_config.speed = config->speed;
    gpio_config.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    handle->active_level = config->active_level;
    gpio_config.initial_level = output_level(handle, config->initially_on);
    status = aDrvGpioInit(&gpio_config, &handle->gpio);
    if (status != A_STATUS_OK) {
        handle->active_level = ADEV_LED_ACTIVE_HIGH;
    }
    return status;
}

#if ADEV_LED_STATIC_ENABLE
aStatus_t aDevLedInitStatic(const aDevLedConfig_t *config,
                           aDevLedHandle_t *handle)
{
    if ((config == NULL) || (handle == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    aDevLedHandleStructInit(handle);
    return handle_init(config, handle);
}
#endif

#if ADEV_LED_DYNAMIC_ENABLE
aStatus_t aDevLedCreate(const aDevLedConfig_t *config,
                       aDevLedHandle_t **handle_out)
{
    aDevLedHandle_t *handle;
    aStatus_t status;

    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;

    aDevLedHandleStructInit(handle);
    handle->dynamic_storage = A_TRUE;
    status = handle_init(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aDevLedDestroy(aDevLedHandle_t *handle)
{
    aStatus_t status;

    if ((handle == NULL) || !handle->dynamic_storage) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->gpio.initialized) {
        status = aDevLedDeInit(handle);
        if (status != A_STATUS_OK) return status;
    }
    aOSFree(handle);
    return A_STATUS_OK;
}
#endif

aStatus_t aDevLedDeInit(aDevLedHandle_t *handle)
{
    aStatus_t status;

    status = aDevLedOff(handle);
    if (status != A_STATUS_OK) return status;
    status = aDrvGpioDeInit(&handle->gpio);
    if (status != A_STATUS_OK) return status;
    /* GPIO 状态已清空；保留动态所有权，供之后 Destroy 回收。 */
    handle->active_level = ADEV_LED_ACTIVE_HIGH;
    return A_STATUS_OK;
}

aStatus_t aDevLedSet(aDevLedHandle_t *handle, aBool_t on)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    return aDrvGpioWrite(&handle->gpio, output_level(handle, on));
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
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return aDrvGpioToggle(&handle->gpio);
}

aStatus_t aDevLedGet(const aDevLedHandle_t *handle, aBool_t *on)
{
    aDrvGpioLevel_t level;
    aStatus_t status;

    if ((handle == NULL) || (on == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aDrvGpioReadOutput(&handle->gpio, &level);
    if (status != A_STATUS_OK) {
        return status;
    }

    *on = level == output_level(handle, A_TRUE);
    return A_STATUS_OK;
}
