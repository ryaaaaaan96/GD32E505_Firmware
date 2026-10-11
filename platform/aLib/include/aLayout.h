/**
 * @file aLayout.h
 * @brief 由调用方选择使用的编译期大小、偏移检查。
 * 需要 C11 或 C++11；Release 同样生效，不依赖 NDEBUG。
 * 不修改类型布局，不产生运行时代码，也不保证字节序或并发访问安全。
 */
#ifndef A_LAYOUT_H
#define A_LAYOUT_H

#include <stddef.h>

/** @brief condition_ 必须为常量表达式，message_ 必须为字符串字面量。 */
#if defined(__cplusplus)
#define ALIB_STATIC_ASSERT(condition_, message_) \
    static_assert((condition_), message_)
#else
#define ALIB_STATIC_ASSERT(condition_, message_) \
    _Static_assert((condition_), message_)
#endif

/**
 * @brief 检查完整类型的总字节数，bytes_ 须为整数常量表达式。
 * 可传入所有成员 sizeof 的和；调用方负责列全成员且不重复。
 * 普通 C 的 const 描述数组元素不能作为编译期常量。
 * 仅检查总大小，不检查成员偏移、重叠或长度是否为某单元的整数倍。
 * 放在类型定义之后使用，调用末尾加分号；不支持变长数组类型。
 */
#define ALIB_ASSERT_TYPE_SIZE(type_, bytes_) \
    ALIB_STATIC_ASSERT(sizeof(type_) == (bytes_), \
                       #type_ ": unexpected type size")

/**
 * @brief 检查成员的预期字节偏移，offset_ 须为整数常量表达式。
 * 适用于普通结构体，C++ 类型须为标准布局；不支持位域。
 * 应传入固定预期偏移，避免用同一成员的 offsetof 与自身比较。
 * 仅检查指定成员，不自动检查整个结构体的连续性或尾部填充。
 */
#define ALIB_ASSERT_MEMBER_OFFSET(type_, member_, offset_) \
    ALIB_STATIC_ASSERT(offsetof(type_, member_) == (offset_), \
                       #type_ "." #member_ ": unexpected member offset")

#endif
