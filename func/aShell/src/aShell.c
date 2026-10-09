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
    aShellContext.reply_status = A_STATUS_OK;
    aShellContext.reply_interrupted = A_FALSE;
    aShellContext.discard_input = A_FALSE;
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
    aShellContext.reply_status = A_STATUS_OK;
    output_status = aShellOutputDrain();
    if (output_status == A_STATUS_OK && aShellContext.reply_interrupted) {
        static const char notice[] =
            "\r\n[shell] reply interrupted; retry command.\r\n"
            ASHELL_PROMPT ": ";
        output_status = aShellOutputReply(notice, sizeof(notice) - 1U);
        if (output_status == A_STATUS_OK)
            aShellContext.reply_interrupted = A_FALSE;
    }
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
            if (aShellContext.discard_input) {
                if (data[i] == '\r' || data[i] == '\n') {
                    aShellContext.discard_input = A_FALSE;
                    aShellContext.previous_cr = data[i] == '\r';
                    aShellNrResetInput();
                }
                continue;
            }
            if (data[i] == '\n' && aShellContext.previous_cr) {
                aShellContext.previous_cr = A_FALSE;
                continue;
            }
            aShellContext.previous_cr = data[i] == '\r';
            aShellNrProcess(data[i]);
        }
    }
    if (input_status == A_STATUS_ERROR && !aShellContext.discard_input) {
        aShellContext.discard_input = A_TRUE;
        (void)ASHELL_REPLY(
            "\r\n[shell] input error; press Enter and retry.\r\n");
    }
    if (output_status == A_STATUS_OK) {
        output_status = aShellOutputDrain();
    }
    if (output_status != A_STATUS_OK) return output_status;
    if (aShellContext.reply_status != A_STATUS_OK)
        return aShellContext.reply_status;
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

aStatus_t aShellWrite(const char *data, size_t size)
{
    if (data == NULL && size != 0U) return A_STATUS_INVALID_PARAM;
    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    return aShellOutputWrite(data, size);
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

aStatus_t aShellReplyWrite(const char *data, size_t size)
{
    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    if (data == NULL && size != 0U) return A_STATUS_INVALID_PARAM;
    if (aShellContext.reply_status == A_STATUS_OK) {
        aShellContext.reply_status = aShellOutputReply(data, size);
        if (aShellContext.reply_status != A_STATUS_OK) {
            aShellOutputDrop();
            aShellContext.reply_interrupted = A_TRUE;
        }
    }
    return aShellContext.reply_status;
}

aStatus_t aShellReplyf(const char *format, ...)
{
    char buffer[ASHELL_PRINT_BUFFER_SIZE];
    va_list arguments;
    int count;

    if (format == NULL) return A_STATUS_INVALID_PARAM;
    if (!aShellContext.ready) return A_STATUS_NOT_READY;
    if (aShellContext.reply_status != A_STATUS_OK)
        return aShellContext.reply_status;
    va_start(arguments, format);
    count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= sizeof(buffer)) {
        aShellOutputDrop();
        aShellContext.reply_interrupted = A_TRUE;
        aShellContext.reply_status = count < 0 ? A_STATUS_ERROR :
                                               A_STATUS_INVALID_PARAM;
        return aShellContext.reply_status;
    }
    return aShellReplyWrite(buffer, (size_t)count);
}
