#include "aLog_internal.h"
#include "aOS.h"
#include <elog.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* 预留头部：30 字节标签、等级、毫秒时间、颜色和 CRLF，最多 63 字节。
 * 留 80 字节余量，先检查正文，避免上游默认的超长截断行为。 */
#define ALOG_HEADER_RESERVE 80U

_Static_assert(ALOG_LINE_BUFFER_SIZE >= 256, "Log line too small");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Log counters need atomics");
_Static_assert(ALOG_LEVEL_ERROR == ELOG_LVL_ERROR &&
               ALOG_LEVEL_VERBOSE == ELOG_LVL_VERBOSE,
               "Log level mapping changed");

/* 此锁保护格式化、上游单例和统计，不代替输出设备的共享访问锁。
 * 输出回调在锁内执行，以保证传入的数据在回调返回前不被其他日志覆盖。 */
static struct {
    aOSMutex_t mutex;
    aLogConfig_t config;
    aBool_t ready;
    aStatus_t output_status;
    unsigned output_records;
    unsigned filtered_records;
    atomic_uint dropped_records;
    char message[ALOG_LINE_BUFFER_SIZE - ALOG_HEADER_RESERVE];
} logger;

static aBool_t level_valid(aLogLevel_t level)
{
    return level >= ALOG_LEVEL_ERROR && level <= ALOG_LEVEL_VERBOSE;
}

static aBool_t tag_valid(const char *tag)
{
    size_t length = 0U;

    if (tag == NULL || *tag == '\0') return A_FALSE;
    while (tag[length] != '\0') {
        unsigned char byte = (unsigned char)tag[length];

        if (length >= ELOG_FILTER_TAG_MAX_LEN || byte <= 32U ||
            byte == 127U) return A_FALSE;
        ++length;
    }
    return A_TRUE;
}

static void record_drop(void)
{
    (void)atomic_fetch_add_explicit(&logger.dropped_records, 1U,
                                    memory_order_relaxed);
}

static aStatus_t unlock_with_status(aStatus_t status)
{
    aStatus_t unlocked = aOSMutexUnlock(logger.mutex);

    return status == A_STATUS_OK ? unlocked : status;
}

/* 上游钩子不返回状态，由外层调用保存第一次输出错误并向业务返回。 */
void aLogPortOutput(const char *data, size_t size)
{
    if (logger.output_status != A_STATUS_OK) {
        record_drop();
        return;
    }
    logger.output_status = logger.config.output(
        logger.config.context, data, size);
    if (logger.output_status == A_STATUS_OK) ++logger.output_records;
    else record_drop();
}

aStatus_t aLogInit(const aLogConfig_t *config)
{
    aStatus_t status;

    if (logger.ready) return A_STATUS_BUSY;
    if (config == NULL || config->output == NULL ||
        !level_valid(config->level) || !aTimeoutIsValid(config->lock_timeout)
        || (config->color != A_FALSE && config->color != A_TRUE)) {
        return A_STATUS_INVALID_PARAM;
    }
    status = aOSMutexCreate(&logger.mutex);
    if (status != A_STATUS_OK) return status;
    logger.config = *config;
    logger.output_records = 0U;
    logger.filtered_records = 0U;
    atomic_init(&logger.dropped_records, 0U);
    logger.output_status = A_STATUS_OK;
    (void)elog_init();
    elog_output_lock_enabled(false);
    /* 上游对象是单例，重新初始化时显式清除旧过滤状态。 */
    elog_set_filter((uint8_t)config->level, "", "");
    elog_set_text_color_enabled(config->color == A_TRUE);
    for (uint8_t level = ELOG_LVL_ASSERT; level <= ELOG_LVL_VERBOSE;
         ++level) {
        elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    }
    /* 不调用 elog_start，避免初始化过程产生隐式日志。 */
    elog_set_output_enabled(true);
    logger.ready = A_TRUE;
    return A_STATUS_OK;
}

aStatus_t aLogDeInit(void)
{
    if (!logger.ready) return A_STATUS_NOT_READY;
    logger.ready = A_FALSE;
    elog_set_output_enabled(false);
    elog_deinit();
    aOSMutexDestroy(&logger.mutex);
    aLogConfigStructInit(&logger.config);
    return A_STATUS_OK;
}

aStatus_t aLogSetLevel(aLogLevel_t level)
{
    aStatus_t status;

    if (!level_valid(level)) return A_STATUS_INVALID_PARAM;
    if (!logger.ready) return A_STATUS_NOT_READY;
    status = aOSMutexLock(logger.mutex, logger.config.lock_timeout);
    if (status != A_STATUS_OK) return status;
    logger.config.level = level;
    elog_set_filter_lvl((uint8_t)level);
    return unlock_with_status(A_STATUS_OK);
}

aStatus_t aLogGetStats(aLogStats_t *stats)
{
    aStatus_t status;

    if (stats == NULL) return A_STATUS_INVALID_PARAM;
    if (!logger.ready) return A_STATUS_NOT_READY;
    status = aOSMutexLock(logger.mutex, A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    stats->level = logger.config.level;
    stats->output_records = logger.output_records;
    stats->filtered_records = logger.filtered_records;
    stats->dropped_records = atomic_load_explicit(&logger.dropped_records,
                                                 memory_order_relaxed);
    return unlock_with_status(A_STATUS_OK);
}

aStatus_t aLogWrite(aLogLevel_t level, const char *tag,
                   const char *format, ...)
{
    aStatus_t status;
    va_list arguments;
    int count;

    if (!level_valid(level) || !tag_valid(tag) || format == NULL)
        return A_STATUS_INVALID_PARAM;
    if (!logger.ready) return A_STATUS_NOT_READY;
    status = aOSMutexLock(logger.mutex, logger.config.lock_timeout);
    if (status != A_STATUS_OK) {
        record_drop();
        return status;
    }
    if (level > logger.config.level || level > ALOG_OUTPUT_LEVEL) {
        ++logger.filtered_records;
        return unlock_with_status(A_STATUS_OK);
    }
    va_start(arguments, format);
    count = vsnprintf(logger.message, sizeof(logger.message),
                      format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= sizeof(logger.message) ||
        memchr(logger.message, '\0', (size_t)count) != NULL) {
        record_drop();
        return unlock_with_status(count < 0 ? A_STATUS_ERROR
                                           : A_STATUS_INVALID_PARAM);
    }
    logger.output_status = A_STATUS_OK;
    /* 官方没有 va_list 入口；正文格式化一次，再交上游补齐日志头。 */
    elog_output((uint8_t)level, tag, NULL, NULL, 0L, "%s", logger.message);
    return unlock_with_status(logger.output_status);
}

aStatus_t aLogHexDump(const char *tag, const void *data, size_t size)
{
    aStatus_t status;

    if (!tag_valid(tag) || (data == NULL && size != 0U) || size > 65520U)
        return A_STATUS_INVALID_PARAM;
    if (!logger.ready) return A_STATUS_NOT_READY;
    if (size == 0U) return A_STATUS_OK;
    status = aOSMutexLock(logger.mutex, logger.config.lock_timeout);
    if (status != A_STATUS_OK) {
        record_drop();
        return status;
    }
    if (ALOG_LEVEL_DEBUG > logger.config.level || ALOG_OUTPUT_LEVEL < 4) {
        ++logger.filtered_records;
        return unlock_with_status(A_STATUS_OK);
    }
    logger.output_status = A_STATUS_OK;
    elog_hexdump(tag, 16U, data, (uint16_t)size);
    return unlock_with_status(logger.output_status);
}
