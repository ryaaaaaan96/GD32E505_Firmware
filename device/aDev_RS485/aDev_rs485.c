#include "aDev_rs485.h"

#include "aOS.h"

static aDrvGpioLevel_t inverse_level(aDrvGpioLevel_t level)
{
    return level == ADRV_GPIO_HIGH ? ADRV_GPIO_LOW : ADRV_GPIO_HIGH;
}

static aStatus_t set_direction(aDevRS485Handle_t *handle,
                               uint8_t transmit)
{
    const aDrvGpioLevel_t level =
        transmit != 0U ? handle->transmit_level
                       : inverse_level(handle->transmit_level);
    aStatus_t status = aDrvGpioWrite(handle->de_pin, level);

    if (status != A_STATUS_OK) {
        return status;
    }
    if ((handle->re_pin != ADRV_PIN_NONE) &&
        ((status = aDrvGpioWrite(handle->re_pin, level)) != A_STATUS_OK)) {
        return status;
    }
    return A_STATUS_OK;
}

void aDevRS485ConfigStructInit(aDevRS485Config_t *config)
{
    if (config == NULL) {
        return;
    }
    aDevUsartConfigStructInit(&config->usart_config);
    config->de_pin = ADRV_PIN_NONE;
    config->re_pin = ADRV_PIN_NONE;
    config->transmit_level = ADRV_GPIO_HIGH;
}

void aDevRS485HandleStructInit(aDevRS485Handle_t *handle)
{
    if (handle == NULL) {
        return;
    }
    aDevUsartHandleStructInit(&handle->usart_handle);
    handle->de_pin = ADRV_PIN_NONE;
    handle->re_pin = ADRV_PIN_NONE;
    handle->transmit_level = ADRV_GPIO_HIGH;
    handle->initialized = 0U;
}

aStatus_t aDevRS485Init(const aDevRS485Config_t *config,
                        aDevRS485Handle_t *handle)
{
    if ((config == NULL) || (handle == NULL) ||
        (config->de_pin == ADRV_PIN_NONE)) {
        return A_STATUS_INVALID_PARAM;
    }

    aDevRS485HandleStructInit(handle);
    aStatus_t status = aDevUsartInit(&config->usart_config,
                                     &handle->usart_handle);
    if (status != A_STATUS_OK) {
        return status;
    }

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
    handle->initialized = 1U;
    return set_direction(handle, 0U);
}

aStatus_t aDevRS485DeInit(aDevRS485Handle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    (void)set_direction(handle, 0U);
    const aStatus_t status = aDevUsartDeInit(&handle->usart_handle);
    if (status != A_STATUS_OK) {
        return status;
    }
    aDevRS485HandleStructInit(handle);
    return A_STATUS_OK;
}

aSSize_t aDevRS485Read(aDevRS485Handle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout)
{
    aStatus_t status;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (buffer_size > (size_t)PTRDIFF_MAX) ||
        ((buffer == NULL) && (buffer_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (handle->initialized == 0U) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (buffer_size == 0U) {
        return 0;
    }
    status = set_direction(handle, 0U);
    if (status != A_STATUS_OK) {
        return aOSFailWithStatus(status);
    }
    return aDevUsartRead(&handle->usart_handle, buffer, buffer_size,
                         timeout);
}

aSSize_t aDevRS485Write(aDevRS485Handle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    aSSize_t written;
    aStatus_t status;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (data_size > (size_t)PTRDIFF_MAX) ||
        ((data == NULL) && (data_size != 0U))) {
        return aOSFailWithStatus(A_STATUS_INVALID_PARAM);
    }
    if (handle->initialized == 0U) {
        return aOSFailWithStatus(A_STATUS_NOT_READY);
    }
    if (data_size == 0U) {
        return 0;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = set_direction(handle, 1U);
    if (status != A_STATUS_OK) {
        return aOSFailWithStatus(status);
    }

    written = aDevUsartWrite(
        &handle->usart_handle, data, data_size,
        aTimepointRemaining(&end, aOSGetUptimeMs()));
    if (written == (aSSize_t)data_size) {
        status = aDevUsartWaitTransmitComplete(
            &handle->usart_handle,
            aTimepointRemaining(&end, aOSGetUptimeMs()));
    }

    {
        const aStatus_t direction_status = set_direction(handle, 0U);

        if (written < 0) {
            return written;
        }
        if (written > 0) {
            return written;
        }
        if (status != A_STATUS_OK) {
            return aOSFailWithStatus(status);
        }
        if (direction_status != A_STATUS_OK) {
            return aOSFailWithStatus(direction_status);
        }
    }

    return written;
}

aStatus_t aDevRS485SetLineConfig(aDevRS485Handle_t *handle,
                                 const aDevRS485LineConfig_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    aDrvUsartHandle_t *usart = &handle->usart_handle.drv_handle;
    aStatus_t status;
    if ((config->baudrate != 0U) &&
        ((status = aDrvUsartSetBaudrate(usart, config->baudrate)) !=
         A_STATUS_OK)) {
        return status;
    }
    status = aDrvUsartSetStopbits(usart, config->stopbits);
    if (status != A_STATUS_OK) {
        return status;
    }
    status = aDrvUsartSetParity(usart, config->parity);
    return status;
}

aStatus_t aDevRS485GetLineConfig(const aDevRS485Handle_t *handle,
                                 aDevRS485LineConfig_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (handle->initialized == 0U) {
        return A_STATUS_NOT_READY;
    }
    const aDrvUsartHandle_t *usart = &handle->usart_handle.drv_handle;
    aDrvUsartGetBaudrate(usart, &config->baudrate);
    aDrvUsartGetStopbits(usart, &config->stopbits);
    aDrvUsartGetParity(usart, &config->parity);
    return A_STATUS_OK;
}
