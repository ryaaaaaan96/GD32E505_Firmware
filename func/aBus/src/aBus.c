#include "aBus_instance.h"
#if ABUS_DYNAMIC_ENABLE
#include "aOS.h"
#endif
#include <assert.h>
#include <string.h>

#ifndef ABUS_DEF_CHECK_ENABLE
#define ABUS_DEF_CHECK_ENABLE 1
#endif
#if ABUS_DEF_CHECK_ENABLE != 0 && ABUS_DEF_CHECK_ENABLE != 1
#error "ABUS_DEF_CHECK_ENABLE must be 0 or 1"
#endif

static aBool_t value_in_range(aDataType_t type,
    const aBusRange_t *range, aDataValue_t value)
{
    if (range == NULL) return A_TRUE;
    switch (type) {
    case ALIB_DATA_U8:
        return value.u8 >= range->min.u8 && value.u8 <= range->max.u8;
    case ALIB_DATA_U16:
        return value.u16 >= range->min.u16 && value.u16 <= range->max.u16;
    case ALIB_DATA_U32:
        return value.u32 >= range->min.u32 && value.u32 <= range->max.u32;
    case ALIB_DATA_S32:
        return value.s32 >= range->min.s32 && value.s32 <= range->max.s32;
    default: return A_FALSE;
    }
}

/* memcpy 支持非对齐字段；NULL 源表示初始化时的全零默认值。 */
static aStatus_t range_check(aDataType_t type, const aBusRange_t *range,
                             const void *src)
{
    aDataValue_t value = {.u32 = 0U};

    if (range == NULL) return A_STATUS_OK;
    switch (type) {
    case ALIB_DATA_U8:
        if (src != NULL) memcpy(&value.u8, src, sizeof(value.u8));
        break;
    case ALIB_DATA_U16:
        if (src != NULL) memcpy(&value.u16, src, sizeof(value.u16));
        break;
    case ALIB_DATA_U32:
        if (src != NULL) memcpy(&value.u32, src, sizeof(value.u32));
        break;
    case ALIB_DATA_S32:
        if (src != NULL) memcpy(&value.s32, src, sizeof(value.s32));
        break;
    default: return A_STATUS_INVALID_PARAM;
    }
    return value_in_range(type, range, value) ? A_STATUS_OK :
           A_STATUS_INVALID_PARAM;
}

