/* 使用真实任务入口，在服务初始化的间隙模拟 Shell 获得执行机会。 */
#include "system_init.h"
#include "aOS.h"
#include "system_device.h"
#include "flash_device.h"
#include "memory_config.h"
#include "database_service.h"
#include "aLog.h"
#include "protocol.h"
#include "aShell.h"
#include "aDrv_basic.h"
#include "aDev_led_instance.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned stage, failure, shell_tasks, cleanup_calls;
static unsigned blocked_probes, processed_probes;
static unsigned log_messages;
static aBool_t console_owned, expect_commands, cleanup_failure;
static aOSTaskFunction_t shell_entry;
static void *shell_argument;
static jmp_buf task_slice;
static aDevLedHandle_t led;

enum { TASK_WAITING = 1, TASK_PROCESSING };

/* 在 Delay/Process 处结束本次任务片段，不访问私有就绪变量。 */
static void probe_shell(aBool_t ready)
{
    if (shell_entry == NULL) return;
    expect_commands = ready;
    switch (setjmp(task_slice)) {
    case 0:
        shell_entry(shell_argument);
        assert(0);
        break;
    case TASK_WAITING:
        assert(!ready);
        break;
    case TASK_PROCESSING:
        assert(ready);
        break;
    default:
        assert(0);
    }
}

static aStatus_t step(void)
{
    probe_shell(A_FALSE);
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
    aStatus_t status;

    assert(out == NULL && config->function != NULL);
    if (strcmp(config->name, "status") == 0) {
        assert(stage == 1U);
        return step();
    }
    assert(strcmp(config->name, "shell") == 0 && stage == 3U);
    ++shell_tasks;
    status = step();
    if (status == A_STATUS_OK) {
        shell_entry = config->function;
        shell_argument = config->argument;
        /* 任务可能在 Create 返回前被调度。 */
        probe_shell(A_FALSE);
    }
    return status;
}

aStatus_t appSystemConsoleInit(void)
{
    aStatus_t status;

    assert(stage == 2U);
    status = step();
    console_owned = status == A_STATUS_OK;
    return status;
}

aStatus_t appSystemConsoleDeInit(void)
{
    assert(console_owned && (failure == 4U || failure == 5U));
    ++cleanup_calls;
    probe_shell(A_FALSE);
    if (cleanup_failure) return A_STATUS_BUSY;
    console_owned = A_FALSE;
    return A_STATUS_OK;
}

static aStatus_t log_output(void *context, const char *data, size_t size)
{
    (void)context;
    (void)data;
    (void)size;
    assert(0); /* 初始化编排测试不访问真实输出端。 */
    return A_STATUS_ERROR;
}

void appSystemLogConfigInit(aLogConfig_t *config)
{
    assert(stage == 4U && console_owned);
    aLogConfigStructInit(config);
    config->output = log_output;
}

aStatus_t aLogInit(const aLogConfig_t *config)
{
    assert(stage == 4U && config->output == log_output);
    return step();
}

aStatus_t aLogWrite(aLogLevel_t level, const char *tag,
                   const char *format, ...)
{
    assert(stage == 5U && failure != 5U && console_owned);
    assert(level == ALOG_LEVEL_INFO && strcmp(tag, "system") == 0);
    assert(strcmp(format, "EasyLogger ready") == 0);
    ++log_messages;
    /* 启动日志丢弃不应使已完成的服务初始化失败。 */
    return A_STATUS_BUSY;
}
aStatus_t appSystemFlashInit(void) { assert(stage == 5U); return step(); }
aStatus_t appSystemMemoryInit(void) { assert(stage == 6U); return step(); }
aStatus_t appDatabaseInit(void) { assert(stage == 7U); return step(); }
aStatus_t protocolInit(void) { assert(stage == 8U); return step(); }

aStatus_t aShellProcess(void)
{
    assert(expect_commands && console_owned && stage == 9U);
    ++processed_probes;
    longjmp(task_slice, TASK_PROCESSING);
}

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
void aOSDelayMs(uint32_t ms)
{
    assert(ms == 1U && !expect_commands);
    ++blocked_probes;
    longjmp(task_slice, TASK_WAITING);
}
uint32_t aDrvGetCoreClockHz(void) { return 180000000U; }
aStatus_t aShellPrintf(const char *format, ...)
{
    (void)format;
    assert(shell_tasks == 0U && console_owned);
    return A_STATUS_OK;
}

int main(int argc, char **argv)
{
    aStatus_t status;
    aStatus_t expected;

    assert(argc == 2 || argc == 3);
    failure = (unsigned)strtoul(argv[1], NULL, 10);
    cleanup_failure = argc == 3;
    assert(failure <= 9U);
    assert(!cleanup_failure || failure == 4U || failure == 5U);
    status = aSystemInit();
    expected = failure == 0U ? A_STATUS_OK : A_STATUS_ERROR;
    if (cleanup_failure) expected = A_STATUS_BUSY;
    assert(status == expected);
    assert(stage == (failure == 0U ? 9U : failure));
    assert(shell_tasks == (failure == 0U || failure >= 4U ? 1U : 0U));
    assert(cleanup_calls == (failure == 4U || failure == 5U ? 1U : 0U));
    assert(log_messages == (failure == 0U || failure > 5U ? 1U : 0U));
    if (cleanup_calls) assert(console_owned == cleanup_failure);
    probe_shell(status == A_STATUS_OK);
    assert(processed_probes == (failure == 0U ? 1U : 0U));
    if (shell_entry != NULL) assert(blocked_probes != 0U);
    printf("Startup failure=%u cleanup_failure=%u: gate/cleanup passed\n",
           failure, (unsigned)cleanup_failure);
    return 0;
}
