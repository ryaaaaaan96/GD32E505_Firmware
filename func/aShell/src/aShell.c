#include "aShell_internal.h"
#include "aShell_config.h"

#include <stdarg.h>
#include <stdio.h>

aShellContext_t aShellContext;

aStatus_t aShellIoError(void)
{
    aErrno_t error = aOSGetErrno();

    if (error == A_EAGAIN) return A_STATUS_BUSY;
    if (error == A_ETIMEDOUT) return A_STATUS_TIMEOUT;
    return A_STATUS_ERROR;
}

aStatus_t aShellInit(const aShellConfig_t *config)
{
    aStatus_t status;

    if (aShellContext.ready) return A_STATUS_BUSY;
    if (config == NULL || config->stream.read == NULL ||
        config->stream.write == NULL ||
        !aTimeoutIsValid(config->read_timeout) ||
        config->read_timeout.type == A_TIMEOUT_TYPE_FOREVER ||
        !aTimeoutIsValid(config->write_timeout) ||
        !aShellNrCommandsAreValid()) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aShellOutputInit();
    if (status != A_STATUS_OK) return status;
    aShellContext.config = *config;
    aShellContext.previous_cr = A_FALSE;
    status = aShellNrInit(); /* Initial prompt is queued, never sent here. */
    if (status != A_STATUS_OK) {
        aShellOutputDeInit();
        aShellConfigStructInit(&aShellContext.config);
        return status;
    }
    aShellContext.ready = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aShellProcess(void)
{
    char data[64];
    aSSize_t count;
    aSSize_t i;
    aStatus_t input_status;
    aStatus_t output_status;

    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    output_status = aShellOutputDrain();
    count = aShellContext.config.stream.read(
        data, sizeof(data), aShellContext.config.read_timeout);
    if (count < -1 || count > (aSSize_t)sizeof(data)) {
        input_status = A_STATUS_ERROR;
    } else if (count < 0) {
        input_status = aShellIoError();
    } else if (count == 0) {
        input_status = A_STATUS_BUSY;
    } else {
        input_status = A_STATUS_OK;
        for (i = 0; i < count; ++i) {
            if (data[i] == '\n' && aShellContext.previous_cr) {
                aShellContext.previous_cr = A_FALSE;
                continue;
            }
            aShellContext.previous_cr = data[i] == '\r';
            aShellNrProcess(data[i]);
        }
    }
    if (output_status == A_STATUS_OK) {
        output_status = aShellOutputDrain();
    }
    if (output_status != A_STATUS_OK) return output_status;
    return input_status;
}

aStatus_t aShellDeInit(void)
{
    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    aShellContext.ready = A_FALSE;
    aShellOutputDeInit(); /* Caller quiesces producers and consumer first. */
    aShellConfigStructInit(&aShellContext.config);
    return A_STATUS_OK;
}

aBool_t aShellIsEnabled(void)
{
    return A_TRUE;
}

aStatus_t aShellPrintf(const char *format, ...)
{
    char buffer[ASHELL_PRINT_BUFFER_SIZE];
    va_list arguments;
    int count;

    if (format == NULL) return A_STATUS_INVALID_PARAM;
    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    va_start(arguments, format);
    count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= sizeof(buffer)) {
        aShellOutputDrop();
        return count < 0 ? A_STATUS_ERROR : A_STATUS_INVALID_PARAM;
    }
    return aShellOutputWrite(buffer, (size_t)count);
}
