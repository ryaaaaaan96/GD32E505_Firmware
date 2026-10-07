/**
 * @file aLog.h
 * @brief 基于 EasyLogger 的单例日志；输出后端由应用提供。
 * 除配置初始化外，仅供任务上下文调用，不创建任务或异步队列。
 * Init/DeInit 必须外部串行化；销毁前停止所有日志调用者。
 */
#ifndef A_LOG_H
#define A_LOG_H

#include "aLib.h"
#include "aStatus.h"

typedef enum {
    ALOG_LEVEL_ERROR = 1,
    ALOG_LEVEL_WARN,
    ALOG_LEVEL_INFO,
    ALOG_LEVEL_DEBUG,
    ALOG_LEVEL_VERBOSE
} aLogLevel_t;

/** 完整日志字节，含 CRLF，不保证以 NUL 结尾。
 * 在调用者任务中持日志锁执行，禁止重入 aLog。
 * 返回前必须消费或复制数据，不能保存 data 指针。
 * OK 表示整条已接收；异步后端不代表已经发送或落盘。
 * 后端应自行处理超时；返回错误时 aLog 不重试。
 */
typedef aStatus_t (*aLogOutputFn_t)(void *context, const char *data,
                                  size_t size);

typedef struct {
    aLogOutputFn_t output; /**< 必填；可接 Shell、Flash 或应用分发函数。 */
    void *context; /**< 借用至 DeInit；配置结构体自身复制保存。 */
    aLogLevel_t level; /**< 默认 INFO；输出此等级及更严重的日志。 */
    aBool_t color; /**< 默认关闭；存入 Flash 时建议关闭。 */
    aTimeout_t lock_timeout; /**< 默认 NO_WAIT；仅控制日志锁等待。 */
} aLogConfig_t;

void aLogConfigStructInit(aLogConfig_t *config);
aStatus_t aLogInit(const aLogConfig_t *config);
aStatus_t aLogDeInit(void);
aStatus_t aLogSetLevel(aLogLevel_t level);

typedef struct {
    aLogLevel_t level;
    unsigned output_records; /**< 后端完整接收的记录数；HEX 每行算一条。 */
    unsigned dropped_records; /**< 锁忙、格式超长/失败及后端失败次数。 */
    unsigned filtered_records; /**< 因运行等级过滤的调用数。 */
} aLogStats_t;

/** 等待日志锁取得快照；计数为 unsigned，溢出回绕。 */
aStatus_t aLogGetStats(aLogStats_t *stats);

/** tag 非空，至多 30 字节，不含空白；format 为 printf 格式。
 * 自动增加等级、标签、启动毫秒时间及 CRLF；正文不必添加换行。
 * 正文最多 ALOG_LINE_BUFFER_SIZE - 81 字节，超长或含 NUL 整条拒绝。
 * 过滤返回 OK，不执行格式化；锁等待使用初始化的 lock_timeout。
 * 接收返回 OK；锁、格式或后端错误原样报告，无内部重试。
 */
#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
aStatus_t aLogWrite(aLogLevel_t level, const char *tag,
                   const char *format, ...);

/** 使用 DEBUG 等级，每行 16 字节；size 为 0 不输出。
 * 非零大小需 data 非空，最多 65520 字节，避免上游 uint16_t 游标回绕。
 * 多行不保证全部成功；首次后端错误后停止交付后续行，不回滚已输出行。
 */
aStatus_t aLogHexDump(const char *tag, const void *data, size_t size);

#if ALOG_ENABLE
#define ALOG_ERROR(tag, ...) \
    aLogWrite(ALOG_LEVEL_ERROR, tag, __VA_ARGS__)
#if ALOG_OUTPUT_LEVEL >= 2
#define ALOG_WARN(tag, ...) aLogWrite(ALOG_LEVEL_WARN, tag, __VA_ARGS__)
#endif
#if ALOG_OUTPUT_LEVEL >= 3
#define ALOG_INFO(tag, ...) aLogWrite(ALOG_LEVEL_INFO, tag, __VA_ARGS__)
#endif
#if ALOG_OUTPUT_LEVEL >= 4
#define ALOG_DEBUG(tag, ...) aLogWrite(ALOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define ALOG_HEXDUMP(tag, data, size) aLogHexDump(tag, data, size)
#endif
#if ALOG_OUTPUT_LEVEL >= 5
#define ALOG_VERBOSE(tag, ...) \
    aLogWrite(ALOG_LEVEL_VERBOSE, tag, __VA_ARGS__)
#endif
#endif

/* 编译裁剪的宏不求值参数，仍可作为返回 aStatus_t 的表达式使用。 */
static inline aStatus_t aLogOutputDisabled(void)
{
    return A_STATUS_OK;
}
#ifndef ALOG_ERROR
#define ALOG_ERROR(...) aLogOutputDisabled()
#endif
#ifndef ALOG_WARN
#define ALOG_WARN(...) aLogOutputDisabled()
#endif
#ifndef ALOG_INFO
#define ALOG_INFO(...) aLogOutputDisabled()
#endif
#ifndef ALOG_DEBUG
#define ALOG_DEBUG(...) aLogOutputDisabled()
#endif
#ifndef ALOG_VERBOSE
#define ALOG_VERBOSE(...) aLogOutputDisabled()
#endif
#ifndef ALOG_HEXDUMP
#define ALOG_HEXDUMP(...) aLogOutputDisabled()
#endif

#endif
