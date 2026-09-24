#include "aShell.h"

void aShellConfigStructInit(aShellConfig_t *config)
{
    if (config == NULL) return;
    config->read = NULL;
    config->write = NULL;
    config->buffer_size = 256U;
    config->task_stack_size = 512U;
    config->task_priority = 2U;
}

aStatus_t aShellInit(const aShellConfig_t *config)
{
    (void)config;
    return A_STATUS_OK;
}

aStatus_t aShellDeInit(void)
{
    return A_STATUS_OK;
}

aBool_t aShellIsEnabled(void)
{
    return A_FALSE;
}

void aShellPrint(const char *format, ...)
{
    (void)format;
}
