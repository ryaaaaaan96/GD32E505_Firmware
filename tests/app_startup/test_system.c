/* 使用真实任务入口，在服务初始化的间隙模拟 Shell 获得执行机会。 */
#include "system_init.h"
#include "aOS.h"
#include "system_device.h"
#include "aBus_instance.h"
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
static unsigned log_messages, kv_cleanup_calls;
static aBusHandle_t test_bus;
static aBusSigState_t test_states[1];
#if ADATABASE_DYNAMIC_ENABLE
static char test_kv, test_ts;
#endif
static const aBusTable_t test_tables[1];
static aBool_t console_owned, expect_commands, cleanup_failure;
static aBool_t shell_init_failure;
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

void appSystemShellConfigInit(aShellConfig_t *config)
{
    *config = (aShellConfig_t){0};
}
aStatus_t aShellInit(const aShellConfig_t *config)
{
    assert(config != NULL && console_owned && stage == 3U);
    return shell_init_failure ? A_STATUS_ERROR : A_STATUS_OK;
}
aStatus_t aShellDeInit(void) { return A_STATUS_OK; }

aStatus_t appSystemConsoleDeInit(void)
{
    assert(console_owned &&
           (shell_init_failure || failure == 4U || failure == 6U));
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
    assert(stage == 5U && console_owned);
    aLogConfigStructInit(config);
    config->output = log_output;
}

aStatus_t aLogInit(const aLogConfig_t *config)
{
    assert(stage == 5U && config->output == log_output);
    return step();
}

aStatus_t aLogWrite(aLogLevel_t level, const char *tag,
                   const char *format, ...)
{
    assert(stage == 6U && failure != 6U && console_owned);
    assert(level == ALOG_LEVEL_INFO && strcmp(tag, "system") == 0);
    assert(strcmp(format, "EasyLogger ready") == 0);
    ++log_messages;
    /* 启动日志丢弃不应使已完成的服务初始化失败。 */
    return A_STATUS_BUSY;
}
void appSystemMemoryConfigInit(aMemoryConfig_t *config)
{
    assert(stage == 4U);
    *config = (aMemoryConfig_t){ .device_count = 1U };
}

aStatus_t aMemoryInit(const aMemoryConfig_t *config)
{
    assert(stage == 4U && config->device_count == 1U);
    /* 模拟设备回调在日志初始化前使用 Shell 启动输出。 */
    assert(aShellPrintf("memory init\r\n") == A_STATUS_OK);
    return step();
}
aStatus_t protocolBusConfigInit(dataBusConfig_t *config)
{
    assert(stage == 6U);
    config->instanceID = 1U;
    config->tables = test_tables;
    config->table_count = 1U;
    aBusInstanceStructInit(&test_bus, test_states, 1U);
#if !ABUS_DYNAMIC_ENABLE
    config->instance = &test_bus;
#endif
    return A_STATUS_OK;
}

#if ABUS_DYNAMIC_ENABLE
aStatus_t aBusCreate(uint16_t instanceID, const aBusTable_t *tables,
                     size_t count, aBusHandle_t **out)
{
    aStatus_t status;

    assert(stage == 6U && instanceID == 1U);
    assert(tables == test_tables && count == 1U);
    status = step();
    *out = status == A_STATUS_OK ? &test_bus : NULL;
    return status;
}
#else
aStatus_t aBusInitStatic(uint16_t instanceID, const aBusTable_t *tables,
                         size_t count, aBusHandle_t *instance)
{
    assert(stage == 6U && instanceID == 1U);
    assert(tables == test_tables && count == 1U);
    assert(instance == &test_bus && instance->sigs == test_states);
    return step();
}
#endif

aStatus_t aDataBaseInit(void) { assert(stage == 7U); return step(); }
void appSystemDatabaseKvConfigInit(aDataBaseKvConfig_t *config)
{
    assert(stage == 8U);
    *config = (aDataBaseKvConfig_t){ .name = "parameters" };
}
void appSystemDatabaseTsConfigInit(aDataBaseTsConfig_t *config)
{
    assert(stage == 9U);
    *config = (aDataBaseTsConfig_t){ .name = "records" };
}
#if ADATABASE_DYNAMIC_ENABLE
aStatus_t aDataBaseKvCreate(const aDataBaseKvConfig_t *config,
                            aDataBaseKvHandle_t **out)
{
    aStatus_t status;

    assert(stage == 8U && !config->format_if_needed);
    status = step();
    *out = status == A_STATUS_OK ? (aDataBaseKvHandle_t *)&test_kv : NULL;
    return status;
}
aStatus_t aDataBaseTsCreate(const aDataBaseTsConfig_t *config,
                            aDataBaseTsHandle_t **out)
{
    aStatus_t status;

    assert(stage == 9U && !config->format_if_needed);
    status = step();
    *out = status == A_STATUS_OK ? (aDataBaseTsHandle_t *)&test_ts : NULL;
    return status;
}
aStatus_t aDataBaseKvDestroy(aDataBaseKvHandle_t *handle)
#else
aStatus_t aDataBaseKvInitStatic(const aDataBaseKvConfig_t *config,
                                aDataBaseKvHandle_t *handle)
{
    assert(stage == 8U && !config->format_if_needed && handle != NULL);
    return step();
}
aStatus_t aDataBaseTsInitStatic(const aDataBaseTsConfig_t *config,
                                aDataBaseTsHandle_t *handle)
{
    assert(stage == 9U && !config->format_if_needed && handle != NULL);
    return step();
}
aStatus_t aDataBaseKvDeInitStatic(aDataBaseKvHandle_t *handle)
#endif
{
    assert(stage == 10U && failure == 10U && handle != NULL);
    ++kv_cleanup_calls;
    return cleanup_failure ? A_STATUS_BUSY : A_STATUS_OK;
}
#if ADATABASE_DYNAMIC_ENABLE
aStatus_t aDataBaseTsDestroy(aDataBaseTsHandle_t *handle)
#else
aStatus_t aDataBaseTsDeInitStatic(aDataBaseTsHandle_t *handle)
#endif
{
    (void)handle;
    assert(0);
    return A_STATUS_ERROR;
}

