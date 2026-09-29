/**
 * @file aScalar.h
 * @brief 通用整数类型标签及对应值，不包含业务规则或同步语义。
 */
#ifndef ASCALAR_H
#define ASCALAR_H

#include <stddef.h>
#include <stdint.h>

/** @brief 标签必须与 aScalar_t 中被初始化和访问的成员一致。 */
typedef enum {
    ALIB_SCALAR_U8,  /**< aScalar_t.u8 */
    ALIB_SCALAR_U16, /**< aScalar_t.u16 */
    ALIB_SCALAR_U32, /**< aScalar_t.u32 */
    ALIB_SCALAR_I32  /**< aScalar_t.i32 */
} aScalarType_t;

/** @brief 仅存放一个值；标签由外层对象保存。
 * 使用指定成员初始化，例如 {.u32 = UINT32_MAX}。
 * 不支持自动类型转换；不能从运行时检测调用方是否填错成员。
 * 原始布局不是协议或持久化格式，不提供原子访问保证。
 */
typedef union {
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int32_t i32;
} aScalar_t;

/** @brief 返回标量的字节数；未知类型返回 0，不是联合体的大小。 */
static inline size_t aScalarSize(aScalarType_t type)
{
    switch (type) {
    case ALIB_SCALAR_U8:
        return sizeof(uint8_t);
    case ALIB_SCALAR_U16:
        return sizeof(uint16_t);
    case ALIB_SCALAR_U32:
        return sizeof(uint32_t);
    case ALIB_SCALAR_I32:
        return sizeof(int32_t);
    default:
        return 0U;
    }
}

#endif
