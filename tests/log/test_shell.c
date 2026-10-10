#include "aLog.h"
#include "aShell.h"
#include "aShell_config.h"
#include "log_service.h"
#include "system_device.h"
#include "aDev_usart.h"
#include "os_mock.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static char output[262144];
static size_t output_size;
static unsigned write_calls;
static const char *input;
static char device_storage;
static size_t write_limit = SIZE_MAX;
static aStatus_t write_error = A_STATUS_OK;
static atomic_uint writers;

aStatus_t aDevLedInitStatic(const aDevLedConfig_t *config,
                           aDevLedHandle_t *handle)
{
    (void)config;
    (void)handle;
    return A_STATUS_OK;
}

aStatus_t aDevUsartCreate(const aDevUsartConfig_t *config,
                         aDevUsartHandle_t **handle)
{
    (void)config;
    *handle = (aDevUsartHandle_t *)(void *)&device_storage;
    return A_STATUS_OK;
}

aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle)
{
    assert(handle == (aDevUsartHandle_t *)(void *)&device_storage);
    return A_STATUS_OK;
}

aStatus_t aDevUsartGetRxError(const aDevUsartHandle_t *handle)
{
    (void)handle;
    return A_STATUS_OK;
}

void aDevUsartClearRxError(aDevUsartHandle_t *handle) { (void)handle; }

aSSize_t aDevUsartRead(aDevUsartHandle_t *handle, void *data, size_t size,
                      aTimeout_t timeout)
{
    size_t count = input == NULL ? 0U : strlen(input);

    (void)timeout;
    assert(handle == (aDevUsartHandle_t *)(void *)&device_storage);
    if (count > size) count = size;
    if (count != 0U) {
        memcpy(data, input, count);
        input += count;
    }
    return (aSSize_t)count;
}

aSSize_t aDevUsartWrite(aDevUsartHandle_t *handle, const void *data,
                       size_t size, aTimeout_t timeout)
{
    (void)timeout;
    assert(handle == (aDevUsartHandle_t *)(void *)&device_storage);
    /* 主机强制让出执行权，检查真实 console_write 是否允许发送者重叠。 */
    assert(atomic_fetch_add(&writers, 1U) == 0U);
    sched_yield();
    ++write_calls;
    if (write_error != A_STATUS_OK) {
        assert(atomic_fetch_sub(&writers, 1U) == 1U);
        return aOSFailWithStatus(write_error);
    }
    if (size > write_limit) size = write_limit;
    assert(output_size + size < sizeof(output));
    memcpy(output + output_size, data, size);
    output_size += size;
    output[output_size] = '\0';
    assert(atomic_fetch_sub(&writers, 1U) == 1U);
    return (aSSize_t)size;
}

static void reset_output(void)
{
    output_size = 0U;
    output[0] = '\0';
    write_calls = 0U;
}

static void feed(const char *command)
{
    input = command;
    while (*input != '\0') assert(aShellProcess() == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_BUSY);
    input = NULL;
}

static void *log_worker(void *argument)
{
    (void)argument;
    for (unsigned i = 0U; i < 1000U; ++i) {
        aStatus_t status = ALOG_ERROR("parallel", "record %u", i);
        assert(status == A_STATUS_OK || status == A_STATUS_TIMEOUT ||
               status == A_STATUS_BUSY);
    }
    return NULL;
}

