#include "aShell.h"
#include "shell.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ASHELL_PRINT_BUFFER_SIZE 256U
#define ASHELL_MIN_BUFFER_SIZE 64U

static SemaphoreHandle_t s_shell_mutex;
static uint32_t s_shell_count;

static int shell_lock(Shell *shell)
{
    (void)shell;
    if (s_shell_mutex == NULL) return -1;
    return xSemaphoreTakeRecursive(s_shell_mutex, portMAX_DELAY) == pdTRUE ?
           0 : -1;
}

static int shell_unlock(Shell *shell)
{
    (void)shell;
    if (s_shell_mutex == NULL) return -1;
    return xSemaphoreGiveRecursive(s_shell_mutex) == pdTRUE ? 0 : -1;
}

void aShellConfigStructInit(aShellConfig_t *config)
{
    if (config == NULL) return;
    config->read = NULL;
    config->write = NULL;
    config->buffer_size = 256U;
    config->task_stack_size = 512U;
    config->task_priority = (uint32_t)(tskIDLE_PRIORITY + 2U);
}

void aShellHandleStructInit(aShellHandle_t *handle)
{
    if (handle == NULL) return;
    handle->shell_obj = NULL;
    handle->buffer = NULL;
    handle->task_handle = NULL;
}

aStatus_t aShellInit(aShellHandle_t *handle,
                     const aShellConfig_t *config)
{
    if ((handle == NULL) || (config == NULL) || (config->read == NULL) ||
        (config->write == NULL) ||
        (config->buffer_size < ASHELL_MIN_BUFFER_SIZE) ||
        (config->task_stack_size == 0U) ||
        (config->task_priority >= configMAX_PRIORITIES)) {
        return A_STATUS_INVALID_PARAM;
    }

    aShellHandleStructInit(handle);
    Shell *shell = pvPortMalloc(sizeof(*shell));
    if (shell == NULL) return A_STATUS_NO_MEMORY;
    char *buffer = pvPortMalloc(config->buffer_size);
    if (buffer == NULL) {
        vPortFree(shell);
        return A_STATUS_NO_MEMORY;
    }

    if (s_shell_mutex == NULL) {
        s_shell_mutex = xSemaphoreCreateRecursiveMutex();
        if (s_shell_mutex == NULL) {
            vPortFree(buffer);
            vPortFree(shell);
            return A_STATUS_NO_MEMORY;
        }
    }

    memset(shell, 0, sizeof(*shell));
    shell->lock = shell_lock;
    shell->unlock = shell_unlock;
    shell->read = config->read;
    shell->write = config->write;
    shellInit(shell, buffer, config->buffer_size);

    TaskHandle_t task = NULL;
    if (xTaskCreate(shellTask, "shell", config->task_stack_size, shell,
                    (UBaseType_t)config->task_priority, &task) != pdPASS) {
        shellRemove(shell);
        vPortFree(buffer);
        vPortFree(shell);
        if (s_shell_count == 0U) {
            vSemaphoreDelete(s_shell_mutex);
            s_shell_mutex = NULL;
        }
        return A_STATUS_NO_MEMORY;
    }

    handle->shell_obj = shell;
    handle->buffer = buffer;
    handle->task_handle = task;
    ++s_shell_count;
    return A_STATUS_OK;
}

aStatus_t aShellDeInit(aShellHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->shell_obj == NULL) return A_STATUS_NOT_READY;

    Shell *shell = (Shell *)handle->shell_obj;
    if (handle->task_handle != NULL) {
        vTaskDelete((TaskHandle_t)handle->task_handle);
    }
    shellRemove(shell);
    vPortFree(handle->buffer);
    vPortFree(shell);
    aShellHandleStructInit(handle);

    if (s_shell_count > 0U) --s_shell_count;
    if ((s_shell_count == 0U) && (s_shell_mutex != NULL)) {
        vSemaphoreDelete(s_shell_mutex);
        s_shell_mutex = NULL;
    }
    return A_STATUS_OK;
}

void aShellPrint(aShellHandle_t *handle, const char *format, ...)
{
    if ((handle == NULL) || (handle->shell_obj == NULL) ||
        (format == NULL)) return;

    char buffer[ASHELL_PRINT_BUFFER_SIZE];
    va_list arguments;
    va_start(arguments, format);
    const int count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count <= 0) return;
    buffer[sizeof(buffer) - 1U] = '\0';

    Shell *shell = (Shell *)handle->shell_obj;
    (void)SHELL_LOCK(shell);
    shellWriteString(shell, buffer);
    (void)SHELL_UNLOCK(shell);
}
