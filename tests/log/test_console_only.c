/* 真实日志和控制台适配，不链接 Shell，实现独立日志配置回归。 */
#include "system_device.h"
#include "aDev_usart.h"
#include "aOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char device;
static char output[2048];
static size_t used;
aStatus_t aDevLedInitStatic(const aDevLedConfig_t *config,
    aDevLedHandle_t *handle)
{
    (void)config; (void)handle;
    return A_STATUS_OK;
}
aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
    aDevUsartHandle_t **handle)
{
    assert(config != NULL);
    *handle = (void *)&device;
    return A_STATUS_OK;
}
aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
{
    assert(handle == (void *)&device);
    return A_STATUS_OK;
}
aStatus_t aDevUsartDiscardRx(aDevUsartHandle_t *handle)
{
    (void)handle;
    return A_STATUS_OK;
}
aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *data, size_t size,
    aTimeout_t timeout)
{
    (void)handle; (void)data; (void)size; (void)timeout;
    return aOSFailWithStatus(A_STATUS_BUSY);
}
aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data, size_t size,
    aTimeout_t timeout)
{
    assert(handle == (void *)&device);
    assert(aTimeoutIsValid(timeout));
    assert(used + size < sizeof(output));
    memcpy(output + used, data, size);
    used += size;
    output[used] = '\0';
    return (aSSize_t)size;
}
int main(void)
{
    aLogConfig_t config;
    assert(appSystemConsoleInit() == A_STATUS_OK);
    appSystemLogConfigInit(&config);
    assert(aLogInit(&config) == A_STATUS_OK);
    assert(ALOG_INFO("test", "logging without shell") == A_STATUS_OK);
    assert(strstr(output, "logging without shell") != NULL);
    assert(aLogDeInit() == A_STATUS_OK);
    assert(appSystemConsoleDeInit() == A_STATUS_OK);
    puts("Console log without Shell passed");
    return 0;
}
