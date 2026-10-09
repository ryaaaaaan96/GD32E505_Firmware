#include "aLog.h"
#include "aShell.h"
#include "aShell_config.h"
#include "log_service.h"
#include "os_mock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char output[32768];
static size_t output_size;
static unsigned write_calls;
static const char *input;

static aSSize_t stream_read(void *data, size_t size, aTimeout_t timeout)
{
    size_t count = input == NULL ? 0U : strlen(input);

    (void)timeout;
    if (count > size) count = size;
    if (count != 0U) {
        memcpy(data, input, count);
        input += count;
    }
    return (aSSize_t)count;
}

static aSSize_t stream_write(const void *data, size_t size,
                            aTimeout_t timeout)
{
    (void)timeout;
    assert(output_size + size < sizeof(output));
    memcpy(output + output_size, data, size);
    output_size += size;
    output[output_size] = '\0';
    ++write_calls;
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

int main(void)
{
    aShellConfig_t shell_config;
    aLogStats_t log_stats;
    aShellOutputStats_t shell_stats;
    char full[ASHELL_OUTPUT_BUFFER_SIZE];
    unsigned dropped;

    assert(aShellWrite("test", 4U) == A_STATUS_NOT_READY);
    aShellConfigStructInit(&shell_config);
    shell_config.stream.read = stream_read;
    shell_config.stream.write = stream_write;
    assert(aShellInit(&shell_config) == A_STATUS_OK);
    assert(aShellWrite(NULL, 1U) == A_STATUS_INVALID_PARAM);
    assert(aShellWrite(NULL, 0U) == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_BUSY);
    reset_output();
    assert(appLogInit() == A_STATUS_OK);
    assert(write_calls == 0U); /* 初始化日志仅入队，没有实际串口发送。 */
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
    assert(ALOG_ERROR("test", "queue full") == A_STATUS_BUSY);
    assert(write_calls == 0U);
    assert(aLogGetStats(&log_stats) == A_STATUS_OK);
    assert(log_stats.dropped_records == dropped + 1U);
    assert(aShellGetOutputStats(&shell_stats) == A_STATUS_OK);
    assert(shell_stats.pending_bytes == sizeof(full));
    assert(shell_stats.dropped_messages >= 1U);
    assert(aShellProcess() == A_STATUS_BUSY);
    assert(output_size == sizeof(full)); /* 失败日志没有部分进入队列。 */
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
        assert(write_calls == 0U);
        assert(aShellProcess() == A_STATUS_BUSY);
        assert(strstr(output, text) != NULL);
    }
#endif

    assert(aShellDeInit() == A_STATUS_OK);
    assert(ALOG_ERROR("test", "backend closed") == A_STATUS_NOT_READY);
    assert(aLogDeInit() == A_STATUS_OK);
    assert(testMutexCount() == 0U);
    puts("aLog: real Shell queue, parser and commands passed");
    return 0;
}
