#include "aDev_usart.h"

#include "aOS.h"

static aErrno_t status_to_errno(aStatus_t status)
{
    switch (status) {
    case A_STATUS_INVALID_PARAM:
        return A_EINVAL;
    case A_STATUS_BUSY:
        return A_EAGAIN;
    case A_STATUS_TIMEOUT:
        return A_ETIMEDOUT;
    case A_STATUS_NOT_READY:
        return A_ENODEV;
    case A_STATUS_UNSUPPORTED:
        return A_ENOTSUP;
    case A_STATUS_OK:
        return A_ERRNO_NONE;
    case A_STATUS_ERROR:
    case A_STATUS_NO_MEMORY:
    default:
        return A_EIO;
    }
}

static aSSize_t fail_with_status(aStatus_t status)
{
    aOSSetErrno(status_to_errno(status));
    return -1;
}

static aSSize_t fail_when_wait_expires(aTimeout_t timeout)
{
    aOSSetErrno(timeout.milliseconds == 0U ? A_EAGAIN : A_ETIMEDOUT);
    return -1;
}

void aDevUsartConfigStructInit(aDevUsartConfig_t *config)
{
    if (config != NULL) {
        aDrvUsartConfigStructInit(&config->drv_config);
    }
}

void aDevUsartHandleStructInit(aDevUsartHandle_t *handle)
{
    if (handle != NULL) {
        aDrvUsartHandleStructInit(&handle->drv_handle);
    }
}

aStatus_t aDevUsartInit(const aDevUsartConfig_t *config,
                        aDevUsartHandle_t *handle)
{
    if ((config == NULL) || (handle == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    return aDrvUsartInitStatic(&config->drv_config, &handle->drv_handle);
}

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    return aDrvUsartDeInitStatic(&handle->drv_handle);
}

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *buffer,
                       size_t buffer_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    size_t count = 0U;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (buffer_size > (size_t)PTRDIFF_MAX) ||
        ((buffer == NULL) && (buffer_size != 0U))) {
        return fail_with_status(A_STATUS_INVALID_PARAM);
    }
    if (buffer_size == 0U) {
        return 0;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    while (count < buffer_size) {
        const aStatus_t status = aDrvUsartTryReadByte(
            &handle->drv_handle, (uint8_t *)buffer + count);

        if (status == A_STATUS_OK) {
            ++count;
            continue;
        }
        if (status != A_STATUS_BUSY) {
            return count != 0U ? (aSSize_t)count : fail_with_status(status);
        }
        if (aTimepointExpired(&end, aOSGetUptimeMs())) {
            return count != 0U ? (aSSize_t)count
                               : fail_when_wait_expires(timeout);
        }
        aOSYield();
    }

    return (aSSize_t)count;
}

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                        size_t data_size, aTimeout_t timeout)
{
    aTimepoint_t end;
    size_t count = 0U;

    if ((handle == NULL) || !aTimeoutIsValid(timeout) ||
        (data_size > (size_t)PTRDIFF_MAX) ||
        ((data == NULL) && (data_size != 0U))) {
        return fail_with_status(A_STATUS_INVALID_PARAM);
    }
    if (data_size == 0U) {
        return 0;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    while (count < data_size) {
        const aStatus_t status = aDrvUsartTryWriteByte(
            &handle->drv_handle, ((const uint8_t *)data)[count]);

        if (status == A_STATUS_OK) {
            ++count;
            continue;
        }
        if (status != A_STATUS_BUSY) {
            return count != 0U ? (aSSize_t)count : fail_with_status(status);
        }
        if (aTimepointExpired(&end, aOSGetUptimeMs())) {
            return count != 0U ? (aSSize_t)count
                               : fail_when_wait_expires(timeout);
        }
        aOSYield();
    }

    return (aSSize_t)count;
}

aStatus_t aDevUsartWaitTransmitComplete(aDevUsartHandle_t *handle,
                                         aTimeout_t timeout)
{
    aTimepoint_t end;

    if ((handle == NULL) || !aTimeoutIsValid(timeout)) {
        return A_STATUS_INVALID_PARAM;
    }

    end = aTimepointCalc(timeout, aOSGetUptimeMs());
    for (;;) {
        bool complete;
        const aStatus_t status = aDrvUsartIsTransmitComplete(
            &handle->drv_handle, &complete);

        if (status != A_STATUS_OK) {
            return status;
        }
        if (complete) {
            return A_STATUS_OK;
        }
        if (aTimepointExpired(&end, aOSGetUptimeMs())) {
            return timeout.milliseconds == 0U ? A_STATUS_BUSY
                                              : A_STATUS_TIMEOUT;
        }
        aOSYield();
    }
}

aStatus_t aDevUsartRegisterCallback(aDevUsartHandle_t *handle,
                                     const aDrvUsartExtiConfig_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }
    return aDrvUsartRegisterCallback(&handle->drv_handle, config);
}

aStatus_t aDevUsartUnregisterCallback(aDevUsartHandle_t *handle,
                                       aDrvUsartExti_t type)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }
    return aDrvUsartUnregisterCallback(&handle->drv_handle, type);
}

void aDevUsartEnableInterrupt(aDevUsartHandle_t *handle)
{
    if (handle != NULL) {
        aDrvUsartEnableInterrupt(&handle->drv_handle);
    }
}

void aDevUsartDisableInterrupt(aDevUsartHandle_t *handle)
{
    if (handle != NULL) {
        aDrvUsartDisableInterrupt(&handle->drv_handle);
    }
}
