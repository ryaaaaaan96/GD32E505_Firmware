#include "aDev_rs485.h"

static aDrvGpioLevel_t inverse_level(aDrvGpioLevel_t level)
{
    return level == ADRV_GPIO_HIGH ? ADRV_GPIO_LOW : ADRV_GPIO_HIGH;
}

static aStatus_t set_direction(aDevHandle_RS485_t *handle,
                               uint8_t transmit)
{
    const aDrvGpioLevel_t level = transmit != 0U ?
        handle->transmit_level : inverse_level(handle->transmit_level);
    aStatus_t status = aDrvGpioWrite(handle->de_pin, level);
    if (status != A_STATUS_OK) return status;
    if ((handle->re_pin != ADRV_PIN_NONE) &&
        ((status = aDrvGpioWrite(handle->re_pin, level)) != A_STATUS_OK)) {
        return status;
    }
    return A_STATUS_OK;
}

void aDevRS485ConfigStructInit(aDevConfig_RS485_t *config)
{
    if (config == NULL) return;
    aDevUsartConfigStructInit(&config->usart_config);
    config->de_pin = ADRV_PIN_NONE;
    config->re_pin = ADRV_PIN_NONE;
    config->transmit_level = ADRV_GPIO_HIGH;
    config->timeout_ms = 1000U;
}

void aDevRS485HandleStructInit(aDevHandle_RS485_t *handle)
{
    if (handle == NULL) return;
    aDevUsartHandleStructInit(&handle->usart_handle);
    handle->de_pin = ADRV_PIN_NONE;
    handle->re_pin = ADRV_PIN_NONE;
    handle->transmit_level = ADRV_GPIO_HIGH;
    handle->timeout_ms = 1000U;
    handle->initialized = 0U;
}

aStatus_t aDevRS485Init(const aDevConfig_RS485_t *config,
                        aDevHandle_RS485_t *handle)
{
    if ((config == NULL) || (handle == NULL) ||
        (config->de_pin == ADRV_PIN_NONE) || (config->timeout_ms == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }

    aDevRS485HandleStructInit(handle);
    aStatus_t status = aDevUsartInit(&config->usart_config,
                                     &handle->usart_handle);
    if (status != A_STATUS_OK) return status;

    aDrvGpioConfig_t gpio_config;
    aDrvGpioConfigStructInit(&gpio_config);
    gpio_config.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    gpio_config.initial_level = inverse_level(config->transmit_level);
    gpio_config.pin = config->de_pin;
    status = aDrvGpioInit(&gpio_config);
    if (status != A_STATUS_OK) {
        (void)aDevUsartDeInit(&handle->usart_handle);
        return status;
    }
    if (config->re_pin != ADRV_PIN_NONE) {
        gpio_config.pin = config->re_pin;
        status = aDrvGpioInit(&gpio_config);
        if (status != A_STATUS_OK) {
            (void)aDevUsartDeInit(&handle->usart_handle);
            return status;
        }
    }

    handle->de_pin = config->de_pin;
    handle->re_pin = config->re_pin;
    handle->transmit_level = config->transmit_level;
    handle->timeout_ms = config->timeout_ms;
    handle->initialized = 1U;
    return set_direction(handle, 0U);
}

aStatus_t aDevRS485DeInit(aDevHandle_RS485_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->initialized == 0U) return A_STATUS_NOT_READY;
    (void)set_direction(handle, 0U);
    const aStatus_t status = aDevUsartDeInit(&handle->usart_handle);
    if (status != A_STATUS_OK) return status;
    aDevRS485HandleStructInit(handle);
    return A_STATUS_OK;
}

int32_t aDevRS485Read(aDevHandle_RS485_t *handle, void *buffer,
                      uint16_t size)
{
    if ((handle == NULL) || (buffer == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) return A_STATUS_NOT_READY;
    const aStatus_t status = set_direction(handle, 0U);
    if (status != A_STATUS_OK) return status;
    return aDevUsartRead(&handle->usart_handle, buffer, size,
                         handle->timeout_ms);
}

int32_t aDevRS485Write(aDevHandle_RS485_t *handle, const void *buffer,
                       uint16_t size)
{
    if ((handle == NULL) || (buffer == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) return A_STATUS_NOT_READY;
    aStatus_t status = set_direction(handle, 1U);
    if (status != A_STATUS_OK) return status;

    const int32_t written = aDevUsartWrite(&handle->usart_handle, buffer,
                                           size, handle->timeout_ms);
    const aStatus_t complete = written == (int32_t)size ?
        aDevUsartWaitTransmitComplete(&handle->usart_handle,
                                      handle->timeout_ms) : A_STATUS_TIMEOUT;
    const aStatus_t direction = set_direction(handle, 0U);
    if (written < 0) return written;
    if (complete != A_STATUS_OK) return complete;
    return direction == A_STATUS_OK ? written : direction;
}

aStatus_t aDevRS485SetConfig(aDevHandle_RS485_t *handle,
                             const aDevRS485Config_t *config)
{
    if ((handle == NULL) || (config == NULL)) return A_STATUS_INVALID_PARAM;
    if (handle->initialized == 0U) return A_STATUS_NOT_READY;
    aDrvUsartHandle_t *usart = &handle->usart_handle.drv_handle;
    aStatus_t status;
    if ((config->baudrate != 0U) &&
        ((status = aDrvUsartSetBaudrate(usart, config->baudrate)) !=
         A_STATUS_OK)) {
        return status;
    }
    status = aDrvUsartSetStopbits(usart, config->stopbits);
    if (status != A_STATUS_OK) return status;
    status = aDrvUsartSetParity(usart, config->parity);
    if (status != A_STATUS_OK) return status;
    if (config->timeout_ms != 0U) handle->timeout_ms = config->timeout_ms;
    return A_STATUS_OK;
}

aStatus_t aDevRS485GetConfig(const aDevHandle_RS485_t *handle,
                             aDevRS485Config_t *config)
{
    if ((handle == NULL) || (config == NULL)) return A_STATUS_INVALID_PARAM;
    if (handle->initialized == 0U) return A_STATUS_NOT_READY;
    const aDrvUsartHandle_t *usart = &handle->usart_handle.drv_handle;
    aDrvUsartGetBaudrate(usart, &config->baudrate);
    aDrvUsartGetStopbits(usart, &config->stopbits);
    aDrvUsartGetParity(usart, &config->parity);
    config->timeout_ms = handle->timeout_ms;
    return A_STATUS_OK;
}
