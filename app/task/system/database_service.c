#include "database_service.h"
#include "database_config.h"
#if !ADATABASE_DYNAMIC_ENABLE
#include "aDataBase_instance.h"
#endif
#if ASHELL_ENABLE
#include "aShell.h"
#endif

static aDataBaseKvHandle_t *kv_handle;
static aDataBaseTsHandle_t *ts_handle;
#if !ADATABASE_DYNAMIC_ENABLE
static aDataBaseKvHandle_t kv_instance;
static aDataBaseTsHandle_t ts_instance;
#endif

aStatus_t appDatabaseOpen(aBool_t format_if_needed)
{
    aDataBaseKvConfig_t kv_config;
    aDataBaseTsConfig_t ts_config;
    aStatus_t status;

    if (kv_handle != NULL || ts_handle != NULL) return A_STATUS_BUSY;
    appSystemDatabaseKvConfigInit(&kv_config);
    kv_config.format_if_needed = format_if_needed;
#if ADATABASE_DYNAMIC_ENABLE
    status = aDataBaseKvCreate(&kv_config, &kv_handle);
#else
    status = aDataBaseKvInitStatic(&kv_config, &kv_instance);
    if (status == A_STATUS_OK) kv_handle = &kv_instance;
#endif
    if (status != A_STATUS_OK) return status;

    appSystemDatabaseTsConfigInit(&ts_config);
    ts_config.format_if_needed = format_if_needed;
#if ADATABASE_DYNAMIC_ENABLE
    status = aDataBaseTsCreate(&ts_config, &ts_handle);
#else
    status = aDataBaseTsInitStatic(&ts_config, &ts_instance);
    if (status == A_STATUS_OK) ts_handle = &ts_instance;
#endif
    if (status != A_STATUS_OK) {
        (void)appDatabaseClose();
        return status;
    }
    return A_STATUS_OK;
}

aStatus_t appDatabaseClose(void)
{
    aStatus_t status;

    if (ts_handle != NULL) {
#if ADATABASE_DYNAMIC_ENABLE
        status = aDataBaseTsDestroy(ts_handle);
#else
        status = aDataBaseTsDeInitStatic(ts_handle);
#endif
        if (status != A_STATUS_OK) return status;
        ts_handle = NULL;
    }
    if (kv_handle != NULL) {
#if ADATABASE_DYNAMIC_ENABLE
        status = aDataBaseKvDestroy(kv_handle);
#else
        status = aDataBaseKvDeInitStatic(kv_handle);
#endif
        if (status != A_STATUS_OK) return status;
        kv_handle = NULL;
    }
    return A_STATUS_OK;
}

aStatus_t appDatabaseInit(void)
{
    aStatus_t status;

    status = aDataBaseInit();
    if (status != A_STATUS_OK) return status;
    /* 启动不允许因空白或损坏的扇区头而自动格式化。 */
    status = appDatabaseOpen(A_FALSE);
#if ASHELL_ENABLE
    if (status == A_STATUS_OK)
        ASHELL_PRINT("Database: KV and TSDB ready\r\n");
    else if (status == A_STATUS_NOT_READY)
        ASHELL_PRINT("Database: not formatted; run 'db init'\r\n");
    else
        ASHELL_PRINT("Database init failed: %d\r\n", (int)status);
#endif
    return status == A_STATUS_NOT_READY ? A_STATUS_OK : status;
}

aStatus_t appDatabaseKvSet(const aDataBaseKvSetRequest_t *request)
{
    if (kv_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseKvSet(kv_handle, request);
}

aStatus_t appDatabaseKvGet(const aDataBaseKvGetRequest_t *request)
{
    if (kv_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseKvGet(kv_handle, request);
}

aStatus_t appDatabaseKvDelete(const aDataBaseKvDeleteRequest_t *request)
{
    if (kv_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseKvDelete(kv_handle, request);
}

aStatus_t appDatabaseTsAppend(const aDataBaseTsAppendRequest_t *request)
{
    if (ts_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseTsAppend(ts_handle, request);
}

aStatus_t appDatabaseTsIterate(const aDataBaseTsIterateRequest_t *request)
{
    if (ts_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseTsIterate(ts_handle, request);
}

aStatus_t appDatabaseTsGetInfo(aDataBaseTsInfo_t *info)
{
    if (ts_handle == NULL) return A_STATUS_NOT_READY;
    return aDataBaseTsGetInfo(ts_handle, info);
}
