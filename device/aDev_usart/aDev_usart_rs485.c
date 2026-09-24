#include "aDev_usart_internal.h"

static aDrvGpioLevel_t inactive(aDrvGpioLevel_t level)
{
    return level == ADRV_GPIO_HIGH ? ADRV_GPIO_LOW : ADRV_GPIO_HIGH;
}

static aStatus_t init_pin(aDrvGpioHandle_t *gpio, aDrvGpioPin_t pin,
                          aDrvGpioLevel_t level)
{
    aDrvGpioConfig_t config;
    aDrvGpioConfigStructInit(&config);
    config.pin = pin;
    config.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    config.initial_level = level;
    return aDrvGpioInit(&config, gpio);
}

aStatus_t aDevUsartRS485Init(aDevUsartHandle_t *handle,
                            const aDevUsartRS485Config_t *config)
{
    aStatus_t status;
    handle->rs485 = *config;
    if (!config->enabled) {
        return A_STATUS_OK;
    }
    if ((config->de_pin == ADRV_PIN_NONE) ||
        (config->de_pin == config->re_pin) ||
        ((config->de_active_level != ADRV_GPIO_LOW) &&
         (config->de_active_level != ADRV_GPIO_HIGH)) ||
        ((config->re_active_level != ADRV_GPIO_LOW) &&
         (config->re_active_level != ADRV_GPIO_HIGH))) {
        return A_STATUS_INVALID_PARAM;
    }
    /* 即使轮询 TX 也需要 TC IRQ，以便超时返回后安全释放线路。 */
    if (!aDrvUsartInterruptIsSupported()) {
        return A_STATUS_UNSUPPORTED;
    }
    status = init_pin(&handle->de_gpio, config->de_pin,
                      inactive(config->de_active_level));
    if ((status == A_STATUS_OK) && (config->re_pin != ADRV_PIN_NONE)) {
        status = init_pin(&handle->re_gpio, config->re_pin,
                          config->re_active_level);
    }
    return status;
}

aStatus_t aDevUsartRS485Begin(aDevUsartHandle_t *handle)
{
    aStatus_t status;
    if (!handle->rs485.enabled || handle->rs485_transmitting) {
        return A_STATUS_OK;
    }
    if (handle->re_gpio.initialized && !handle->rs485.receive_during_tx) {
        status = aDrvGpioWrite(&handle->re_gpio,
                               inactive(handle->rs485.re_active_level));
        if (status != A_STATUS_OK) {
            return status;
        }
    }
    status = aDrvGpioWrite(&handle->de_gpio, handle->rs485.de_active_level);
    if (status == A_STATUS_OK) {
        handle->rs485_transmitting = A_TRUE;
    } else if (handle->re_gpio.initialized) {
        (void)aDrvGpioWrite(&handle->re_gpio, handle->rs485.re_active_level);
    }
    return status;
}

aStatus_t aDevUsartRS485Complete(aDevUsartHandle_t *handle)
{
    aStatus_t status;
    if (!handle->rs485.enabled || !handle->rs485_transmitting) {
        return A_STATUS_OK;
    }
    status = aDrvGpioWrite(&handle->de_gpio,
                           inactive(handle->rs485.de_active_level));
    if (status != A_STATUS_OK) {
        return status;
    }
    if (handle->re_gpio.initialized) {
        status = aDrvGpioWrite(&handle->re_gpio, handle->rs485.re_active_level);
    }
    if (status == A_STATUS_OK) {
        handle->rs485_transmitting = A_FALSE;
    }
    return status;
}

aStatus_t aDevUsartRS485DeInit(aDevUsartHandle_t *handle)
{
    aStatus_t status = aDevUsartRS485Complete(handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    if (handle->re_gpio.initialized) {
        status = aDrvGpioDeInit(&handle->re_gpio);
    }
    if (handle->de_gpio.initialized) {
        aStatus_t de_status = aDrvGpioDeInit(&handle->de_gpio);
        if (status == A_STATUS_OK) {
            status = de_status;
        }
    }
    return status;
}
