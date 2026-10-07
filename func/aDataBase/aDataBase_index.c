#include "aDataBase_internal.h"
#include <string.h>

static const size_t sig_key_size = 14U;

/* 只接受规范化的大写十六进制键，避免同一 SIG 出现多个名字。 */
static aBool_t key_parse(const char *key, size_t len,
                        uint16_t *device, uint16_t *sig)
{
    uint16_t values[2] = {0U, 0U};

    if (len != sig_key_size || memcmp(key, "@sig:", 5U) != 0 ||
        key[9] != ':') return A_FALSE;
    for (size_t part = 0U; part < 2U; ++part) {
        for (size_t i = 0U; i < 4U; ++i) {
            char ch = key[5U + part * 5U + i];
            unsigned digit;
            if (ch >= '0' && ch <= '9') digit = (unsigned)(ch - '0');
            else if (ch >= 'A' && ch <= 'F')
                digit = (unsigned)(ch - 'A') + 10U;
            else return A_FALSE;
            values[part] = (uint16_t)((values[part] << 4U) | digit);
        }
    }
    *device = values[0];
    *sig = values[1];
    return A_TRUE;
}

static void key_make(char *key, uint16_t device, uint16_t sig)
{
    static const char digits[] = "0123456789ABCDEF";
    uint16_t values[2] = {device, sig};

    memcpy(key, "@sig:0000:0000", 15U);
    for (size_t part = 0U; part < 2U; ++part)
        for (size_t i = 0U; i < 4U; ++i)
            key[5U + part * 5U + i] =
                digits[(values[part] >> (12U - i * 4U)) & 15U];
}

static uint32_t key_number(uint16_t device, uint16_t sig)
{
    return ((uint32_t)device << 16U) | sig;
}

/* 清单在程序 Flash，RAM 只有密集排列的偏移，不为未持久化 SIG 留空位。 */
static size_t slot_position(const aDataBaseSigKey_t *keys, size_t count,
                            uint16_t device, uint16_t sig)
{
    size_t low = 0U, high = count;
    uint32_t key = key_number(device, sig);

    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        const aDataBaseSigKey_t *candidate = &keys[mid];
        uint32_t current = key_number(candidate->deviceID, candidate->sigKey);
        if (current < key) low = mid + 1U;
        else if (current > key) high = mid;
        else return mid;
    }
    return SIZE_MAX;
}

static uint32_t *slot_find(aDataBaseKvHandle_t *handle,
                           uint16_t device, uint16_t sig)
{
    size_t pos = slot_position(handle->persist_sigs, handle->persist_count,
                               device, sig);
    return pos == SIZE_MAX ? NULL : &handle->index.sig_offsets[pos];
}

/* 字符串接口、启动和 GC 共用二分查找；当前 SIG 请求直接命中槽位。 */
static uint32_t *index_slot(void *context, const char *key, size_t len)
{
    aDataBaseKvHandle_t *handle = context;
    uint16_t device, sig;

    if (handle->active_slot != NULL && len == sig_key_size &&
        memcmp(key, handle->active_key, len) == 0)
        return handle->active_slot;
    if (!key_parse(key, len, &device, &sig)) return NULL;
    return slot_find(handle, device, sig);
}