static aStatus_t values_check(const aBusSig_t *sig, const void *src)
{
    aStatus_t status = range_check(sig->type, sig->range, src);

    if (status != A_STATUS_OK) return status;
    for (size_t i = 0U; i < sig->param_count; i++) {
        const aBusParam_t *item = &sig->params[i];
        const void *field = src == NULL ? NULL :
            (const unsigned char *)src + item->offset;

        status = range_check(item->type, item->range, field);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

#if ABUS_DEF_CHECK_ENABLE
static aBool_t type_valid(aDataType_t type, size_t size,
                          const aBusRange_t *range)
{
    if (size == 0U) return A_FALSE;
    if (type == ALIB_DATA_RAW || type == ALIB_DATA_STRUCT) {
        return range == NULL;
    }
    return aDataTypeSize(type) != 0U && aDataTypeSize(type) == size &&
           (range == NULL || value_in_range(type, range, range->min));
}

/* 定义检查只在初始化执行；写入范围检查始终保留。 */
static aStatus_t definitions_check(const aBusTable_t *table)
{
    for (size_t i = 0; i < table->sig_count; i++) {
        const aBusSig_t *def = &table->sigs[i];

        if (!type_valid(def->type, def->size, def->range) ||
            (def->flags & ~ABUS_SIG_FLAG_LOCK) != 0U ||
            (def->param_count != 0U && def->params == NULL) ||
            (def->type != ALIB_DATA_STRUCT &&
             (def->params != NULL || def->param_count != 0U))) {
            return A_STATUS_INVALID_PARAM;
        }
        /* 每个字段检查类型、长度、边界、可选上下限；不递归。 */
        for (size_t j = 0; j < def->param_count; j++) {
            const aBusParam_t *field = &def->params[j];

            if (field->type == ALIB_DATA_STRUCT ||
                !type_valid(field->type, field->size, field->range) ||
                field->offset > def->size ||
                field->size > def->size - field->offset) {
                return A_STATUS_INVALID_PARAM;
            }
        }
        if (values_check(def, def->default_data) != A_STATUS_OK) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    return A_STATUS_OK;
}
#endif

/* 链接脚本始终提供边界，即使没有任何绑定。段内只保存对齐的指针。 */
extern const aBusRamBinding_t *const __abus_bindings_start[];
extern const aBusRamBinding_t *const __abus_bindings_end[];

static const aBusTable_t *table_find(const aBusTable_t *tables,
    size_t count, uint16_t deviceID, size_t *offset)
{
    *offset = 0U;
    for (size_t i = 0; i < count; i++) {
        if (tables[i].deviceID == deviceID) return &tables[i];
        *offset += tables[i].sig_count;
    }
    return NULL;
}

static void resources_release(aBusHandle_t *handle)
{
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    aOSMutexDestroy(&handle->mutex);
#endif
    for (size_t i = 0; i < handle->sig_count; i++) {
        const aBusSigState_t empty = {.data = NULL};
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
        aOSMutexDestroy(&handle->sigs[i].mutex);
#endif
        handle->sigs[i] = empty;
    }
#if ABUS_DYNAMIC_ENABLE
    aOSFree(handle->allocation);
    handle->allocation = NULL;
#endif
    handle->instanceID = 0U;
    handle->tables = NULL;
    handle->table_count = 0U;
    handle->sig_count = 0U;
}

/* 只遍历一次注册段，按 deviceID + 表内下标收集。 */
static aStatus_t bindings_collect(aBusHandle_t *handle,
    const aBusTable_t *tables, size_t count)
{
    const aBusRamBinding_t *const *cursor;

    for (cursor = __abus_bindings_start;
         cursor != __abus_bindings_end; cursor++) {
        const aBusRamBinding_t *binding = *cursor;
        const aBusTable_t *table;
        size_t offset;

        if (binding->instanceID != handle->instanceID) continue;
        table = table_find(tables, count, binding->deviceID, &offset);
        if (table == NULL) continue;
        if (binding->sigIndex >= table->sig_count || binding->data == NULL ||
            binding->size < table->sigs[binding->sigIndex].size ||
            handle->sigs[offset + binding->sigIndex].data != NULL) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->sigs[offset + binding->sigIndex].data = binding->data;
    }
    return A_STATUS_OK;
}

static aStatus_t tables_check(const aBusTable_t *tables, size_t count,
                              size_t *total)
{
    assert(tables != NULL);
    assert(count != 0U);
    *total = 0U;
    /* deviceID 是 uint16_t；该上限也限制重复检查的输入规模。 */
    if (count > (size_t)UINT16_MAX + 1U) return A_STATUS_INVALID_PARAM;
    for (size_t i = 0; i < count; i++) {
        const aBusTable_t *table = &tables[i];
#if ABUS_DEF_CHECK_ENABLE
        aStatus_t status;
#endif

        assert(table->sigs != NULL);
        assert(table->sig_count != 0U);
        assert(table->sig_count <= (size_t)UINT16_MAX + 1U);
        /* 挂载歧义始终拒绝，不受定义检查开关影响。 */
        for (size_t j = 0; j < i; j++) {
            if (tables[j].deviceID == table->deviceID) {
                return A_STATUS_INVALID_PARAM;
            }
        }
        if (table->sig_count > SIZE_MAX - *total) {
            return A_STATUS_INVALID_PARAM;
        }
        *total += table->sig_count;
#if ABUS_DEF_CHECK_ENABLE
        status = definitions_check(table);
        if (status != A_STATUS_OK) return status;
#endif
    }
    return A_STATUS_OK;
}

/* 全部表准备成功后才统一写默认值，后表失败不会污染前表数据。 */
static aStatus_t resources_prepare(aBusHandle_t *handle,
    const aBusTable_t *tables, size_t count, size_t total)
{
    aStatus_t status;
    size_t missing = 0U;
#if ABUS_DYNAMIC_ENABLE
    unsigned char *next;
#endif

    handle->sig_count = total;
    for (size_t i = 0; i < total; i++) {
        const aBusSigState_t empty = {.data = NULL};
        handle->sigs[i] = empty;
    }
    status = bindings_collect(handle, tables, count);
    if (status != A_STATUS_OK) goto fail;
    /* 按表顺序访问定义与 RAM，不为每个 SIG 从第一张表重新查找。 */
    for (size_t t = 0U, offset = 0U; t < count; t++) {
        const aBusTable_t *table = &tables[t];
        for (size_t i = 0U; i < table->sig_count; i++) {
            const aBusSig_t *sig = &table->sigs[i];

            if (handle->sigs[offset + i].data != NULL) continue;
#if ABUS_STATIC_ENABLE
#if ABUS_DYNAMIC_ENABLE
            if (!handle->dynamic_storage)
#endif
            {
                status = A_STATUS_NOT_FOUND;
                goto fail;
            }
#endif
            if (sig->size > SIZE_MAX - missing) {
                status = A_STATUS_INVALID_PARAM;
                goto fail;
            }
            missing += sig->size;
        }
        offset += table->sig_count;
    }
#if ABUS_DYNAMIC_ENABLE
    if (missing != 0U) {
        handle->allocation = aOSAlloc(missing);
        if (handle->allocation == NULL) {
            status = A_STATUS_NO_MEMORY;
            goto fail;
        }
    }
#endif
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    for (size_t i = 0; i < total; i++) {
        status = aOSMutexCreate(&handle->sigs[i].mutex);
        if (status != A_STATUS_OK) goto fail;
    }
#endif
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    status = aOSMutexCreate(&handle->mutex);
    if (status != A_STATUS_OK) goto fail;
#endif
#if ABUS_DYNAMIC_ENABLE
    next = handle->allocation;
#endif
    /* 资源全部就绪后，合并数据地址分配与默认值写入这一遍遍历。 */
    for (size_t t = 0U, offset = 0U; t < count; t++) {
        const aBusTable_t *table = &tables[t];
        for (size_t i = 0U; i < table->sig_count; i++) {
            const aBusSig_t *sig = &table->sigs[i];
            aBusSigState_t *entry = &handle->sigs[offset + i];

#if ABUS_DYNAMIC_ENABLE
            if (entry->data == NULL) {
                entry->data = next;
                next += sig->size;
            }
#endif
            if (sig->default_data != NULL) {
                memcpy(entry->data, sig->default_data, sig->size);
            } else {
                memset(entry->data, 0, sig->size);
            }
        }
        offset += table->sig_count;
    }
    handle->tables = tables;
    handle->table_count = count;
    return A_STATUS_OK;
fail:
    resources_release(handle);
    return status;
}

#if ABUS_STATIC_ENABLE
aStatus_t aBusInitStatic(uint16_t instanceID,
    const aBusTable_t *tables, size_t table_count,
    aBusHandle_t *handle)
{
    aStatus_t status;
    size_t total;

    assert(handle != NULL);
    if (handle->tables != NULL) return A_STATUS_BUSY;
#if ABUS_DYNAMIC_ENABLE
    if (handle->dynamic_storage) return A_STATUS_INVALID_PARAM;
#endif
    status = tables_check(tables, table_count, &total);
    if (status != A_STATUS_OK) return status;
    if (handle->sigs == NULL || handle->capacity < total) {
        return A_STATUS_INVALID_PARAM;
    }
    handle->instanceID = instanceID;
    return resources_prepare(handle, tables, table_count, total);
}

aStatus_t aBusDeInitStatic(aBusHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
#if ABUS_DYNAMIC_ENABLE
    if (handle->dynamic_storage) return A_STATUS_INVALID_PARAM;
#endif
    resources_release(handle);
    return A_STATUS_OK;
}
#endif

#if ABUS_DYNAMIC_ENABLE
aStatus_t aBusCreate(uint16_t instanceID,
    const aBusTable_t *tables, size_t table_count,
    aBusHandle_t **handle_out)
{
    aBusHandle_t *handle;
    aStatus_t status;
    size_t total;
    size_t offset = sizeof(aBusHandle_t);
    size_t alignment = _Alignof(aBusSigState_t);
    size_t padding = (alignment - offset % alignment) % alignment;

    assert(handle_out != NULL);
    *handle_out = NULL;
    status = tables_check(tables, table_count, &total);
    if (status != A_STATUS_OK) return status;
    offset += padding;
    if (total > (SIZE_MAX - offset) / sizeof(aBusSigState_t)) {
        return A_STATUS_INVALID_PARAM;
    }
    handle = aOSAlloc(offset + total * sizeof(aBusSigState_t));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    {
        const aBusHandle_t initial = {
            .instanceID = instanceID,
            .sigs = (aBusSigState_t *)((unsigned char *)handle + offset),
#if ABUS_STATIC_ENABLE
            .dynamic_storage = A_TRUE,
#endif
        };
        *handle = initial;
    }
    status = resources_prepare(handle, tables, table_count, total);
    if (status != A_STATUS_OK) {
        aOSFree(handle);
        return status;
    }
    *handle_out = handle;
    return A_STATUS_OK;
}

aStatus_t aBusDestroy(aBusHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
#if ABUS_STATIC_ENABLE
    if (!handle->dynamic_storage) return A_STATUS_INVALID_PARAM;
#endif
    resources_release(handle);
    aOSFree(handle);
    return A_STATUS_OK;
}
#endif

/* 定义与当前值共用下标，RAM 条目无需重复保存定义指针。 */
static size_t sig_find(const aBusTable_t *table, uint16_t sigKey)
{
    for (size_t i = 0; i < table->sig_count; i++) {
        if (table->sigs[i].sigKey == sigKey) return i;
    }
    return SIZE_MAX;
}

aStatus_t aBusResolveKey(aBusHandle_t *handle,
    const aBusSigKeyQuery_t *query, size_t *sigIndex)
{
    const aBusTable_t *table;
    size_t offset;
    size_t index;

    assert(handle != NULL && query != NULL && sigIndex != NULL);
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    table = table_find(handle->tables, handle->table_count,
                       query->deviceID, &offset);
    if (table == NULL) return A_STATUS_NOT_FOUND;
    index = sig_find(table, query->sigKey);
    if (index == SIZE_MAX) return A_STATUS_NOT_FOUND;
    *sigIndex = index;
    return A_STATUS_OK;
}

#ifndef NDEBUG
static aBool_t buffer_is_valid(aBusSigState_t *entry, const aBusSig_t *sig,
                               const void *buffer,
                               size_t size)
{
    uintptr_t start = (uintptr_t)buffer;
    uintptr_t data = (uintptr_t)entry->data;

    return size == sig->size && start <= UINTPTR_MAX - size &&
           data <= UINTPTR_MAX - size &&
           !(start < data + size && data < start + size);
}
#endif

aStatus_t aBusGetSigInfo(aBusHandle_t *handle,
    const aBusSigQuery_t *query, aBusSigInfo_t *info)
{
    const aBusTable_t *table;
    const aBusSig_t *sig;
    size_t offset;

    assert(handle != NULL && query != NULL && info != NULL);
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    table = table_find(handle->tables, handle->table_count,
                       query->deviceID, &offset);
    if (table == NULL || query->sigIndex >= table->sig_count) {
        return A_STATUS_NOT_FOUND;
    }
    sig = &table->sigs[query->sigIndex];
    info->sigKey = sig->sigKey;
    info->flags = sig->flags;
    info->type = sig->type;
    info->size = sig->size;
    info->range = sig->range;
    info->params = sig->params;
    info->param_count = sig->param_count;
    return A_STATUS_OK;
}

aStatus_t aBusSetByIndex(aBusHandle_t *handle,
                    const aBusSetIndexRequest_t *request)
{
    aBusSigState_t *entry;
    const aBusSig_t *sig;
    size_t index;
    size_t offset;
    const aBusTable_t *table;
    aStatus_t status;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex = NULL;
#endif

    assert(handle != NULL);
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->src != NULL && aTimeoutIsValid(request->timeout));
    table = table_find(handle->tables, handle->table_count,
                       request->deviceID, &offset);
    if (table == NULL) return A_STATUS_NOT_FOUND;
    index = request->sigIndex;
    if (index >= table->sig_count) return A_STATUS_NOT_FOUND;
    entry = &handle->sigs[offset + index];
    sig = &table->sigs[index];
    assert(buffer_is_valid(entry, sig, request->src, request->size));
    /* 范围校验始终执行，且不延长共享数据的持锁时间。 */
    status = values_check(sig, request->src);
    if (status != A_STATUS_OK) return status;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if ((sig->flags & ABUS_SIG_FLAG_LOCK) != 0) {
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
        mutex = handle->mutex;
#else
        mutex = entry->mutex;
#endif
        status = aOSMutexLock(mutex, request->timeout);
        if (status != A_STATUS_OK) return status;
    }
#endif
    memcpy(entry->data, request->src, request->size);
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if (mutex != NULL) return aOSMutexUnlock(mutex);
#endif
    return A_STATUS_OK;
}

aStatus_t aBusGetByIndex(aBusHandle_t *handle,
                    const aBusGetIndexRequest_t *request)
{
    aBusSigState_t *entry;
    const aBusSig_t *sig;
    size_t index;
    size_t offset;
    const aBusTable_t *table;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex = NULL;
    aStatus_t status;
#endif

    assert(handle != NULL);
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->dst != NULL && aTimeoutIsValid(request->timeout));
    table = table_find(handle->tables, handle->table_count,
                       request->deviceID, &offset);
    if (table == NULL) return A_STATUS_NOT_FOUND;
    index = request->sigIndex;
    if (index >= table->sig_count) return A_STATUS_NOT_FOUND;
    entry = &handle->sigs[offset + index];
    sig = &table->sigs[index];
    assert(buffer_is_valid(entry, sig, request->dst, request->size));
    (void)sig;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if ((sig->flags & ABUS_SIG_FLAG_LOCK) != 0) {
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
        mutex = handle->mutex;
#else
        mutex = entry->mutex;
#endif
        status = aOSMutexLock(mutex, request->timeout);
        if (status != A_STATUS_OK) return status;
    }
#endif
    memcpy(request->dst, entry->data, request->size);
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if (mutex != NULL) return aOSMutexUnlock(mutex);
#endif
    return A_STATUS_OK;
}

/* 稳定键仅用于定位，校验、锁和复制统一走下标入口。 */
aStatus_t aBusSetByKey(aBusHandle_t *handle,
    const aBusSetKeyRequest_t *request)
{
    aBusSetIndexRequest_t indexed;
    const aBusTable_t *table;
    size_t offset;

    assert(handle != NULL);
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->src != NULL && aTimeoutIsValid(request->timeout));
    aBusSetIndexRequestStructInit(&indexed);
    table = table_find(handle->tables, handle->table_count,
                       request->deviceID, &offset);
    if (table == NULL) return A_STATUS_NOT_FOUND;
    indexed.deviceID = request->deviceID;
    indexed.sigIndex = sig_find(table, request->sigKey);
    if (indexed.sigIndex == SIZE_MAX) return A_STATUS_NOT_FOUND;
    indexed.src = request->src;
    indexed.size = request->size;
    indexed.timeout = request->timeout;
    return aBusSetByIndex(handle, &indexed);
}

