#include "aLog.h"
#include "os_mock.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char data[131072];
    size_t used;
    unsigned calls;
    aStatus_t error;
    unsigned fail_at;
} sink_t;

static sink_t sink;

/* 按记录复制保存，模拟 Shell/Flash 后端对借用内存的消费契约。 */
static aStatus_t capture(void *context, const char *data, size_t size)
{
    sink_t *target = context;

    assert(target == &sink && size <= ALOG_LINE_BUFFER_SIZE);
    assert(size >= 2U && data[size - 2U] == '\r' && data[size - 1U] == '\n');
    assert(memchr(data, '\0', size) == NULL);
    ++target->calls;
    if (target->error != A_STATUS_OK &&
        (target->fail_at == 0U || target->calls == target->fail_at)) {
        return target->error;
    }
    assert(target->used + size < sizeof(target->data));
    memcpy(target->data + target->used, data, size);
    target->used += size;
    target->data[target->used] = '\0';
    return A_STATUS_OK;
}

static void clear_sink(void) { memset(&sink, 0, sizeof(sink)); }

static void *thread_output(void *argument)
{
    unsigned id = (unsigned)(uintptr_t)argument;

    for (unsigned i = 0U; i < 150U; ++i) {
        assert(ALOG_ERROR("thread", "worker=%u record=%u", id, i)
               == A_STATUS_OK);
    }
    return NULL;
}

