#ifndef A_LOG_INTERNAL_H
#define A_LOG_INTERNAL_H

#include "aLog.h"

/* 仅供上游输出钩子调用；调用时已持有 aLog 外层锁。 */
void aLogPortOutput(const char *data, size_t size);

#endif