void aDataBaseKvConfigStructInit(aDataBaseKvConfig_t *config)
{
    *config = (aDataBaseKvConfig_t){0};
}
void protocolPersistenceConfigInit(aDataBaseKvConfig_t *config)
{
    static const aDataBaseSigKey_t items[] = {
        {.deviceID = 1U, .sigKey = 42U}
    };
    assert(stage == 8U || stage == 10U);
    config->persist_sigs = items;
    config->persist_count = 1U;
}
aStatus_t aBusResolveKey(aBusHandle_t *bus,
    const aBusSigKeyQuery_t *query, size_t *index)
{
    assert(bus == &test_bus && stage == 10U && query->sigKey == 42U);
    *index = 0U;
    return A_STATUS_OK;
}
aStatus_t aBusGetSigInfo(aBusHandle_t *bus,
    const aBusSigQuery_t *query, aBusSigInfo_t *info)
{
    (void)query;
    assert(bus == &test_bus && stage == 10U);
    info->size = sizeof(uint32_t);
    return A_STATUS_OK;
}
aStatus_t aDataBaseSigGet(aDataBaseKvHandle_t *handle,
    const aDataBaseSigGetRequest_t *request)
{
    assert(handle != NULL && stage == 10U && request->size == 4U);
    return A_STATUS_NOT_FOUND;
}
aStatus_t aDataBaseSigSet(aDataBaseKvHandle_t *handle,
    const aDataBaseSigSetRequest_t *request)
{
    (void)handle; (void)request; assert(0); return A_STATUS_ERROR;
}
aStatus_t aDataBaseSigDelete(aDataBaseKvHandle_t *handle,
    const aDataBaseSigDeleteRequest_t *request)
{
    /* 本场景没有持久化记录，不应触发删除。 */
    (void)handle; (void)request; assert(0); return A_STATUS_ERROR;
}
aStatus_t aBusSetByIndex(aBusHandle_t *bus,
    const aBusSetIndexRequest_t *request)
{
    (void)bus; (void)request; assert(0); return A_STATUS_ERROR;
}
aStatus_t aBusGetByIndex(aBusHandle_t *bus,
    const aBusGetIndexRequest_t *request)
{
    (void)bus; (void)request; assert(0); return A_STATUS_ERROR;
}

aStatus_t protocolInit(aBusHandle_t *bus)
{
    assert(stage == 10U && bus == &test_bus);
    return step();
}

aStatus_t aShellProcess(void)
{
    assert(expect_commands && console_owned && stage == 11U);
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
    assert(console_owned);
    if (stage <= 4U) {
        assert(log_messages == 0U);
        if (stage == 4U) assert(shell_tasks == 1U);
        else assert(stage == 3U && shell_tasks == 0U);
    } else {
        assert(stage >= 7U && stage <= 10U && shell_tasks == 1U);
        assert(log_messages == 1U);
    }
    return A_STATUS_OK;
}

int main(int argc, char **argv)
{
    aStatus_t status;
    aStatus_t expected;

    assert(argc == 2 || argc == 3);
    shell_init_failure = strcmp(argv[1], "shell_init") == 0;
    failure = (unsigned)strtoul(argv[1], NULL, 10);
    cleanup_failure = argc == 3;
    assert(failure <= 11U);
    assert(!cleanup_failure || shell_init_failure ||
           failure == 4U || failure == 6U ||
           failure == 10U);
    status = aSystemInit();
    if (shell_init_failure) {
        assert(status == (cleanup_failure ? A_STATUS_BUSY : A_STATUS_ERROR));
        assert(stage == 3U && shell_tasks == 0U && cleanup_calls == 1U);
        assert(console_owned == cleanup_failure);
        puts("Shell initialization failure cleanup passed");
        return 0;
    }
    expected = failure == 0U ? A_STATUS_OK : A_STATUS_ERROR;
    if (cleanup_failure) expected = A_STATUS_BUSY;
    assert(status == expected);
    assert(stage == (failure == 0U ? 11U : failure));
    assert(shell_tasks == (failure == 0U || failure >= 4U ? 1U : 0U));
    assert(cleanup_calls == (failure == 4U || failure == 6U ? 1U : 0U));
    assert(log_messages == (failure == 0U || failure > 6U ? 1U : 0U));
    assert(kv_cleanup_calls == (failure == 10U ? 1U : 0U));
    if (cleanup_calls) assert(console_owned == cleanup_failure);
    probe_shell(status == A_STATUS_OK);
    assert(processed_probes == (failure == 0U ? 1U : 0U));
    if (shell_entry != NULL) assert(blocked_probes != 0U);
    printf("Startup failure=%u cleanup_failure=%u: gate/cleanup passed\n",
           failure, (unsigned)cleanup_failure);
    return 0;
}
