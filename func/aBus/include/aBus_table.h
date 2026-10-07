/** @file aBus_table.h
 * @brief 只读数据定义；不依赖运行实例、锁、内存分配或链接段收集。
 * 协议及持久化模块可以独立借用，定义和引用数据须保持有效且不变。
 */
#ifndef ABUS_TABLE_H
#define ABUS_TABLE_H

#include "aLib.h"
#include "aDataType.h"

/** @brief 只控制读写时使用锁；不影响创建，NONE 模式忽略。 */
#define ABUS_SIG_FLAG_LOCK (1U << 0)

/** @brief 单个 SIG 标志位的静态名称；组合位应逐位查询。 */
static inline const char *aBusSigFlagName(uint16_t flag)
{
    switch (flag) {
    case 0U: return "NONE";
    case ABUS_SIG_FLAG_LOCK: return "LOCK";
    default: return "UNKNOWN";
    }
}

/** @brief 可选整数范围；联合体成员由所属 SIG/Param 的 type 决定。 */
typedef struct {
    aDataValue_t min;
    aDataValue_t max;
} aBusRange_t;

/** @brief STRUCT 字段描述；允许未登记字段，不支持嵌套 STRUCT。 */
typedef struct {
    size_t offset; /**< 相对 SIG 起点，建议使用 offsetof。 */
    size_t size; /**< 必须大于零；标量须与类型大小一致。 */
    aDataType_t type;
    const aBusRange_t *range; /**< NULL 不限范围；RAW 必须为 NULL。 */
} aBusParam_t;

/** @brief 只读 SIG 定义；每组当前值是一段完整的 RAM 快照。 */
typedef struct {
    uint16_t sigKey; /**< 同表唯一且跨版本稳定，不要求连续。 */
    uint16_t flags;
    aDataType_t type; /**< 整组类型；可解析结构体用 STRUCT，字节块用 RAW。 */
    size_t size; /**< 完整快照字节数，必须大于零。 */
    const void *default_data; /**< size 字节默认值；NULL 表示清零。 */
    const aBusRange_t *range; /**< 标量可选；RAW/STRUCT 必须为 NULL。 */
    const aBusParam_t *params; /**< 仅 STRUCT 使用；可为 NULL。 */
    size_t param_count;
} aBusSig_t;

/** @brief 只读表定义；表及引用的定义在实例使用期保持有效且不变。 */
typedef struct {
    const aBusSig_t *sigs; /**< 连续的 Flash 定义表。 */
    size_t sig_count;
    uint16_t deviceID; /**< 绑定所属逻辑表标识；应用保证唯一性。 */
} aBusTable_t;

#define ABUS_TABLE_DEFAULT { NULL, 0U, 0U }

static inline void aBusTableStructInit(aBusTable_t *table)
{
    if (table != NULL) {
        const aBusTable_t defaults = ABUS_TABLE_DEFAULT;
        *table = defaults;
    }
}

#endif
