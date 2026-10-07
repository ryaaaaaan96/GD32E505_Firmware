#ifndef APP_DATABASE_H
#define APP_DATABASE_H

#include "aDataBase.h"

/* 应用层单例，KV 和 TS 句柄均私有；存储绑定由设备初始化层完成。 */
aStatus_t appDatabaseInit(void);
/** 显式允许首次格式化或修复，仅作用于配置中指定的数据库分区。 */
aStatus_t appDatabaseOpen(aBool_t format_if_needed);
aStatus_t appDatabaseClose(void);
aStatus_t appDatabaseKvSet(const aDataBaseKvSetRequest_t *request);
aStatus_t appDatabaseKvGet(const aDataBaseKvGetRequest_t *request);
aStatus_t appDatabaseKvDelete(const aDataBaseKvDeleteRequest_t *request);
aStatus_t appDatabaseTsAppend(const aDataBaseTsAppendRequest_t *request);
aStatus_t appDatabaseTsIterate(const aDataBaseTsIterateRequest_t *request);
aStatus_t appDatabaseTsGetInfo(aDataBaseTsInfo_t *info);

#endif
