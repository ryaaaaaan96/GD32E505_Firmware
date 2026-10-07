#ifndef A_LOG_ELOG_CFG_H
#define A_LOG_ELOG_CFG_H

/* 只编译核心；官方目录保持原样，此头通过私有 include 路径优先加载。 */
#include <assert.h>
#define ELOG_OUTPUT_ENABLE
#define ELOG_OUTPUT_LVL ALOG_OUTPUT_LEVEL
#define ELOG_LINE_BUF_SIZE ALOG_LINE_BUFFER_SIZE
#define ELOG_LINE_NUM_MAX_LEN 5
#define ELOG_FILTER_TAG_MAX_LEN 30
#define ELOG_FILTER_KW_MAX_LEN 16
#define ELOG_FILTER_TAG_LVL_MAX_NUM 4
#define ELOG_NEWLINE_SIGN "\r\n"
#define ELOG_COLOR_ENABLE

/* 不启用上游异步、缓冲和 pthread；任务与慢速输出由应用编排。
 * 公共参数由 aLog 校验；内部断言使用 C 标准 assert。
 * 仅启用等级、标签和时间，限制头部长度，不输出文件或函数路径。 */

#endif
