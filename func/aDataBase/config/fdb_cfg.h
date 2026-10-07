#ifndef PROJECT_FDB_CFG_H
#define PROJECT_FDB_CFG_H

#define FDB_USING_KVDB
#define FDB_USING_TSDB
#define FDB_USING_AMEMORY_MODE
#define FDB_USING_KV_INDEX
#define FDB_USING_TIMESTAMP_64BIT
#define FDB_WRITE_GRAN 1

/* 禁止官方日志直接依赖控制台；保留参数求值避免上游未使用变量告警。 */
static inline void aDataBaseUpstreamLog(const char *format, ...)
{
    (void)format;
}
#define FDB_PRINT(...) aDataBaseUpstreamLog(__VA_ARGS__)

#endif
