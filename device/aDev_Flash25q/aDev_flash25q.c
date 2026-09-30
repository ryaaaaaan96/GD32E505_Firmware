#include "aDev_flash25q_internal.h"

#include <string.h>

/* SFUD 的页编程静态缓冲区由所有实例共享。生命周期由应用串行编排。 */
static aOSMutex_t sfud_mutex;
static size_t bus_count;

static aStatus_t timeout_check(aTimeout_t timeout)
{
    if (!aTimeoutIsValid(timeout)) return A_STATUS_INVALID_PARAM;
    if ((timeout.type == A_TIMEOUT_TYPE_RELATIVE) &&
        (timeout.milliseconds == 0U)) return A_STATUS_UNSUPPORTED;
    return A_STATUS_OK;
}

static aStatus_t result_get(aDevFlash25qHandle_t *handle, sfud_err result)
{
    if (handle->port_error != A_STATUS_OK) return handle->port_error;
    switch (result) {
    case SFUD_SUCCESS: return A_STATUS_OK;
    case SFUD_ERR_NOT_FOUND: return A_STATUS_NOT_FOUND;
    case SFUD_ERR_TIMEOUT: return A_STATUS_TIMEOUT;
    case SFUD_ERR_ADDR_OUT_OF_BOUND: return A_STATUS_INVALID_PARAM;
    default: return A_STATUS_ERROR;
    }
}

static aStatus_t operation_begin(aDevFlash25qHandle_t *handle,
                                 aTimeout_t timeout)
{
    aTimepoint_t deadline;
    aStatus_t status;

    status = timeout_check(timeout);
    if (status != A_STATUS_OK) return status;
    deadline = aTimepointCalc(timeout, aOSGetUptimeMs());
    status = aOSMutexLock(sfud_mutex,
        aTimepointRemaining(&deadline, aOSGetUptimeMs()));
    if (status != A_STATUS_OK) return status;
    /* 取得锁之后再写实例的本次操作状态，避免并发调用覆盖 deadline。 */
    handle->deadline = deadline;
    handle->port_error = A_STATUS_OK;
    return A_STATUS_OK;
}

static aStatus_t operation_end(aDevFlash25qHandle_t *handle, sfud_err result)
{
    aStatus_t status = result_get(handle, result);
    aStatus_t unlock_status = aOSMutexUnlock(sfud_mutex);

    return status == A_STATUS_OK ? unlock_status : status;
}

void aDevFlash25qBusConfigStructInit(aDevFlash25qBusConfig_t *config)
{
    if (config == NULL) return;
    aDrvSpiConfigStructInit(&config->spi);
    config->spi.prescaler = 64U;
}

void aDevFlash25qConfigStructInit(aDevFlash25qConfig_t *config)
{
    if (config == NULL) return;
    config->bus = NULL;
    config->cs_pin = ADRV_PIN_NONE;
    config->expected_capacity = 0U;
    config->timeout = A_TIMEOUT_MS(1000U);
}

void aDevFlash25qReadRequestStructInit(aDevFlash25qReadRequest_t *request)
{
    if (request == NULL) return;
    request->address = 0U;
    request->data = NULL;
    request->size = 0U;
    request->timeout = A_TIMEOUT_MS(1000U);
}

void aDevFlash25qWriteRequestStructInit(aDevFlash25qWriteRequest_t *request)
{
    if (request == NULL) return;
    request->address = 0U;
    request->data = NULL;
    request->size = 0U;
    request->timeout = A_TIMEOUT_MS(1000U);
}

void aDevFlash25qEraseRequestStructInit(aDevFlash25qEraseRequest_t *request)
{
    if (request == NULL) return;
    request->address = 0U;
    request->size = 0U;
    request->timeout = A_TIMEOUT_MS(5000U);
}