static aStatus_t definitions_check(const aDataBaseKvConfig_t *config,
                                  size_t max_size)
{
    if ((config->tables == NULL) != (config->table_count == 0U) ||
        (config->persist_sigs == NULL) != (config->persist_count == 0U))
        return A_STATUS_INVALID_PARAM;
    for (size_t t = 0U; t < config->table_count; ++t) {
        if (config->tables[t].sigs == NULL ||
            config->tables[t].sig_count == 0U)
            return A_STATUS_INVALID_PARAM;
    }
    /* 只检查持久化项：顺序、唯一键及匹配定义；不复制或索引整张 aBus 表。 */
    for (size_t p = 0U; p < config->persist_count; ++p) {
        const aDataBaseSigKey_t *key = &config->persist_sigs[p];
        const aBusSig_t *found = NULL;
        aBool_t device_found = A_FALSE;
        if (p != 0U) {
            const aDataBaseSigKey_t *last = &config->persist_sigs[p - 1U];
            if (key_number(last->deviceID, last->sigKey) >=
                key_number(key->deviceID, key->sigKey))
                return A_STATUS_INVALID_PARAM;
        }
        for (size_t t = 0U; t < config->table_count; ++t) {
            const aBusTable_t *table = &config->tables[t];
            if (table->deviceID != key->deviceID) continue;
            if (device_found) return A_STATUS_INVALID_PARAM;
            device_found = A_TRUE;
            for (size_t i = 0U; i < table->sig_count; ++i) {
                if (table->sigs[i].sigKey != key->sigKey) continue;
                if (found != NULL) return A_STATUS_INVALID_PARAM;
                found = &table->sigs[i];
            }
        }
        if (found == NULL || found->size == 0U || found->size > max_size)
            return A_STATUS_INVALID_PARAM;
    }
    if (config->sig_slots != NULL) {
        /* 可选直接映射只校验一次；业务读写不再按 sigKey 二分。 */
        for (size_t t = 0U; t < config->table_count; ++t) {
            const aBusTable_t *table = &config->tables[t];
            const uint16_t *slots = config->sig_slots[t];
            if (slots == NULL) continue;
            for (size_t i = 0U; i < table->sig_count; ++i) {
                size_t pos = slot_position(config->persist_sigs,
                    config->persist_count, table->deviceID,
                    table->sigs[i].sigKey);
                if (pos == SIZE_MAX) {
                    if (slots[i] != ADATABASE_SIG_SLOT_NONE)
                        return A_STATUS_INVALID_PARAM;
                } else if (pos >= ADATABASE_SIG_SLOT_NONE || slots[i] != pos) {
                    return A_STATUS_INVALID_PARAM;
                }
            }
        }
    }
    return A_STATUS_OK;
}

aStatus_t aDataBaseIndexPrepare(aDataBaseKvHandle_t *handle,
    const aDataBaseKvConfig_t *config, aBool_t dynamic)
{
    aMemoryInfo_t info;
    size_t sig_count = config->persist_count;
    size_t sector_count, sector_bytes;
    aStatus_t status;

    status = aMemoryGetInfo(handle->instance.partition, &info);
    if (status != A_STATUS_OK) return status;
    if (info.geometry.erase_granularity <= 128U ||
        info.geometry.capacity > UINT32_MAX ||
        info.geometry.capacity % info.geometry.erase_granularity != 0U)
        return A_STATUS_INVALID_PARAM;
    status = definitions_check(config,
        info.geometry.erase_granularity - 128U);
    if (status != A_STATUS_OK) return status;
    sector_count = (size_t)(info.geometry.capacity /
                           info.geometry.erase_granularity);
    if (sector_count > SIZE_MAX / sizeof(struct kvdb_sec_info))
        return A_STATUS_NO_MEMORY;
    sector_bytes = sector_count * sizeof(struct kvdb_sec_info);
    if (sig_count > (SIZE_MAX - sector_bytes) / sizeof(uint32_t))
        return A_STATUS_NO_MEMORY;
    if (config->index_storage != NULL) {
        handle->index = *config->index_storage;
        if (handle->index.sectors == NULL ||
            handle->index.sector_capacity < sector_count ||
            (sig_count != 0U && handle->index.sig_offsets == NULL) ||
            handle->index.sig_capacity < sig_count)
            return A_STATUS_NO_MEMORY;
    } else if (dynamic) {
#if ADATABASE_DYNAMIC_ENABLE
        /* 整个实例仅申请一块缓存，避免逐 SIG 分配的堆开销。 */
        handle->index_memory = aOSAlloc(sector_bytes +
                                       sig_count * sizeof(uint32_t));
        if (handle->index_memory == NULL) return A_STATUS_NO_MEMORY;
        handle->index.sectors = handle->index_memory;
        handle->index.sector_capacity = sector_count;
        handle->index.sig_offsets = (uint32_t *)(
            (uint8_t *)handle->index_memory + sector_bytes);
        handle->index.sig_capacity = sig_count;
#endif
    } else if (sig_count != 0U) {
        return A_STATUS_NO_MEMORY;
    }
    handle->tables = config->tables;
    handle->table_count = config->table_count;
    handle->persist_sigs = config->persist_sigs;
    handle->persist_count = sig_count;
    handle->sig_slots = config->sig_slots;
    for (size_t i = 0U; i < sig_count; ++i)
        handle->index.sig_offsets[i] = UINT32_MAX;
    if (handle->index.sectors != NULL) {
        memset(handle->index.sectors, 0, sector_bytes);
        for (size_t i = 0U; i < sector_count; ++i)
            handle->index.sectors[i].addr = UINT32_MAX;
        handle->db.sector_index = handle->index.sectors;
        handle->db.sector_index_count = sector_count;
    }
    return A_STATUS_OK;
}

