/** @file aBus_instance.h
 * @brief 仅实例创建层使用；初始化后对象不得复制、移动或直接修改字段。
 * 应用存储须保持有效直到停止所有访问并完成 DeInitStatic。
 */
#ifndef ABUS_INSTANCE_H
#define ABUS_INSTANCE_H

#include "aBus.h"
#if ABUS_LOCK_MODE != ABUS_LOCK_NONE
#include "aOS.h"
#endif

typedef struct {
    void *data; /**< 完整快照，只通过 Set/Get 访问。 */
#if ABUS_LOCK_MODE == ABUS_LOCK_SIG
    aOSMutex_t mutex;
#endif
} aBusSigState_t;

struct aBusHandle {
    const aBusTable_t *tables; /**< 非 NULL 表示整个实例已就绪。 */
    size_t table_count;
    size_t sig_count; /**< 全部表的 SIG 总数。 */
    aBusSigState_t *sigs;
#if ABUS_STATIC_ENABLE
    size_t capacity; /**< 全部表的静态条目数组容量，仅初始化时使用。 */
#endif
#if ABUS_DYNAMIC_ENABLE
    void *allocation; /**< 未绑定 SIG 共用的数据块；静态实例为 NULL。 */
#endif
#if ABUS_LOCK_MODE == ABUS_LOCK_BUS
    aOSMutex_t mutex;
#endif
#if ABUS_STATIC_ENABLE && ABUS_DYNAMIC_ENABLE
    aBool_t dynamic_storage; /**< 区分静态借用与动态所有权。 */
#endif
};

#if ABUS_STATIC_ENABLE
/** @brief 初始化未运行实例；capacity 是 SIG 状态数组容量。
 * 数据地址由 ABUS_STORAGE_EXPORT 收集，不需要应用提供总数据池。
 */
static inline void aBusInstanceStructInit(aBusHandle_t *handle,
                                         aBusSigState_t *sigs,
                                         size_t capacity)
{
    if (handle != NULL) {
        const aBusHandle_t defaults = {
            .sigs = sigs,
            .capacity = capacity
        };
        *handle = defaults;
    }
}

#endif /* ABUS_STATIC_ENABLE */

#endif
