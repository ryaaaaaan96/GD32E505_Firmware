#include "aDataBase_internal.h"
#include "fdb_low_lvl.h"
#include <string.h>

/* 给官方扇区头、索引和最长 key 预留空间，不允许跨扇区存储单条值。 */
static const size_t record_overhead = 128U;

static aStatus_t result_get(fdb_err_t result)
{
    switch (result) {
    case FDB_NO_ERR: return A_STATUS_OK;
    case FDB_PART_NOT_FOUND: return A_STATUS_NOT_FOUND;
    case FDB_KV_NAME_ERR: return A_STATUS_INVALID_PARAM;
    case FDB_KV_NAME_EXIST: return A_STATUS_BUSY;
    case FDB_SAVED_FULL: return A_STATUS_NO_MEMORY;
    case FDB_INIT_FAILED: return A_STATUS_NOT_READY;
    default: return A_STATUS_ERROR;
    }
}

static aBool_t key_valid(const char *key)
{
    size_t length;

    if (key == NULL || key[0] == '\0') return A_FALSE;
    for (length = 0U; length < FDB_KV_NAME_MAX; ++length)
        if (key[length] == '\0') return A_TRUE;
    return A_FALSE;
}

static aStatus_t instance_begin(aDataBaseInstance_t *instance,
                                aBool_t ready, aTimeout_t timeout)
{
    aStatus_t status;

    if (!ready) return A_STATUS_NOT_READY;
    status = aDataBaseOperationBegin(timeout);
    if (status != A_STATUS_OK) return status;
    if (instance->fault) return aDataBaseOperationEnd(A_STATUS_NOT_READY);
    return A_STATUS_OK;
}

static aStatus_t instance_end(aDataBaseInstance_t *instance,
                              aStatus_t status)
{
    if (aDataBaseStorageError() != A_STATUS_OK) instance->fault = A_TRUE;
    return aDataBaseOperationEnd(status);
}

