#include "aBus.h"
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
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

/* 单例借用应用定义和数据，只拥有内部锁资源；生命周期由应用串行化。 */
static const aBus_SigDef *definitions;
static size_t definition_count;
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
static aOSMutex_t bus_mutex;
#endif

static aBool_t value_in_range(const aBus_ItemDef *item, aScalar_t value)
{
    switch (item->type) {
    case ALIB_SCALAR_U8:
        return value.u8 >= item->min.u8 && value.u8 <= item->max.u8;
    case ALIB_SCALAR_U16:
        return value.u16 >= item->min.u16 && value.u16 <= item->max.u16;
    case ALIB_SCALAR_U32:
        return value.u32 >= item->min.u32 && value.u32 <= item->max.u32;
    case ALIB_SCALAR_I32:
        return value.i32 >= item->min.i32 && value.i32 <= item->max.i32;
    default: return A_FALSE;
    }
}

#if ABUS_DEF_CHECK_ENABLE
/* 初始化时检查静态规则及存储重叠，运行时不重复扫描这些定义。 */
static aStatus_t definitions_check(const aBusConfig_t *config)
{
    /* 逐组检查：数据大小、状态指针、组标志及字段规则表是否合法。 */
    for (size_t i = 0U; i < config->signal_count; ++i) {
        const aBus_SigDef *sig = &config->signals[i];
        uintptr_t start;

        if (sig->size == 0U || sig->state == NULL ||
            sig->state->data == NULL ||
            (sig->flags & ~ABUS_SIG_FLAG_LOCK) != 0U ||
            (sig->itemCount != 0U && sig->items == NULL)) {
            return A_STATUS_INVALID_PARAM;
        }
        /* 检查数据区末尾地址是否会溢出，供后续区间重叠判断使用。 */
        start = (uintptr_t)sig->state->data;
        if (start > UINTPTR_MAX - sig->size) {
            return A_STATUS_INVALID_PARAM;
        }
        /* 与之前已检查的组逐一比较数据区重叠，不检查 sigID 唯一性。 */
        for (size_t j = 0U; j < i; ++j) {
            const aBus_SigDef *other = &config->signals[j];
            uintptr_t other_start = (uintptr_t)other->state->data;

            if (start < other_start + other->size &&
                other_start < start + sig->size) {
                return A_STATUS_INVALID_PARAM;
            }
        }
        /* 逐项检查当前组：类型和标志合法，字段不越界，默认值在范围内。
         * 默认值满足上下限也同时保证 min <= max。 */
        for (size_t j = 0U; j < sig->itemCount; ++j) {
            const aBus_ItemDef *item = &sig->items[j];
            size_t size = aScalarSize(item->type);

            if (size == 0U || item->flags != 0U ||
                item->offset > sig->size ||
                size > (size_t)sig->size - item->offset ||
                !value_in_range(item, item->default_value)) {
                return A_STATUS_INVALID_PARAM;
            }
            /* 与本组之前的字段逐一比较：禁止重复或部分重叠，
             * 避免初始化默认值时相互覆盖。 */
            for (size_t k = 0U; k < j; ++k) {
                const aBus_ItemDef *other = &sig->items[k];
                size_t other_size = aScalarSize(other->type);

                if (item->offset < other->offset + other_size &&
                    other->offset < item->offset + size) {
                    return A_STATUS_INVALID_PARAM;
                }
            }
        }
    }
    return A_STATUS_OK;
}

#endif

/* 调用前须停止所有读写者；这里只释放锁，不释放应用的数据区。 */
void aBusDeInit(void)
{
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    aOSMutexDestroy(&bus_mutex);
#elif ABUS_LOCK_MODE == ABUS_LOCK_SIG
    for (size_t i = 0U; i < definition_count; ++i) {
        aOSMutexDestroy(&definitions[i].state->mutex);
    }
#endif
    definitions = NULL;
    definition_count = 0U;
}

aStatus_t aBusInit(const aBusConfig_t *config)
{
#if ABUS_DEF_CHECK_ENABLE || ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aStatus_t status;
#endif

    if (definitions != NULL) return A_STATUS_BUSY;
    assert(config != NULL);
    assert(config->signals != NULL);
    assert(config->signal_count != 0U);
    assert(config->signal_count <= (size_t)UINT16_MAX + 1U);
#if ABUS_DEF_CHECK_ENABLE
    status = definitions_check(config);
    if (status != A_STATUS_OK) return status;
#endif
    /* 编译时选择锁存储及创建路径，不按组标志跳过创建。 */
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    status = aOSMutexCreate(&bus_mutex);
    if (status != A_STATUS_OK) return status;
#elif ABUS_LOCK_MODE == ABUS_LOCK_SIG
    for (size_t i = 0U; i < config->signal_count; ++i) {
        status = aOSMutexCreate(&config->signals[i].state->mutex);
        if (status != A_STATUS_OK) {
            /* 仅回收本次已创建的锁，不改动失败条目的已有资源。 */
            for (size_t j = 0U; j < i; ++j) {
                aOSMutexDestroy(&config->signals[j].state->mutex);
            }
            return status;
        }
    }
#endif
    /* 检查和锁创建全部成功后才写默认值，确保前面的失败不改动数据。 */
    for (size_t i = 0U; i < config->signal_count; ++i) {
        const aBus_SigDef *sig = &config->signals[i];

        for (size_t j = 0U; j < sig->itemCount; ++j) {
            const aBus_ItemDef *item = &sig->items[j];
            unsigned char *field = sig->state->data;

            /* 只复制 type 对应成员的字节，未登记字段保持应用初始内容。 */
            const aScalar_t *value = &item->default_value;

            switch (item->type) {
            case ALIB_SCALAR_U8:
                memcpy(field + item->offset, &value->u8, sizeof(value->u8));
                break;
            case ALIB_SCALAR_U16:
                memcpy(field + item->offset, &value->u16, sizeof(value->u16));
                break;
            case ALIB_SCALAR_U32:
                memcpy(field + item->offset, &value->u32, sizeof(value->u32));
                break;
            case ALIB_SCALAR_I32:
                memcpy(field + item->offset, &value->i32, sizeof(value->i32));
                break;
            default:
                /* 关闭定义检查时，类型合法性由应用保证。 */
                break;
            }
        }
    }
    definitions = config->signals;
    definition_count = config->signal_count;
    return A_STATUS_OK;
}

