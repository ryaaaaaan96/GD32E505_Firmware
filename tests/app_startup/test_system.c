/* 使用真实 system_init.c，验证依赖顺序与失败时不开放 Shell 命令。 */
#include "system_init.h"
#include "aOS.h"
#include "system_device.h"
#include "flash_device.h"
#include "memory_config.h"
#include "database_service.h"
#include "log_service.h"
#include "aLog.h"
#include "protocol.h"
#include "app_config.h"
#include "aShell.h"
#include "aDrv_basic.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static unsigned stage, failure, shell_tasks;
static aDevLedHandle_t led;

static aStatus_t step(void)
{
    ++stage;
    return stage == failure ? A_STATUS_ERROR : A_STATUS_OK;
}

aStatus_t appSystemStatusLedInit(aDevLedHandle_t **out)
{
    assert(stage == 0U);
    *out = &led;
    return step();
}
aStatus_t aOSCreateTask(const aOSTaskConfig_t *config,
                       aOSTaskHandle_t *out)
{
    assert(out == NULL && config->function != NULL);
    if (strcmp(config->name, "status") == 0) assert(stage == 1U);
    else {
        assert(strcmp(config->name, "shell") == 0 && stage == 8U);
        ++shell_tasks;
    }
    return step();
}
aStatus_t appSystemConsoleInit(void)
{
    assert(stage == 2U);
    return step();
}
aStatus_t appLogInit(void) { assert(stage == 3U); return step(); }
aStatus_t appSystemFlashInit(void)
{
    assert(stage == 4U);
    return step();
}
aStatus_t appSystemMemoryInit(void)
{
    assert(stage == 5U);
    return step();
}
aStatus_t appDatabaseInit(void) { assert(stage == 6U); return step(); }
aStatus_t protocolInit(void) { assert(stage == 7U); return step(); }
aStatus_t aLogDeInit(void) { return A_STATUS_OK; }
aStatus_t aShellDeInit(void) { return A_STATUS_OK; }
aStatus_t aShellProcess(void) { assert(0); return A_STATUS_ERROR; }
aStatus_t aDevLedToggle(aDevLedHandle_t *handle)
{
    (void)handle;
    assert(0);
    return A_STATUS_ERROR;
}
aStatus_t aDevLedOff(aDevLedHandle_t *handle)
{
    (void)handle;
    return A_STATUS_OK;
}
void aOSDelayMs(uint32_t ms) { (void)ms; assert(0); }
uint32_t aDrvGetCoreClockHz(void) { return 180000000U; }
aStatus_t aShellPrintf(const char *format, ...)
{
    (void)format;
    assert(shell_tasks == 0U);
    return A_STATUS_OK;
}

int main(void)
{
    for (failure = 0U; failure <= 9U; ++failure) {
        stage = 0U;
        shell_tasks = 0U;
        aStatus_t status = aSystemInit();
        assert(status == (failure == 0U ? A_STATUS_OK : A_STATUS_ERROR));
        assert(stage == (failure == 0U ? 9U : failure));
        assert(shell_tasks == (failure == 0U || failure == 9U ? 1U : 0U));
    }
    puts("System service initialization and Shell startup ordering passed");
    return 0;
}
#include "aDev_led_instance.h"