void aDataBaseKvConfigStructInit(aDataBaseKvConfig_t *config)
{
    if (config == NULL) return;
    *config = (aDataBaseKvConfig_t){
        .name = NULL, .partition = NULL, .format_if_needed = A_FALSE,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aDataBaseTsConfigStructInit(aDataBaseTsConfig_t *config)
{
    if (config == NULL) return;
    *config = (aDataBaseTsConfig_t){
        .name = NULL, .partition = NULL, .max_record_size = 256U,
        .rollover = A_TRUE, .format_if_needed = A_FALSE,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aDataBaseKvSetRequestStructInit(aDataBaseKvSetRequest_t *request)
{
    if (request == NULL) return;
    *request = (aDataBaseKvSetRequest_t){
        .key = NULL, .data = NULL, .size = 0U,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aDataBaseKvGetRequestStructInit(aDataBaseKvGetRequest_t *request)
{
    if (request == NULL) return;
    *request = (aDataBaseKvGetRequest_t){
        .key = NULL, .data = NULL, .capacity = 0U, .size_out = NULL,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aDataBaseKvDeleteRequestStructInit(aDataBaseKvDeleteRequest_t *request)
{
    if (request == NULL) return;
    *request = (aDataBaseKvDeleteRequest_t){
        .key = NULL, .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aDataBaseTsAppendRequestStructInit(aDataBaseTsAppendRequest_t *request)
{
    if (request == NULL) return;
    *request = (aDataBaseTsAppendRequest_t){
        .timestamp = 0, .data = NULL, .size = 0U,
        .timeout = A_TIMEOUT_MS(5000U)
    };
}

void aDataBaseTsIterateRequestStructInit(aDataBaseTsIterateRequest_t *request)
{
    if (request == NULL) return;
    *request = (aDataBaseTsIterateRequest_t){
        .from = 0, .to = INT64_MAX, .buffer = NULL, .capacity = 0U,
        .callback = NULL, .context = NULL, .timeout = A_TIMEOUT_MS(5000U)
    };
}

static aStatus_t partition_get(const char *name,
                               const aMemoryHandle_t **partition)
{
    *partition = aMemoryFind(name);
    if (*partition == NULL) return A_STATUS_NOT_FOUND;
    if (aDataBasePartitionIsUsed(*partition)) return A_STATUS_BUSY;
    return A_STATUS_OK;
}

static aStatus_t kv_initialize(const aDataBaseKvConfig_t *config,
                               aDataBaseKvHandle_t *handle)
{
    const aMemoryHandle_t *partition;
    aStatus_t status;
    bool not_format;
    fdb_err_t result;

    if (config == NULL || handle == NULL || config->name == NULL ||
        config->name[0] == '\0' || config->partition == NULL)
        return A_STATUS_INVALID_PARAM;
    status = aDataBaseOperationBegin(config->timeout);
    if (status != A_STATUS_OK) return status;
    status = partition_get(config->partition, &partition);
    if (status != A_STATUS_OK) return aDataBaseOperationEnd(status);
    memset(handle, 0, sizeof(*handle));
    handle->instance.partition = partition;
    not_format = !config->format_if_needed;
    fdb_kvdb_control(&handle->db, FDB_KVDB_CTRL_SET_NOT_FORMAT, &not_format);
    result = fdb_kvdb_init(&handle->db, config->name, config->partition,
                           NULL, handle);
    status = result_get(result);
    if (not_format && result == FDB_READ_ERR &&
        aDataBaseStorageError() == A_STATUS_OK) status = A_STATUS_NOT_READY;
    status = aDataBaseOperationEnd(status);
    if (status != A_STATUS_OK) {
        (void)fdb_kvdb_deinit(&handle->db);
        return status;
    }
    aDataBaseInstanceAdd(&handle->instance);
    return A_STATUS_OK;
}

/* 公共追加接口始终带显式时间戳；此函数仅满足官方初始化的回调契约。 */
static fdb_time_t unused_clock(void)
{
    return 0;
}

static aStatus_t ts_initialize(const aDataBaseTsConfig_t *config,
                               aDataBaseTsHandle_t *handle)
{
    const aMemoryHandle_t *partition;
    aMemoryInfo_t info;
    aStatus_t status;
    bool not_format;
    bool rollover;
    fdb_err_t result;

    if (config == NULL || handle == NULL || config->name == NULL ||
        config->name[0] == '\0' || config->partition == NULL ||
        config->max_record_size == 0U) return A_STATUS_INVALID_PARAM;
    status = aDataBaseOperationBegin(config->timeout);
    if (status != A_STATUS_OK) return status;
    status = partition_get(config->partition, &partition);
    if (status != A_STATUS_OK) return aDataBaseOperationEnd(status);
    status = aMemoryGetInfo(partition, &info);
    if (status != A_STATUS_OK ||
        info.geometry.erase_granularity <= record_overhead ||
        config->max_record_size >
        info.geometry.erase_granularity - record_overhead)
        return aDataBaseOperationEnd(A_STATUS_INVALID_PARAM);
    memset(handle, 0, sizeof(*handle));
    handle->instance.partition = partition;
    not_format = !config->format_if_needed;
    fdb_tsdb_control(&handle->db, FDB_TSDB_CTRL_SET_NOT_FORMAT, &not_format);
    result = fdb_tsdb_init(&handle->db, config->name, config->partition,
                           unused_clock, config->max_record_size, handle);
    status = result_get(result);
    if (not_format && result == FDB_READ_ERR &&
        aDataBaseStorageError() == A_STATUS_OK) status = A_STATUS_NOT_READY;
    if (status == A_STATUS_OK) {
        rollover = config->rollover;
        fdb_tsdb_control(&handle->db, FDB_TSDB_CTRL_SET_ROLLOVER, &rollover);
    }
    status = aDataBaseOperationEnd(status);
    if (status != A_STATUS_OK) {
        (void)fdb_tsdb_deinit(&handle->db);
        return status;
    }
    aDataBaseInstanceAdd(&handle->instance);
    return A_STATUS_OK;
}

static aStatus_t kv_deinitialize(aDataBaseKvHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->db.parent.init_ok) return A_STATUS_NOT_READY;
    status = aDataBaseOperationBegin(A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    (void)fdb_kvdb_deinit(&handle->db);
    aDataBaseInstanceRemove(&handle->instance);
    return aDataBaseOperationEnd(A_STATUS_OK);
}

static aStatus_t ts_deinitialize(aDataBaseTsHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (!handle->db.parent.init_ok) return A_STATUS_NOT_READY;
    status = aDataBaseOperationBegin(A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    (void)fdb_tsdb_deinit(&handle->db);
    aDataBaseInstanceRemove(&handle->instance);
    return aDataBaseOperationEnd(A_STATUS_OK);
}

#if ADATABASE_STATIC_ENABLE
aStatus_t aDataBaseKvInitStatic(
    const aDataBaseKvConfig_t *config, aDataBaseKvHandle_t *handle)
{
    return kv_initialize(config, handle);
}

aStatus_t aDataBaseKvDeInitStatic(aDataBaseKvHandle_t *handle)
{
    if (handle == NULL || handle->instance.dynamic)
        return A_STATUS_INVALID_PARAM;
    return kv_deinitialize(handle);
}

aStatus_t aDataBaseTsInitStatic(
    const aDataBaseTsConfig_t *config, aDataBaseTsHandle_t *handle)
{
    return ts_initialize(config, handle);
}

aStatus_t aDataBaseTsDeInitStatic(aDataBaseTsHandle_t *handle)
{
    if (handle == NULL || handle->instance.dynamic)
        return A_STATUS_INVALID_PARAM;
    return ts_deinitialize(handle);
}
#endif

#if ADATABASE_DYNAMIC_ENABLE
aStatus_t aDataBaseKvCreate(
    const aDataBaseKvConfig_t *config, aDataBaseKvHandle_t **handle_out)
{
    aDataBaseKvHandle_t *handle;
    aStatus_t status;

    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = kv_initialize(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    handle->instance.dynamic = A_TRUE;
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aDataBaseKvDestroy(aDataBaseKvHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL || !handle->instance.dynamic)
        return A_STATUS_INVALID_PARAM;
    status = kv_deinitialize(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}

aStatus_t aDataBaseTsCreate(
    const aDataBaseTsConfig_t *config, aDataBaseTsHandle_t **handle_out)
{
    aDataBaseTsHandle_t *handle;
    aStatus_t status;

    if (handle_out == NULL) return A_STATUS_INVALID_PARAM;
    *handle_out = NULL;
    handle = aOSAlloc(sizeof(*handle));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    status = ts_initialize(config, handle);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    handle->instance.dynamic = A_TRUE;
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aDataBaseTsDestroy(aDataBaseTsHandle_t *handle)
{
    aStatus_t status;

    if (handle == NULL || !handle->instance.dynamic)
        return A_STATUS_INVALID_PARAM;
    status = ts_deinitialize(handle);
    if (status == A_STATUS_OK) aOSFree(handle);
    return status;
}
#endif

aStatus_t aDataBaseKvSet(
    aDataBaseKvHandle_t *handle, const aDataBaseKvSetRequest_t *request)
{
    struct fdb_blob blob;
    aStatus_t status;

    if (handle == NULL || request == NULL || !key_valid(request->key) ||
        request->data == NULL || request->size == 0U)
        return A_STATUS_INVALID_PARAM;
    status = instance_begin(&handle->instance, handle->db.parent.init_ok,
                             request->timeout);
    if (status != A_STATUS_OK) return status;
    if (handle->db.parent.sec_size <= record_overhead || request->size >
        handle->db.parent.sec_size - record_overhead)
        return instance_end(&handle->instance, A_STATUS_INVALID_PARAM);
    fdb_blob_make(&blob, request->data, request->size);
    status = result_get(fdb_kv_set_blob(&handle->db, request->key, &blob));
    return instance_end(&handle->instance, status);
}

aStatus_t aDataBaseKvGet(
    aDataBaseKvHandle_t *handle, const aDataBaseKvGetRequest_t *request)
{
    struct fdb_kv kv;
    struct fdb_blob blob;
    aStatus_t status;

    if (handle == NULL || request == NULL || !key_valid(request->key) ||
        request->size_out == NULL ||
        (request->data == NULL && request->capacity != 0U))
        return A_STATUS_INVALID_PARAM;
    *request->size_out = 0U;
    status = instance_begin(&handle->instance, handle->db.parent.init_ok,
                             request->timeout);
    if (status != A_STATUS_OK) return status;
    if (fdb_kv_get_obj(&handle->db, request->key, &kv) == NULL)
        return instance_end(&handle->instance, A_STATUS_NOT_FOUND);
    *request->size_out = kv.value_len;
    if (request->data == NULL && request->capacity == 0U)
        return instance_end(&handle->instance, A_STATUS_OK);
    if (request->capacity < kv.value_len)
        return instance_end(&handle->instance, A_STATUS_NO_MEMORY);
    fdb_blob_make(&blob, request->data, request->capacity);
    fdb_kv_to_blob(&kv, &blob);
    status = fdb_blob_read(&handle->db.parent, &blob) == kv.value_len
             ? A_STATUS_OK : A_STATUS_ERROR;
    return instance_end(&handle->instance, status);
}

aStatus_t aDataBaseKvDelete(
    aDataBaseKvHandle_t *handle, const aDataBaseKvDeleteRequest_t *request)
{
    struct fdb_kv kv;
    aStatus_t status;

    if (handle == NULL || request == NULL || !key_valid(request->key))
        return A_STATUS_INVALID_PARAM;
    status = instance_begin(&handle->instance, handle->db.parent.init_ok,
                             request->timeout);
    if (status != A_STATUS_OK) return status;
    if (fdb_kv_get_obj(&handle->db, request->key, &kv) == NULL)
        return instance_end(&handle->instance, A_STATUS_NOT_FOUND);
    status = result_get(fdb_kv_del(&handle->db, request->key));
    return instance_end(&handle->instance, status);
}

static aStatus_t ts_space_check(aDataBaseTsHandle_t *handle, size_t size)
{
    struct fdb_tsdb *db = &handle->db;
    uint32_t next;
    uint8_t status_table[FDB_STORE_STATUS_TABLE_SIZE];
    size_t next_status;

    if (db->rollover || db->cur_sec.status == FDB_SECTOR_STORE_EMPTY)
        return A_STATUS_OK;
    if (db->cur_sec.status == FDB_SECTOR_STORE_FULL)
        return A_STATUS_NO_MEMORY;
    if (db->cur_sec.remain >= size + record_overhead) return A_STATUS_OK;
    next = db->cur_sec.addr + db->parent.sec_size;
    if (next >= db->parent.max_size) return A_STATUS_NO_MEMORY;
    /* 官方禁止覆盖模式只阻止地址回绕；先检查下一扇区，保护已有旧记录。
     * 边界处保守预留索引空间，最多提前预留一个 record_overhead。 */
    next_status = _fdb_read_status(&db->parent, next, status_table,
                                   FDB_SECTOR_STORE_STATUS_NUM);
    return next_status == FDB_SECTOR_STORE_EMPTY
           ? A_STATUS_OK : A_STATUS_NO_MEMORY;
}

aStatus_t aDataBaseTsAppend(
    aDataBaseTsHandle_t *handle, const aDataBaseTsAppendRequest_t *request)
{
    struct fdb_blob blob;
    aStatus_t status;

    if (handle == NULL || request == NULL || request->data == NULL ||
        request->size == 0U || request->timestamp <= 0)
        return A_STATUS_INVALID_PARAM;
    status = instance_begin(&handle->instance, handle->db.parent.init_ok,
                             request->timeout);
    if (status != A_STATUS_OK) return status;
    if (request->size > handle->db.max_len ||
        request->timestamp <= handle->db.last_time)
        return instance_end(&handle->instance, A_STATUS_INVALID_PARAM);
    status = ts_space_check(handle, request->size);
    if (status != A_STATUS_OK)
        return instance_end(&handle->instance, status);
    fdb_blob_make(&blob, request->data, request->size);
    status = result_get(fdb_tsl_append_with_ts(&handle->db, &blob,
                                              request->timestamp));
    return instance_end(&handle->instance, status);
}

typedef struct {
    aDataBaseTsHandle_t *handle;
    const aDataBaseTsIterateRequest_t *request;
    aStatus_t status;
} iterate_context_t;

static bool ts_record_read(fdb_tsl_t tsl, void *context)
{
    iterate_context_t *iteration = context;
    const aDataBaseTsIterateRequest_t *request = iteration->request;
    aDataBaseTsRecord_t record;
    struct fdb_blob blob;

    if (aDataBaseStorageError() != A_STATUS_OK) return true;
    if (tsl->status != FDB_TSL_WRITE) return false;
    if (tsl->log_len > request->capacity) {
        iteration->status = A_STATUS_NO_MEMORY;
        return true;
    }
    fdb_blob_make(&blob, request->buffer, request->capacity);
    fdb_tsl_to_blob(tsl, &blob);
    if (fdb_blob_read(&iteration->handle->db.parent, &blob) != tsl->log_len) {
        iteration->status = A_STATUS_ERROR;
        return true;
    }
    record.timestamp = tsl->time;
    record.data = request->buffer;
    record.size = tsl->log_len;
    return !request->callback(&record, request->context);
}

aStatus_t aDataBaseTsIterate(
    aDataBaseTsHandle_t *handle, const aDataBaseTsIterateRequest_t *request)
{
    iterate_context_t iteration;
    aStatus_t status;

    if (handle == NULL || request == NULL || request->buffer == NULL ||
        request->capacity == 0U || request->callback == NULL ||
        request->from < 0 || request->to < request->from)
        return A_STATUS_INVALID_PARAM;
    status = instance_begin(&handle->instance, handle->db.parent.init_ok,
                             request->timeout);
    if (status != A_STATUS_OK) return status;
    iteration = (iterate_context_t){ handle, request, A_STATUS_OK };
    fdb_tsl_iter_by_time(&handle->db, request->from, request->to,
                          ts_record_read, &iteration);
    return instance_end(&handle->instance, iteration.status);
}

aStatus_t aDataBaseTsGetInfo(
    aDataBaseTsHandle_t *handle, aDataBaseTsInfo_t *info)
{
    aStatus_t status;

    if (handle == NULL || info == NULL) return A_STATUS_INVALID_PARAM;
    status = instance_begin(&handle->instance, handle->db.parent.init_ok,
                             A_TIMEOUT_FOREVER);
    if (status != A_STATUS_OK) return status;
    info->last_timestamp = handle->db.last_time;
    info->max_record_size = handle->db.max_len;
    info->rollover = handle->db.rollover;
    return instance_end(&handle->instance, A_STATUS_OK);
}