/* 稳定键仅用于定位，校验、锁和复制统一走下标入口。 */
aStatus_t aBusGetByKey(aBusHandle_t *handle,
    const aBusGetKeyRequest_t *request)
{
    aBusGetIndexRequest_t indexed;
    const aBusTable_t *table;
    size_t offset;

    assert(handle != NULL);
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->dst != NULL && aTimeoutIsValid(request->timeout));
    aBusGetIndexRequestStructInit(&indexed);
    table = table_find(handle->tables, handle->table_count,
                       request->deviceID, &offset);
    if (table == NULL) return A_STATUS_NOT_FOUND;
    indexed.deviceID = request->deviceID;
    indexed.sigIndex = sig_find(table, request->sigKey);
    if (indexed.sigIndex == SIZE_MAX) return A_STATUS_NOT_FOUND;
    indexed.dst = request->dst;
    indexed.size = request->size;
    indexed.timeout = request->timeout;
    return aBusGetByIndex(handle, &indexed);
}

/* 查询字段同时取得原 SIG 状态，整组/字段访问共用同一把锁。 */
static aStatus_t param_find(aBusHandle_t *handle, uint16_t deviceID,
    size_t sigIndex, size_t paramIndex, const aBusSig_t **sig,
    const aBusParam_t **param, aBusSigState_t **entry)
{
    const aBusTable_t *table;
    size_t offset;

    if (handle == NULL) return A_STATUS_INVALID_PARAM;
    if (handle->tables == NULL) return A_STATUS_NOT_READY;
    table = table_find(handle->tables, handle->table_count, deviceID,
                       &offset);
    if (table == NULL || sigIndex >= table->sig_count) {
        return A_STATUS_NOT_FOUND;
    }
    *sig = &table->sigs[sigIndex];
    if ((*sig)->type != ALIB_DATA_STRUCT) return A_STATUS_UNSUPPORTED;
    if (paramIndex >= (*sig)->param_count) return A_STATUS_NOT_FOUND;
    *param = &(*sig)->params[paramIndex];
    *entry = &handle->sigs[offset + sigIndex];
    return A_STATUS_OK;
}

