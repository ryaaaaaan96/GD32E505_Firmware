#include "aShell.h"

#include <stddef.h>

void aShellConfigStructInit(aShellConfig_t *config)
{
    if (config == NULL) {
        return;
    }

    config->read = NULL;
    config->write = NULL;
    config->buffer_size = 256U;
    config->task_stack_size = 512U;
    config->task_priority = 2U;
}

void aShellHandleStructInit(aShellHandle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->shell_obj = NULL;
    handle->buffer = NULL;
    handle->task_handle = NULL;
}

aStatus_t aShellInit(aShellHandle_t *handle,
                     const aShellConfig_t *config)
{
    if ((handle == NULL) || (config == NULL)) {
        return A_STATUS_INVALID_PARAM;
    }

    aShellHandleStructInit(handle);
    return A_STATUS_OK;
}

aStatus_t aShellDeInit(aShellHandle_t *handle)
{
    if (handle == NULL) {
        return A_STATUS_INVALID_PARAM;
    }

    aShellHandleStructInit(handle);
    return A_STATUS_OK;
}

aBool_t aShellIsEnabled(void)
{
    return A_FALSE;
}

void aShellPrint(aShellHandle_t *handle, const char *format, ...)
{
    (void)handle;
    (void)format;
}
