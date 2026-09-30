/**
 * @file aDataType.h
 * @brief 通用数据类型标签、数值及 RAW 数据引用，不包含业务规则或同步语义。
 */
#ifndef ADATA_TYPE_H
#define ADATA_TYPE_H

#include <stddef.h>
#include <stdint.h>

/** @brief 标签必须与 aDataValue_t 中被初始化和访问的成员一致。 */
typedef enum {
    ALIB_DATA_U8,  /**< aDataValue_t.u8 */
    ALIB_DATA_U16, /**< aDataValue_t.u16 */
    ALIB_DATA_U32, /**< aDataValue_t.u32 */
    ALIB_DATA_S32, /**< aDataValue_t.s32 */
    ALIB_DATA_RAW  /**< aDataValue_t.raw；长度由外层定义提供。 */
} aDataType_t;

/** @brief 仅存放一个值；标签由外层对象保存。
 * 使用指定成员初始化，例如 {.u32 = UINT32_MAX}。
 * 不支持自动类型转换；不能从运行时检测调用方是否填错成员。
 * 原始布局不是协议或持久化格式，不提供原子访问保证。
 */
typedef union {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int32_t s32;
    void *raw_mut; /**< 可写 RAW 存储；由运行时对象管理生命周期。 */
    const void *raw; /**< 借用的原始数据地址，不是待复制的指针值。 */
} aDataValue_t;

/** @brief 返回定长数值类型的字节数；RAW/未知类型返回 0。
 * RAW 的长度由调用方显式提供，不能使用 sizeof(void *) 代替。 */
static inline size_t aDataTypeSize(aDataType_t type)
{
    switch (type) {
    case ALIB_DATA_U8:
        return sizeof(uint8_t);
    case ALIB_DATA_U16:
        return sizeof(uint16_t);
    case ALIB_DATA_U32:
        return sizeof(uint32_t);
    case ALIB_DATA_S32:
        return sizeof(int32_t);
    default:
        return 0U;
    }
}

#endif