int main(void)
{
    aLogConfig_t config;
    aLogStats_t stats;
    unsigned calls;
    char text[ALOG_LINE_BUFFER_SIZE];
    uint8_t bytes[48];
    pthread_t threads[4];

    aLogConfigStructInit(NULL);
    aLogConfigStructInit(&config);
    assert(config.output == NULL && config.context == NULL);
    assert(config.level == ALOG_LEVEL_INFO && config.color == A_FALSE);
    assert(config.lock_timeout.type == A_TIMEOUT_TYPE_RELATIVE &&
           config.lock_timeout.milliseconds == 0U);
    assert(aLogInit(NULL) == A_STATUS_INVALID_PARAM);
    assert(aLogInit(&config) == A_STATUS_INVALID_PARAM);
    assert(ALOG_ERROR("test", "before init") == A_STATUS_NOT_READY);
    assert(aLogDeInit() == A_STATUS_NOT_READY);
    config.output = capture;
    config.context = &sink;
    testFailMutexCreate(A_TRUE);
    assert(aLogInit(&config) == A_STATUS_NO_MEMORY);
    testFailMutexCreate(A_FALSE);
    assert(testMutexCount() == 0U);
    config.level = (aLogLevel_t)99;
    assert(aLogInit(&config) == A_STATUS_INVALID_PARAM);
    config.level = ALOG_LEVEL_VERBOSE;
    config.color = A_TRUE;
    testSetUptime(UINT32_MAX);
    assert(aLogInit(&config) == A_STATUS_OK);
    assert(aLogInit(&config) == A_STATUS_BUSY);
    assert(testMutexCount() == 1U && sink.calls == 0U);
    config.output = NULL; /* 已复制配置，不借用栈上的配置结构体。 */

    assert(ALOG_ERROR("flash", "value=%u 100%%", 123U) == A_STATUS_OK);
    assert(strstr(sink.data, "E/flash") != NULL);
    assert(strstr(sink.data, "4294967295 ms") != NULL);
    assert(strstr(sink.data, "value=123 100%") != NULL);
    assert(strstr(sink.data, "\033[") != NULL);
    assert(aLogWrite((aLogLevel_t)0, "test", "bad")
           == A_STATUS_INVALID_PARAM);
    assert(aLogWrite(ALOG_LEVEL_ERROR, "", "bad") == A_STATUS_INVALID_PARAM);
    assert(aLogWrite(ALOG_LEVEL_ERROR, "bad tag", "bad")
           == A_STATUS_INVALID_PARAM);
    assert(aLogWrite(ALOG_LEVEL_ERROR, "test", NULL)
           == A_STATUS_INVALID_PARAM);
    calls = sink.calls;
    assert(ALOG_ERROR("test", "a%cb", 0) == A_STATUS_INVALID_PARAM);
    memset(text, 'x', sizeof(text));
    text[ALOG_LINE_BUFFER_SIZE - 81U] = '\0';
    assert(ALOG_ERROR("123456789012345678901234567890", "%s", text)
           == A_STATUS_OK);
    text[ALOG_LINE_BUFFER_SIZE - 81U] = 'x';
    text[ALOG_LINE_BUFFER_SIZE - 80U] = '\0';
    assert(ALOG_ERROR("test", "%s", text) == A_STATUS_INVALID_PARAM);
    assert(sink.calls == calls + 1U); /* 超长/NUL 日志未部分交付。 */

    calls = sink.calls;
    assert(aLogSetLevel(ALOG_LEVEL_WARN) == A_STATUS_OK);
    assert(aLogWrite(ALOG_LEVEL_INFO, "test", "filtered") == A_STATUS_OK);
    assert(sink.calls == calls);
    testFailNextLock(A_STATUS_BUSY);
    assert(ALOG_ERROR("test", "busy") == A_STATUS_BUSY);
    testFailNextLock(A_STATUS_TIMEOUT);
    assert(ALOG_ERROR("test", "timeout") == A_STATUS_TIMEOUT);
    sink.error = A_STATUS_ERROR;
    assert(ALOG_ERROR("test", "backend failure") == A_STATUS_ERROR);
    assert(sink.calls == calls + 1U); /* 错误直接返回，没有自动重试。 */
    sink.error = A_STATUS_OK;
    assert(ALOG_ERROR("test", "next record") == A_STATUS_OK);
    assert(aLogGetStats(&stats) == A_STATUS_OK);
    assert(stats.level == ALOG_LEVEL_WARN && stats.output_records == 3U);
    assert(stats.dropped_records == 5U && stats.filtered_records == 1U);
    assert(aLogGetStats(NULL) == A_STATUS_INVALID_PARAM);
    assert(aLogSetLevel((aLogLevel_t)6) == A_STATUS_INVALID_PARAM);

    assert(aLogSetLevel(ALOG_LEVEL_VERBOSE) == A_STATUS_OK);
    for (size_t i = 0U; i < sizeof(bytes); ++i) bytes[i] = (uint8_t)i;
    assert(aLogHexDump("hex", NULL, 1U) == A_STATUS_INVALID_PARAM);
    assert(aLogHexDump("hex", bytes, 65521U) == A_STATUS_INVALID_PARAM);
    assert(aLogHexDump("hex", NULL, 0U) == A_STATUS_OK);
#if ALOG_OUTPUT_LEVEL >= 4
    clear_sink();
    assert(aLogHexDump("hex", bytes, 17U) == A_STATUS_OK);
    assert(sink.calls == 2U && strstr(sink.data, "00 01 02 03") != NULL);
    clear_sink();
    sink.error = A_STATUS_TIMEOUT;
    sink.fail_at = 2U;
    assert(aLogHexDump("hex", bytes, sizeof(bytes)) == A_STATUS_TIMEOUT);
    assert(sink.calls == 2U); /* 第三行不再调用后端。 */
    assert(strstr(sink.data, "0000-000F") != NULL);
    assert(strstr(sink.data, "0020-002F") == NULL);
#else
    {
        unsigned evaluated = 0U;
        assert(ALOG_DEBUG("test", "%u", ++evaluated) == A_STATUS_OK);
        assert(ALOG_HEXDUMP("test", bytes, ++evaluated) == A_STATUS_OK);
        assert(evaluated == 0U);
    }
#endif

    assert(aLogDeInit() == A_STATUS_OK && testMutexCount() == 0U);
    aLogConfigStructInit(&config);
    config.output = capture;
    config.context = &sink;
    config.level = ALOG_LEVEL_VERBOSE;
    config.lock_timeout = A_TIMEOUT_FOREVER;
    clear_sink();
    assert(aLogInit(&config) == A_STATUS_OK);
    for (unsigned i = 0U; i < 4U; ++i) {
        assert(pthread_create(&threads[i], NULL, thread_output,
                              (void *)(uintptr_t)i) == 0);
    }
    for (unsigned i = 0U; i < 4U; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(aLogGetStats(&stats) == A_STATUS_OK);
    assert(stats.output_records == 600U && stats.dropped_records == 0U);
    assert(strstr(sink.data, "\033[") == NULL);
    for (unsigned i = 0U; i < 4U; ++i) {
        for (unsigned j = 0U; j < 150U; ++j) {
            (void)snprintf(text, sizeof(text), "worker=%u record=%u\r\n",
                           i, j);
            assert(strstr(sink.data, text) != NULL);
        }
    }
    assert(aLogDeInit() == A_STATUS_OK && testMutexCount() == 0U);
    puts("aLog: lifecycle, backend errors, bounds and threads passed");
    return 0;
}