aStatus_t aDataBaseIndexBuild(aDataBaseKvHandle_t *handle)
{
    struct fdb_kv_iterator iterator;

    if (handle->db.sector_index != NULL &&
        fdb_kvdb_cache_prepare(&handle->db) != FDB_NO_ERR)
        return A_STATUS_ERROR;
    if (handle->persist_count == 0U) return A_STATUS_OK;
    /* 官方先完成恢复，再一次扫描构建完整索引；未出现的项保持不存在。 */
    fdb_kv_iterator_init(&handle->db, &iterator);
    while (aDataBaseStorageError() == A_STATUS_OK &&
           fdb_kv_iterate(&handle->db, &iterator)) {
        struct fdb_kv *kv = &iterator.curr_kv;
        uint32_t *slot = index_slot(handle, kv->name, kv->name_len);
        if (slot != NULL) {
            if (*slot != UINT32_MAX) return A_STATUS_ERROR;
            *slot = kv->addr.start;
        }
    }
    if (aDataBaseStorageError() != A_STATUS_OK)
        return aDataBaseStorageError();
    handle->db.index_context = handle;
    handle->db.index_slot = index_slot;
    return A_STATUS_OK;
}

void aDataBaseIndexRelease(aDataBaseKvHandle_t *handle)
{
#if ADATABASE_DYNAMIC_ENABLE
    aOSFree(handle->index_memory);
#endif
    handle->index_memory = NULL;
    handle->db.index_slot = NULL;
    handle->db.index_context = NULL;
    handle->db.sector_index = NULL;
    handle->db.sector_index_count = 0U;
    handle->active_slot = NULL;
}

aStatus_t aDataBaseIndexSelect(aDataBaseKvHandle_t *handle,
    uint16_t deviceID, size_t sigIndex, const aBusSig_t **sig)
{
    for (size_t t = 0U; t < handle->table_count; ++t) {
        const aBusTable_t *table = &handle->tables[t];
        if (table->deviceID == deviceID) {
            if (sigIndex >= table->sig_count) return A_STATUS_NOT_FOUND;
            *sig = &table->sigs[sigIndex];
            key_make(handle->active_key, deviceID, (*sig)->sigKey);
            if (handle->sig_slots != NULL && handle->sig_slots[t] != NULL) {
                uint16_t slot = handle->sig_slots[t][sigIndex];
                if (slot == ADATABASE_SIG_SLOT_NONE)
                    return A_STATUS_UNSUPPORTED;
                handle->active_slot = &handle->index.sig_offsets[slot];
            } else {
                handle->active_slot = slot_find(handle, deviceID,
                                                (*sig)->sigKey);
            }
            if (handle->active_slot == NULL) return A_STATUS_UNSUPPORTED;
            return A_STATUS_OK;
        }
    }
    return A_STATUS_NOT_FOUND;
}
