#include "log_config.h"
#include "system_device.h"
#include "aOS.h"

/* 在日志调用任务中提交串口；发送互斥由 console_write 负责。
 * 日志锁已保护格式化数据，此处不再创建锁，也不经过 Shell 队列。 */
static aStatus_t console_output(void *context, const char *data, size_t size)
{
    aSSize_t count;

    (void)context;
    count = app_system_console_stream.write(data, size, A_TIMEOUT_MS(20U));
    if (count >= 0) {
        /* 部分提交可能已输出前缀，不读取旧 errno，也不重发整条日志。 */
        return (size_t)count == size ? A_STATUS_OK : A_STATUS_ERROR;
    }
    switch (aOSGetErrno()) {
    case A_EAGAIN: return A_STATUS_BUSY;
    case A_ETIMEDOUT: return A_STATUS_TIMEOUT;
    case A_ENODEV: return A_STATUS_NOT_READY;
    case A_EINVAL: return A_STATUS_INVALID_PARAM;
    default: return A_STATUS_ERROR;
    }
}

void appSystemLogConfigInit(aLogConfig_t *config)
{
    if (config == NULL) return;
    aLogConfigStructInit(config);
    config->output = console_output;
}