/* 用 memcpy 读取字段，避免 offset 未对齐时直接解引用类型指针。 */
static aStatus_t values_check(const aBus_SigDef *sig, const void *src)
{
    for (size_t i = 0U; i < sig->itemCount; ++i) {
        const aBus_ItemDef *item = &sig->items[i];
        const unsigned char *field = (const unsigned char *)src + item->offset;
        aScalar_t value = {.u32 = 0U};

        switch (item->type) {
        case ALIB_SCALAR_U8:
            memcpy(&value.u8, field, sizeof(value.u8));
            break;
        case ALIB_SCALAR_U16:
            memcpy(&value.u16, field, sizeof(value.u16));
            break;
        case ALIB_SCALAR_U32:
            memcpy(&value.u32, field, sizeof(value.u32));
            break;
        case ALIB_SCALAR_I32:
            memcpy(&value.i32, field, sizeof(value.i32));
            break;
        default: return A_STATUS_INVALID_PARAM;
        }
        if (!value_in_range(item, value)) {
            return A_STATUS_INVALID_PARAM;
        }
    }
    return A_STATUS_OK;
}

/* sigID 不要求连续；未找到时返回 definition_count。 */
static size_t signal_find(uint16_t sigID)
{
    for (size_t i = 0U; i < definition_count; ++i) {
        if (definitions[i].sigID == sigID) return i;
    }
    return definition_count;
}

#ifndef NDEBUG
/* 仅供断言检查；NDEBUG 构建不保留此函数及调用。 */
static aBool_t buffer_is_valid(const aBus_SigDef *sig, const void *buffer,
                               uint16_t size)
{
    uintptr_t buffer_start = (uintptr_t)buffer;
    uintptr_t data_start = (uintptr_t)sig->state->data;

    return size == sig->size && buffer_start <= UINTPTR_MAX - size &&
           data_start <= UINTPTR_MAX - size &&
           !(buffer_start < data_start + size &&
             data_start < buffer_start + size);
}
#endif

aStatus_t aBusSetSig(uint16_t sigID, const void *src, uint16_t size,
                    aTimeout_t timeout)
{
    const aBus_SigDef *sig;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex = NULL;
#endif
    aStatus_t status;
    size_t index;

#if ABUS_LOCK_MODE == ABUS_LOCK_NONE
    (void)timeout;
#endif
    if (definitions == NULL) return A_STATUS_NOT_READY;
    assert(src != NULL);
    assert(aTimeoutIsValid(timeout));
    index = signal_find(sigID);
    if (index == definition_count) return A_STATUS_NOT_FOUND;
    sig = &definitions[index];
    assert(buffer_is_valid(sig, src, size));
    /* 输入由调用方保持稳定，范围校验在锁外完成以缩短持锁时间。 */
    status = values_check(sig, src);
    if (status != A_STATUS_OK) return status;

#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if ((sig->flags & ABUS_SIG_FLAG_LOCK) != 0U) {
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
        mutex = bus_mutex;
#else
        mutex = sig->state->mutex;
#endif
        status = aOSMutexLock(mutex, timeout);
        if (status != A_STATUS_OK) return status;
    }
#endif
    /* 完整替换；无锁组须由应用保证串行访问，不自动合并字段。 */
    memcpy(sig->state->data, src, size);
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if (mutex != NULL) return aOSMutexUnlock(mutex);
#endif
    return A_STATUS_OK;
}

aStatus_t aBusGetSig(uint16_t sigID, void *dst, uint16_t size,
                    aTimeout_t timeout)
{
    const aBus_SigDef *sig;
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aOSMutex_t mutex = NULL;
#endif
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    aStatus_t status;
#endif
    size_t index;

#if ABUS_LOCK_MODE == ABUS_LOCK_NONE
    (void)timeout;
#endif
    if (definitions == NULL) return A_STATUS_NOT_READY;
    assert(dst != NULL);
    assert(aTimeoutIsValid(timeout));
    index = signal_find(sigID);
    if (index == definition_count) return A_STATUS_NOT_FOUND;
    sig = &definitions[index];
    assert(buffer_is_valid(sig, dst, size));

#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if ((sig->flags & ABUS_SIG_FLAG_LOCK) != 0U) {
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
        mutex = bus_mutex;
#else
        mutex = sig->state->mutex;
#endif
        status = aOSMutexLock(mutex, timeout);
        if (status != A_STATUS_OK) return status;
    }
#endif
    /* 读写使用同一把锁，保证整组快照；不向调用方暴露内部指针。 */
    memcpy(dst, sig->state->data, size);
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
    if (mutex != NULL) return aOSMutexUnlock(mutex);
#endif
    return A_STATUS_OK;
}
