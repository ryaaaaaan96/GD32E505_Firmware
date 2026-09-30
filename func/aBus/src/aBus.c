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

static aBool_t value_in_range(const aBusParam_t *item, aDataValue_t value)
{
    switch (item->type) {
    case ALIB_DATA_U8:
        return value.u8 >= item->min.u8 && value.u8 <= item->max.u8;
    case ALIB_DATA_U16:
        return value.u16 >= item->min.u16 && value.u16 <= item->max.u16;
    case ALIB_DATA_U32:
        return value.u32 >= item->min.u32 && value.u32 <= item->max.u32;
    case ALIB_DATA_S32:
        return value.s32 >= item->min.s32 && value.s32 <= item->max.s32;
    default: return A_FALSE;
    }
}

/* 用 memcpy 读取字段，避免 offset 未对齐时直接解引用类型指针。 */
static aStatus_t values_check(const aBusSig_t *sig, const void *src)
{
    for (size_t i = 0U; i < sig->param_count; ++i) {
        const aBusParam_t *item = &sig->params[i];
        const unsigned char *field = (const unsigned char *)src + item->offset;
        aDataValue_t value = {.u32 = 0U};

        switch (item->type) {
        case ALIB_DATA_RAW:
            return A_STATUS_INVALID_PARAM;
        case ALIB_DATA_U8:
            memcpy(&value.u8, field, sizeof(value.u8));
            break;
        case ALIB_DATA_U16:
            memcpy(&value.u16, field, sizeof(value.u16));
            break;
        case ALIB_DATA_U32:
            memcpy(&value.u32, field, sizeof(value.u32));
            break;
        case ALIB_DATA_S32:
            memcpy(&value.s32, field, sizeof(value.s32));
            break;
        default: return A_STATUS_INVALID_PARAM;
        }
        if (!value_in_range(item, value)) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    return A_STATUS_OK;
}


#if ABUS_DEF_CHECK_ENABLE
/* 只检查静态规则；sigKey 唯一性由生成工具或应用保证。 */
static aStatus_t definitions_check(const aBusTable_t *table)
{
    for (size_t i = 0; i < table->sig_count; i++) {
        const aBusSig_t *def = &table->sigs[i];

        if (def->size == 0U ||
            (def->flags & ~ABUS_SIG_FLAG_LOCK) != 0U ||
            (def->param_count != 0U && def->params == NULL)) {
            return A_STATUS_INVALID_PARAM;
        }
        /* 每项只检查类型、边界与上下限；允许描述重叠范围。 */
        for (size_t j = 0; j < def->param_count; j++) {
            const aBusParam_t *field = &def->params[j];
            size_t size = aDataTypeSize(field->type);
            aDataValue_t zero = {.u32 = 0U};

            if (size == 0U || field->offset > def->size ||
                size > def->size - field->offset ||
                !value_in_range(field, field->min) ||
                (def->default_data == NULL &&
                 !value_in_range(field, zero))) {
                return A_STATUS_INVALID_PARAM;
            }
        }
        if (def->default_data != NULL &&
            values_check(def, def->default_data) != A_STATUS_OK) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    return A_STATUS_OK;
}
#endif

/* 链接脚本始终提供边界，即使没有任何绑定。段内只保存对齐的指针。 */
extern const aBusStorageBinding_t *const __abus_bindings_start[];
extern const aBusStorageBinding_t *const __abus_bindings_end[];

static void resources_release(aBusHandle_t *handle,
                              const aBusTable_t *table)
{
    if (table == NULL) return;
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    aOSMutexDestroy(&handle->mutex);
#endif
    for (size_t i = 0; i < table->sig_count; i++) {
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
    handle->table = NULL;
}

/* 绑定按表身份过滤，按下标 O(1) 匹配；重复绑定始终拒绝。 */
static aStatus_t bindings_collect(aBusHandle_t *handle,
                                 const aBusTable_t *table)
{
    const aBusStorageBinding_t *const *cursor;

    for (cursor = __abus_bindings_start;
         cursor != __abus_bindings_end; cursor++) {
        const aBusStorageBinding_t *binding = *cursor;

        if (binding->table != table) continue;
        if (binding->sigIndex >= table->sig_count || binding->data == NULL ||
            binding->size < table->sigs[binding->sigIndex].size ||
            handle->sigs[binding->sigIndex].data != NULL) {
            return A_STATUS_INVALID_PARAM;
        }
        handle->sigs[binding->sigIndex].data = binding->data;
    }
    return A_STATUS_OK;
}

static aStatus_t table_check(const aBusTable_t *table)
{
    assert(table != NULL);
    assert(table->sigs != NULL);
    assert(table->sig_count != 0);
    assert(table->sig_count <= (size_t)UINT16_MAX + 1U);

#if ABUS_DEF_CHECK_ENABLE
    return definitions_check(table);
#else
    (void)table;
    return A_STATUS_OK;
#endif
}

/* 收集静态地址后统计缺口；只有动态实例允许用一次分配补齐。 */
static aStatus_t resources_prepare(aBusHandle_t *handle,
                                   const aBusTable_t *table)
{
    aStatus_t status;
    size_t missing = 0U;
#if ABUS_DYNAMIC_ENABLE
    unsigned char *next;
#endif

    for (size_t i = 0; i < table->sig_count; i++) {
        const aBusSigState_t empty = {.data = NULL};
        handle->sigs[i] = empty;
    }
    status = bindings_collect(handle, table);
    if (status != A_STATUS_OK) goto fail;
    for (size_t i = 0; i < table->sig_count; i++) {
        if (handle->sigs[i].data != NULL) continue;
#if ABUS_STATIC_ENABLE
#if ABUS_DYNAMIC_ENABLE
        if (!handle->dynamic_storage)
#endif
        {
            status = A_STATUS_NOT_FOUND;
            goto fail;
        }
#endif
        if (table->sigs[i].size > SIZE_MAX - missing) {
            status = A_STATUS_INVALID_PARAM;
            goto fail;
        }
        missing += table->sigs[i].size;
    }
#if ABUS_DYNAMIC_ENABLE
    if (missing != 0U) {
        handle->allocation = aOSAlloc(missing);
        if (handle->allocation == NULL) {
            status = A_STATUS_NO_MEMORY;
            goto fail;
        }
    }
    next = handle->allocation;
    for (size_t i = 0; i < table->sig_count; i++) {
        if (handle->sigs[i].data != NULL) continue;
        handle->sigs[i].data = next;
        next += table->sigs[i].size;
    }
#endif
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    for (size_t i = 0; i < table->sig_count; i++) {
        status = aOSMutexCreate(&handle->sigs[i].mutex);
        if (status != A_STATUS_OK) goto fail;
    }
#endif
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    status = aOSMutexCreate(&handle->mutex);
    if (status != A_STATUS_OK) goto fail;
#endif
    /* 所有可能失败的步骤完成后，才修改绑定的业务数据。 */
    for (size_t i = 0; i < table->sig_count; i++) {
        const aBusSig_t *sig = &table->sigs[i];
        void *data = handle->sigs[i].data;

        if (sig->default_data != NULL) {
            memcpy(data, sig->default_data, sig->size);
        } else {
            memset(data, 0, sig->size);
        }
    }
    handle->table = table;
    return A_STATUS_OK;
fail:
    resources_release(handle, table);
    return status;
}

#if ABUS_STATIC_ENABLE
aStatus_t aBusInitStatic(const aBusTable_t *table, aBusHandle_t *handle)
{
    aStatus_t status;

    assert(handle != NULL);
    if (handle->table != NULL) return A_STATUS_BUSY;
#if ABUS_DYNAMIC_ENABLE
    if (handle->dynamic_storage) return A_STATUS_INVALID_PARAM;
#endif
    status = table_check(table);
    if (status != A_STATUS_OK) return status;
    if (handle->sigs == NULL || handle->capacity < table->sig_count) {
        return A_STATUS_INVALID_PARAM;
    }
    return resources_prepare(handle, table);
}

aStatus_t aBusDeInitStatic(aBusHandle_t *handle)
{
    if (handle == NULL) return A_STATUS_OK;
#if ABUS_DYNAMIC_ENABLE
    if (handle->dynamic_storage) return A_STATUS_INVALID_PARAM;
#endif
    resources_release(handle, handle->table);
    return A_STATUS_OK;
}
#endif

#if ABUS_DYNAMIC_ENABLE
aStatus_t aBusCreate(const aBusTable_t *table, aBusHandle_t **handle_out)
{
    aBusHandle_t *handle;
    aStatus_t status;
    size_t offset = sizeof(aBusHandle_t);
    size_t alignment = _Alignof(aBusSigState_t);
    size_t padding = (alignment - offset % alignment) % alignment;

    assert(handle_out != NULL);
    *handle_out = NULL;
    status = table_check(table);
    if (status != A_STATUS_OK) return status;
    offset += padding;
    if (table->sig_count > (SIZE_MAX - offset) / sizeof(aBusSigState_t)) {
        return A_STATUS_INVALID_PARAM;
    }
    /* 元数据先整体分配，用状态数组收集绑定，无需大型临时索引。 */
    handle = aOSAlloc(offset + table->sig_count * sizeof(aBusSigState_t));
    if (handle == NULL) return A_STATUS_NO_MEMORY;
    {
        const aBusHandle_t initial = {
            .sigs = (aBusSigState_t *)((unsigned char *)handle + offset),
#if ABUS_STATIC_ENABLE
            .dynamic_storage = A_TRUE,
#endif
        };
        *handle = initial;
    }
    status = resources_prepare(handle, table);
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
    resources_release(handle, handle->table);
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

aStatus_t aBusSetByIndex(aBusHandle_t *handle,
                    const aBusSetIndexRequest_t *request)
{
    aBusSigState_t *entry;
    const aBusSig_t *sig;
    size_t index;
    aStatus_t status;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex = NULL;
#endif

    assert(handle != NULL);
    if (handle->table == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->src != NULL && aTimeoutIsValid(request->timeout));
    index = request->sigIndex;
    if (index >= handle->table->sig_count) return A_STATUS_NOT_FOUND;
    entry = &handle->sigs[index];
    sig = &handle->table->sigs[index];
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
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex = NULL;
    aStatus_t status;
#endif

    assert(handle != NULL);
    if (handle->table == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->dst != NULL && aTimeoutIsValid(request->timeout));
    index = request->sigIndex;
    if (index >= handle->table->sig_count) return A_STATUS_NOT_FOUND;
    entry = &handle->sigs[index];
    sig = &handle->table->sigs[index];
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

    assert(handle != NULL);
    if (handle->table == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->src != NULL && aTimeoutIsValid(request->timeout));
    aBusSetIndexRequestStructInit(&indexed);
    indexed.sigIndex = sig_find(handle->table, request->sigKey);
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

    assert(handle != NULL);
    if (handle->table == NULL) return A_STATUS_NOT_READY;
    assert(request != NULL);
    assert(request->dst != NULL && aTimeoutIsValid(request->timeout));
    aBusGetIndexRequestStructInit(&indexed);
    indexed.sigIndex = sig_find(handle->table, request->sigKey);
    if (indexed.sigIndex == SIZE_MAX) return A_STATUS_NOT_FOUND;
    indexed.dst = request->dst;
    indexed.size = request->size;
    indexed.timeout = request->timeout;
    return aBusGetByIndex(handle, &indexed);
}