int main(void)
{
    aLogStats_t log_stats;
    aShellOutputStats_t shell_stats;
    char full[ASHELL_OUTPUT_BUFFER_SIZE];
    unsigned dropped;
    pthread_t worker;

    assert(aShellWrite("test", 4U) == A_STATUS_NOT_READY);
    assert(appSystemConsoleInit() == A_STATUS_OK);
    assert(aShellWrite(NULL, 1U) == A_STATUS_INVALID_PARAM);
    assert(aShellWrite(NULL, 0U) == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_BUSY);
    reset_output();
    assert(appLogInit() == A_STATUS_OK);
    assert(write_calls == 1U); /* 日志直接提交，不等待 Shell 消费。 */
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(strstr(output, "I/system") != NULL);
    assert(strstr(output, "EasyLogger ready") != NULL);
    reset_output();
    feed("log test\r");
    assert(strstr(output, "error message") != NULL);
    assert(strstr(output, "warn message") != NULL);
    assert(strstr(output, "info message") != NULL);
    assert(strstr(output, "debug message") == NULL);
    assert(strstr(output, "verbose message") == NULL);

    reset_output();
    feed("log level verbose\rlog test\rlog info\r");
    assert(strstr(output, "debug message") != NULL);
    assert(strstr(output, "verbose message") != NULL);
    assert(strstr(output, "D/HEX test") != NULL);
    assert(strstr(output, "41 49 44 43") != NULL);
    assert(strstr(output, "level=verbose") != NULL);
    reset_output();
    feed("log write info flash 100% saved\r");
    assert(strstr(output, "I/flash") != NULL);
    assert(strstr(output, "100% saved") != NULL);
    reset_output();
    feed("log level invalid\r");
    assert(strstr(output, "log failed:") != NULL);

    reset_output();
    memset(full, 'x', sizeof(full));
    assert(aShellWrite(full, sizeof(full)) == A_STATUS_OK);
    assert(aLogGetStats(&log_stats) == A_STATUS_OK);
    dropped = log_stats.dropped_records;
    assert(ALOG_ERROR("test", "queue full") == A_STATUS_OK);
    assert(write_calls == 1U);
    assert(aLogGetStats(&log_stats) == A_STATUS_OK);
    assert(log_stats.dropped_records == dropped);
    assert(aShellGetOutputStats(&shell_stats) == A_STATUS_OK);
    assert(shell_stats.pending_bytes == sizeof(full));
    assert(shell_stats.dropped_messages == 0U);
    assert(strstr(output, "queue full") != NULL);
    reset_output();
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(output_size == sizeof(full)); /* 日志不占用 Shell 队列。 */
    assert(ALOG_ERROR("test", "after drain") == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(strstr(output, "after drain") != NULL);
    assert(strstr(output, "queue full") == NULL);

#if ALOG_LINE_BUFFER_SIZE >= 512
    {
        char text[300];

        reset_output();
        memset(text, 'q', sizeof(text) - 1U);
        text[sizeof(text) - 1U] = '\0';
        assert(ALOG_ERROR("long", "%s", text) == A_STATUS_OK);
        assert(write_calls == 1U);
        assert(aShellProcess() == A_STATUS_BUSY);
        assert(strstr(output, text) != NULL);
    }
#endif

    reset_output();
    assert(aLogGetStats(&log_stats) == A_STATUS_OK);
    dropped = log_stats.dropped_records;
    write_limit = 3U;
    (void)aOSFailWithStatus(A_STATUS_TIMEOUT); /* 部分写入不能读取旧 errno。 */
    assert(ALOG_ERROR("partial", "truncated") == A_STATUS_ERROR);
    assert(output_size == 3U && write_calls == 1U);
    write_limit = SIZE_MAX;
    write_error = A_STATUS_TIMEOUT;
    assert(ALOG_ERROR("test", "timeout") == A_STATUS_TIMEOUT);
    write_error = A_STATUS_BUSY;
    assert(ALOG_ERROR("test", "busy") == A_STATUS_BUSY);
    write_error = A_STATUS_OK;
    assert(aLogGetStats(&log_stats) == A_STATUS_OK);
    assert(log_stats.dropped_records == dropped + 3U);

    reset_output();
    assert(pthread_create(&worker, NULL, log_worker, NULL) == 0);
    for (unsigned i = 0U; i < 1000U; ++i) {
        aStatus_t status = ASHELL_PRINT("shell %u\r\n", i);
        assert(status == A_STATUS_OK || status == A_STATUS_BUSY);
        status = aShellProcess();
        assert(status == A_STATUS_OK || status == A_STATUS_BUSY ||
               status == A_STATUS_TIMEOUT);
    }
    assert(pthread_join(worker, NULL) == 0);
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(atomic_load(&writers) == 0U);

    assert(appSystemConsoleDeInit() == A_STATUS_OK);
    assert(ALOG_ERROR("test", "backend closed") == A_STATUS_NOT_READY);
    assert(aLogDeInit() == A_STATUS_OK);
    assert(testMutexCount() == 0U);
    puts("aLog: shared console, partial writes, commands and "
         "concurrency passed");
    return 0;
}