aStatus_t aDevFlash25qBusInitStatic(
    const aDevFlash25qBusConfig_t *config,
    aDevFlash25qBus_t *bus)
{
    aStatus_t status;

    if ((config == NULL) || (bus == NULL)) return A_STATUS_INVALID_PARAM;
    if ((config->spi.mode != ADRV_SPI_MODE_MASTER) ||
        (config->spi.dataBits != 8U) ||
        (config->spi.bitOrder != ADRV_SPI_BITORDER_MSB) ||
        (config->spi.csMode != ADRV_SPI_CS_SOFT) ||
        (config->spi.csPin != ADRV_PIN_NONE)) return A_STATUS_UNSUPPORTED;
    memset(bus, 0, sizeof(*bus));
    aDrvSpiHandleStructInit(&bus->spi);
    status = aOSMutexCreate(&bus->mutex);
    if (status != A_STATUS_OK) return status;
    if (bus_count == 0U) {
        status = aOSMutexCreate(&sfud_mutex);
        if (status != A_STATUS_OK) goto fail;
    }
    status = aDrvSpiInitStatic(&config->spi, &bus->spi);
    if (status != A_STATUS_OK) {
        if (bus_count == 0U) aOSMutexDestroy(&sfud_mutex);
        goto fail;
    }
    ++bus_count;
    return A_STATUS_OK;
fail:
    aOSMutexDestroy(&bus->mutex);
    return status;
}

aStatus_t aDevFlash25qBusDeInitStatic(aDevFlash25qBus_t *bus)
{
    aStatus_t status;

    if (bus == NULL) return A_STATUS_INVALID_PARAM;
    if (!bus->spi.initialized) return A_STATUS_NOT_READY;
    if (bus->references != 0U) return A_STATUS_BUSY;
    status = aDrvSpiDeInitStatic(&bus->spi);
    if (status != A_STATUS_OK) return status;
    aOSMutexDestroy(&bus->mutex);
    --bus_count;
    if (bus_count == 0U) aOSMutexDestroy(&sfud_mutex);
    return A_STATUS_OK;
}

static aStatus_t initialize(const aDevFlash25qConfig_t *config,
                            aDevFlash25qHandle_t *handle)
{
    aDrvGpioConfig_t gpio;
    aStatus_t status;
    sfud_err result;

    if ((config == NULL) || (handle == NULL) ||
        (config->bus == NULL) || (config->cs_pin == ADRV_PIN_NONE))
        return A_STATUS_INVALID_PARAM;
    if (!config->bus->spi.initialized || config->bus->fault)
        return A_STATUS_NOT_READY;
    status = timeout_check(config->timeout);
    if (status != A_STATUS_OK) return status;
    memset(handle, 0, sizeof(*handle));
    handle->bus = config->bus;
    handle->flash.name = "Flash25Q";
    handle->flash.spi.name = "SPI";
    handle->flash.user_data = handle;
    aDrvGpioConfigStructInit(&gpio);
    gpio.pin = config->cs_pin;
    gpio.mode = ADRV_GPIO_OUTPUT_PUSH_PULL;
    gpio.initial_level = ADRV_GPIO_HIGH;
    status = aDrvGpioInit(&gpio, &handle->cs);
    if (status != A_STATUS_OK) return status;
    status = operation_begin(handle, config->timeout);
    if (status == A_STATUS_OK) {
        result = sfud_device_init(&handle->flash);
        status = operation_end(handle, result);
    }
    if ((status == A_STATUS_OK) &&
        ((handle->flash.chip.erase_gran == 0U) ||
         ((config->expected_capacity != 0U) &&
          (config->expected_capacity != handle->flash.chip.capacity))))
        status = A_STATUS_UNSUPPORTED;
    if (status != A_STATUS_OK) {
        (void)aDrvGpioDeInit(&handle->cs);
        handle->flash.init_ok = false;
        return status;
    }
    ++handle->bus->references;
    return A_STATUS_OK;
}

static aStatus_t deinitialize(aDevFlash25qHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->flash.init_ok) return A_STATUS_NOT_READY;
    status = aDrvGpioWrite(&handle->cs, ADRV_GPIO_HIGH);
    if (status != A_STATUS_OK) return status;
    status = aDrvGpioDeInit(&handle->cs);
    if (status != A_STATUS_OK) return status;
    --handle->bus->references;
    handle->flash.init_ok = false;
    return A_STATUS_OK;
}

