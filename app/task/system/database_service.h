#ifndef APP_DATABASE_SERVICE_H
#define APP_DATABASE_SERVICE_H

#include "aDataBase.h"

/* 系统服务私有持有 KV 和 TS 单例，存储配置及绑定由 devices/system 提供。 */
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
