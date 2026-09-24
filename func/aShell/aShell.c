#include "aShell.h"
#include "shell.h"

#include "aOS.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define ASHELL_PRINT_BUFFER_SIZE 256U
#define ASHELL_MIN_BUFFER_SIZE 64U

static Shell *s_shell;
static char *s_shell_buffer;
static aOSTaskHandle_t s_shell_task;
static aOSRecursiveMutex_t s_shell_mutex;

static int shell_lock(Shell *shell)
{
    (void)shell;
    return aOSRecursiveMutexLock(s_shell_mutex, A_TIMEOUT_FOREVER) ==
                   A_STATUS_OK
               ? 0
               : -1;
}

static int shell_unlock(Shell *shell)
{
    (void)shell;
    return aOSRecursiveMutexUnlock(s_shell_mutex) == A_STATUS_OK ? 0 : -1;
}

void aShellConfigStructInit(aShellConfig_t *config)
{
    if (config == NULL) return;
    config->read = NULL;
    config->write = NULL;
    config->buffer_size = 256U;
    config->task_stack_size = 512U;
    config->task_priority = AOS_TASK_PRIO_LOW;
}

aStatus_t aShellInit(const aShellConfig_t *config)
{
    aStatus_t status;
    Shell *shell;
    char *buffer;

    if ((config == NULL) || (config->read == NULL) ||
        (config->write == NULL) ||
        (config->buffer_size < ASHELL_MIN_BUFFER_SIZE) ||
        (config->task_stack_size == 0U)) {
        return A_STATUS_INVALID_PARAM;
    }
    if (s_shell != NULL) {
        return A_STATUS_BUSY;
    }

    if (s_shell_mutex == NULL) {
        status = aOSRecursiveMutexCreate(&s_shell_mutex);
        if (status != A_STATUS_OK) return status;
    }

    shell = aOSAlloc(sizeof(*shell));
    if (shell == NULL) return A_STATUS_NO_MEMORY;
    buffer = aOSAlloc(config->buffer_size);
    if (buffer == NULL) {
        aOSFree(shell);
        return A_STATUS_NO_MEMORY;
    }

    memset(shell, 0, sizeof(*shell));
    shell->lock = shell_lock;
    shell->unlock = shell_unlock;
    shell->read = config->read;
    shell->write = config->write;
    shellInit(shell, buffer, config->buffer_size);

    status = aOSCreateTask(shellTask, "shell", config->task_stack_size,
                           shell, config->task_priority, &s_shell_task);
    if (status != A_STATUS_OK) {
        shellRemove(shell);
        aOSFree(buffer);
        aOSFree(shell);
        s_shell_task = NULL;
        return status;
    }

    s_shell = shell;
    s_shell_buffer = buffer;
    return A_STATUS_OK;
}

aStatus_t aShellDeInit(void)
{
    Shell *shell;
    aStatus_t status;

    if (s_shell == NULL) return A_STATUS_NOT_READY;
    status = aOSRecursiveMutexLock(s_shell_mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;

    shell = s_shell;
    s_shell = NULL;
    aOSDeleteTask(s_shell_task);
    s_shell_task = NULL;
    shellRemove(shell);
    aOSFree(s_shell_buffer);
    aOSFree(shell);
    s_shell_buffer = NULL;

    (void)aOSRecursiveMutexUnlock(s_shell_mutex);
    return A_STATUS_OK;
}

aBool_t aShellIsEnabled(void)
{
    return A_TRUE;
}

void aShellPrint(const char *format, ...)
{
    char buffer[ASHELL_PRINT_BUFFER_SIZE];
    va_list arguments;
    int count;

    if (format == NULL) return;
    va_start(arguments, format);
    count = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (count <= 0) return;
    buffer[sizeof(buffer) - 1U] = '\0';

    if ((s_shell == NULL) || (shell_lock(s_shell) != 0)) return;
    if (s_shell != NULL) shellWriteString(s_shell, buffer);
    (void)shell_unlock(s_shell);
}