static aBool_t param_buffer_valid(const aBusSig_t *sig,
    const aBusParam_t *param, const aBusSigState_t *entry,
    const void *buffer, size_t size)
{
    uintptr_t start = (uintptr_t)buffer;
    uintptr_t data = (uintptr_t)entry->data;

    return buffer != NULL && size == param->size &&
           start <= UINTPTR_MAX - size && data <= UINTPTR_MAX - sig->size &&
           !(start < data + sig->size && data < start + size);
}

#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
static aOSMutex_t param_mutex(aBusHandle_t *handle,
    const aBusSig_t *sig, aBusSigState_t *entry)
{
    if ((sig->flags & ABUS_SIG_FLAG_LOCK) == 0U) return NULL;
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    (void)entry;
    return handle->mutex;
#else
    (void)handle;
    return entry->mutex;
#endif
}
#endif

/* 在锁内拼出受影响的标量，不改原数据；兼容描述互相重叠的字段。 */
static aStatus_t patch_check(const aBusSig_t *sig,
    const aBusParam_t *target, const void *data, const void *src)
{
    size_t end = target->offset + target->size;

    for (size_t i = 0U; i < sig->param_count; i++) {
        const aBusParam_t *field = &sig->params[i];
        unsigned char value[sizeof(uint32_t)];
        size_t first;
        size_t last;
        aStatus_t status;

        if (field->range == NULL || field->offset >= end ||
            field->offset + field->size <= target->offset) continue;
        if (field->size > sizeof(value)) return A_STATUS_INVALID_PARAM;
        memcpy(value, (const unsigned char *)data + field->offset,
               field->size);
        first = field->offset > target->offset ? field->offset :
                target->offset;
        last = field->offset + field->size;
        if (last > end) last = end;
        memcpy(value + first - field->offset,
               (const unsigned char *)src + first - target->offset,
               last - first);
        status = range_check(field->type, field->range, value);
        if (status != A_STATUS_OK) return status;
    }
    return A_STATUS_OK;
}

