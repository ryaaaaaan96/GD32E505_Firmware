#include "app_log.h"
#include "aLog.h"
#include "aShell.h"

/* 后续可替换成 Flash 排队，或在这里分发到多个输出后端。
 * 当前直接复用 Shell 字节队列，不做第二次 printf 格式化。 */
static aStatus_t shell_output(void *context, const char *data, size_t size)
{
    (void)context;
    return aShellWrite(data, size);
}

aStatus_t appLogInit(void)
{
    aLogConfig_t config;
    aStatus_t status;

    aLogConfigStructInit(&config);
    config.output = shell_output;
    status = aLogInit(&config);
    if (status != A_STATUS_OK) return status;
    /* 初始化成功不以欢迎日志是否成功入队作为判据。 */
    (void)ALOG_INFO("system", "EasyLogger ready");
    return A_STATUS_OK;
}
