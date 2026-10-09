#include "log_service.h"
#include "log_config.h"
#include "aLog.h"

aStatus_t appLogInit(void)
{
    aLogConfig_t config;
    aStatus_t status;

    appSystemLogConfigInit(&config);
    status = aLogInit(&config);
    if (status != A_STATUS_OK) return status;
    /* 初始化成功不以欢迎日志是否成功入队作为判据。 */
    (void)ALOG_INFO("system", "EasyLogger ready");
    return A_STATUS_OK;
}