aStatus_t aBusSetParam(aBusHandle_t *handle,
    const aBusSetParamRequest_t *request)
{
    const aBusSig_t *sig;
    const aBusParam_t *param;
    aBusSigState_t *entry;
    aStatus_t status;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex;
#endif

    if (request == NULL || !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    status = param_find(handle, request->deviceID, request->sigIndex,
                        request->paramIndex, &sig, &param, &entry);
    if (status != A_STATUS_OK) return status;
    if (!param_buffer_valid(sig, param, entry, request->src, request->size)) {
        return A_STATUS_INVALID_PARAM;
    }
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    mutex = param_mutex(handle, sig, entry);
    if (mutex != NULL) {
        status = aOSMutexLock(mutex, request->timeout);
        if (status != A_STATUS_OK) return status;
    }
#endif
    status = patch_check(sig, param, entry->data, request->src);
    if (status == A_STATUS_OK) {
        memcpy((unsigned char *)entry->data + param->offset,
               request->src, request->size);
    }
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if (mutex != NULL) {
        aStatus_t unlock_status = aOSMutexUnlock(mutex);

        if (status == A_STATUS_OK) status = unlock_status;
    }
#endif
    return status;
}

aStatus_t aBusGetParam(aBusHandle_t *handle,
    const aBusGetParamRequest_t *request)
{
    const aBusSig_t *sig;
    const aBusParam_t *param;
    aBusSigState_t *entry;
    aStatus_t status;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex;
#endif

    if (request == NULL || !aTimeoutIsValid(request->timeout)) {
        return A_STATUS_INVALID_PARAM;
    }
    status = param_find(handle, request->deviceID, request->sigIndex,
                        request->paramIndex, &sig, &param, &entry);
    if (status != A_STATUS_OK) return status;
    if (!param_buffer_valid(sig, param, entry, request->dst, request->size)) {
        return A_STATUS_INVALID_PARAM;
    }
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    mutex = param_mutex(handle, sig, entry);
    if (mutex != NULL) {
        status = aOSMutexLock(mutex, request->timeout);
        if (status != A_STATUS_OK) return status;
    }
#endif
    memcpy(request->dst, (const unsigned char *)entry->data + param->offset,
           request->size);
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if (mutex != NULL) return aOSMutexUnlock(mutex);
#endif
    return A_STATUS_OK;
}
