#include "log_config.h"
#include "aShell.h"

/* 复用 Shell 字节队列；后续可在此适配 Flash 或分发到多个输出端。 */
static aStatus_t shell_output(void *context, const char *data, size_t size)
{
    (void)context;
    return aShellWrite(data, size);
}

void appSystemLogConfigInit(aLogConfig_t *config)
{
    if (config == NULL) return;
    aLogConfigStructInit(config);
    config->output = shell_output;
}
