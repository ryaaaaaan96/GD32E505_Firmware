#include "aShell.h"

aStatus_t aShellInit(const aShellConfig_t *config)
{
    (void)config;
    return A_STATUS_OK;
}

aStatus_t aShellProcess(void)
{
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

aStatus_t aShellPrintf(const char *format, ...)
{
    (void)format;
    return A_STATUS_OK;
}

aStatus_t aShellWrite(const char *data, size_t size)
{
    (void)data;
    (void)size;
    return A_STATUS_OK;
}

aStatus_t aShellGetOutputStats(aShellOutputStats_t *stats)
{
    if (stats == NULL) return A_STATUS_INVALID_PARAM;
    stats->pending_bytes = 0U;
    stats->dropped_messages = 0U;
    return A_STATUS_OK;
}
