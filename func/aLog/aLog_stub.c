#include "aLog.h"

/* 关闭模块时保留公共符号，不创建 OS 对象，也不调用输出后端。 */
aStatus_t aLogInit(const aLogConfig_t *config)
{
    (void)config;
    return A_STATUS_OK;
}
aStatus_t aLogDeInit(void) { return A_STATUS_OK; }
aStatus_t aLogSetLevel(aLogLevel_t level)
{
    (void)level;
    return A_STATUS_OK;
}
aStatus_t aLogWrite(aLogLevel_t level, const char *tag,
                   const char *format, ...)
{
    (void)level;
    (void)tag;
    (void)format;
    return A_STATUS_OK;
}
aStatus_t aLogHexDump(const char *tag, const void *data, size_t size)
{
    (void)tag;
    (void)data;
    (void)size;
    return A_STATUS_OK;
}
aStatus_t aLogGetStats(aLogStats_t *stats)
{
    if (stats == NULL) return A_STATUS_INVALID_PARAM;
    stats->level = ALOG_LEVEL_INFO;
    stats->output_records = 0U;
    stats->dropped_records = 0U;
    stats->filtered_records = 0U;
    return A_STATUS_OK;
}
