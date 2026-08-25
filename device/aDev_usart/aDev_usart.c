#include "aDev_usart.h"
#include "aLib.h"
#include "aOS.h"

#define ADEV_USART_RETRY_DELAY_MS 1U

void aDevUsartConfigStructInit(aDevConfig_Usart_t *config)
{
    if (config != NULL) aDrvUsartConfigStructInit(&config->drv_config);
}

void aDevUsartHandleStructInit(aDevHandle_Usart_t *handle)
{
    if (handle != NULL) aDrvUsartHandleStructInit(&handle->drv_handle);
}

aStatus_t aDevUsartInit(const aDevConfig_Usart_t *config,
                        aDevHandle_Usart_t *handle)
{
    if ((config == NULL) || (handle == NULL)) return A_STATUS_INVALID_PARAM;
    return aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
}

aStatus_t aDevUsartDeInit(aDevHandle_Usart_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return aDrvUsartDeInitStatic(&handle->drv_handle);
}

int32_t aDevUsartRead(aDevHandle_Usart_t *handle, void *buffer,
                      uint16_t size, uint32_t timeout_ms)
{
    if ((handle == NULL) || (buffer == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    uint16_t count = 0U;
    const uint32_t start = aOSGetTickMs();
    while (count < size) {
        const int32_t received = aDrvUsartReadByte(
            &handle->drv_handle, (uint8_t *)buffer + count);
        if (received < 0) return received;
        if (received > 0) {
            count = (uint16_t)(count + (uint16_t)received);
            continue;
        }
        if ((timeout_ms != ALIB_WAIT_FOREVER) &&
            ((uint32_t)(aOSGetTickMs() - start) >= timeout_ms)) break;
        aOSDelayMs(ADEV_USART_RETRY_DELAY_MS);
    }
    return (int32_t)count;
}

int32_t aDevUsartWrite(aDevHandle_Usart_t *handle, const void *buffer,
                       uint16_t size, uint32_t timeout_ms)
{
    if ((handle == NULL) || (buffer == NULL) || (size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    uint16_t count = 0U;
    const uint32_t start = aOSGetTickMs();
    while (count < size) {
        const int32_t sent = aDrvUsartWriteByte(
            &handle->drv_handle, (const uint8_t *)buffer + count);
        if (sent < 0) return sent;
        if (sent > 0) {
            count = (uint16_t)(count + (uint16_t)sent);
            continue;
        }
        if ((timeout_ms != ALIB_WAIT_FOREVER) &&
            ((uint32_t)(aOSGetTickMs() - start) >= timeout_ms)) break;
        aOSDelayMs(ADEV_USART_RETRY_DELAY_MS);
    }
    return (int32_t)count;
}

aStatus_t aDevUsartWaitTransmitComplete(aDevHandle_Usart_t *handle,
                                         uint32_t timeout_ms)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return aDrvUsartWaitTransmitComplete(&handle->drv_handle, timeout_ms);
}

aStatus_t aDevUsartRegisterCallback(aDevHandle_Usart_t *handle,
                                     const aDrvUsartExtiConfig_t *config)
{
    if ((handle == NULL) || (config == NULL)) return A_STATUS_INVALID_PARAM;
    return aDrvUsartRegisterCallback(&handle->drv_handle, config);
}

aStatus_t aDevUsartUnregisterCallback(aDevHandle_Usart_t *handle,
                                       aDrvUsartExti_t type)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    return aDrvUsartUnregisterCallback(&handle->drv_handle, type);
}

void aDevUsartEnableInterrupt(aDevHandle_Usart_t *handle)
{
    if (handle != NULL) aDrvUsartEnableInterrupt(&handle->drv_handle);
}

void aDevUsartDisableInterrupt(aDevHandle_Usart_t *handle)
{
    if (handle != NULL) aDrvUsartDisableInterrupt(&handle->drv_handle);
}