#if ADEV_FLASH25Q_STATIC_ENABLE
aStatus_t aDevFlash25qInitStatic(
    const aDevFlash25qConfig_t *config, aDevFlash25qHandle_t *handle)
{
    return initialize(config, handle);
}

aStatus_t aDevFlash25qDeInitStatic(aDevFlash25qHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->dynamic) return A_STATUS_INVALID_PARAM;
    return deinitialize(handle);
}
#endif

#if ADEV_FLASH25Q_DYNAMIC_ENABLE
aStatus_t aDevFlash25qCreate(
    const aDevFlash25qConfig_t *config, aDevFlash25qHandle_t **handle_out)
{
    aDevFlash25qHandle_t *handle;
    aStatus_t status;

    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    if (config == NULL) return A_STATUS_INVALID_PARAM;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = initialize(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    handle->dynamic = A_TRUE;
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aDevFlash25qDestroy(aDevFlash25qHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->dynamic) return A_STATUS_INVALID_PARAM;
    status = deinitialize(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

static aStatus_t range_check(aDevFlash25qHandle_t *handle,
                             uint32_t address, size_t size)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->flash.init_ok)
        return A_STATUS_NOT_READY;
    if ((size == 0U) || (address > handle->flash.chip.capacity) ||
        (size > handle->flash.chip.capacity - address))
        return A_STATUS_INVALID_PARAM;
    return A_STATUS_OK;
}

aStatus_t aDevFlash25qRead(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qReadRequest_t *request)
{
    aStatus_t status;
    sfud_err result;

    if ((request == NULL) || (request->data == NULL))
        return A_STATUS_INVALID_PARAM;
    status = range_check(handle, request->address, request->size);
    if (status != A_STATUS_OK) return status;
    status = operation_begin(handle, request->timeout);
    if (status != A_STATUS_OK) return status;
    result = sfud_read(&handle->flash, request->address,
                       request->size, request->data);
    return operation_end(handle, result);
}

aStatus_t aDevFlash25qWrite(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qWriteRequest_t *request)
{
    aStatus_t status;
    sfud_err result;

    if ((request == NULL) || (request->data == NULL))
        return A_STATUS_INVALID_PARAM;
    status = range_check(handle, request->address, request->size);
    if (status != A_STATUS_OK) return status;
    status = operation_begin(handle, request->timeout);
    if (status != A_STATUS_OK) return status;
    result = sfud_write(&handle->flash, request->address,
                        request->size, request->data);
    return operation_end(handle, result);
}

aStatus_t aDevFlash25qErase(
    aDevFlash25qHandle_t *handle,
    const aDevFlash25qEraseRequest_t *request)
{
    aStatus_t status;
    sfud_err result;
    uint32_t unit;

    if (request == NULL) return A_STATUS_INVALID_PARAM;
    status = range_check(handle, request->address, request->size);
    if (status != A_STATUS_OK) return status;
    unit = handle->flash.chip.erase_gran;
    if ((request->address % unit != 0U) || (request->size % unit != 0U))
        return A_STATUS_INVALID_PARAM;
    status = operation_begin(handle, request->timeout);
    if (status != A_STATUS_OK) return status;
    result = sfud_erase(&handle->flash, request->address, request->size);
    return operation_end(handle, result);
}

aStatus_t aDevFlash25qGetInfo(
    const aDevFlash25qHandle_t *handle, aDevFlash25qInfo_t *info)
{
    if ((handle == NULL) || (info == NULL)) return A_STATUS_INVALID_PARAM;
    if (!handle->flash.init_ok) return A_STATUS_NOT_READY;
    info->capacity = handle->flash.chip.capacity;
    info->erase_size = handle->flash.chip.erase_gran;
    info->manufacturer_id = handle->flash.chip.mf_id;
    info->memory_type = handle->flash.chip.type_id;
    info->capacity_id = handle->flash.chip.capacity_id;
    return A_STATUS_OK;
}
