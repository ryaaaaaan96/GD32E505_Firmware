/**
 * @file aLib.h
 * @brief 通用布尔类型、流式结果、errno 映射与纯时间运算。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 本层不读取系统时钟，不保存线程错误状态，也不等待或分配内存。
 * 时间单位为毫秒；调用方提供同一单调 uint32_t 时基。差值按无符号回绕计算，
 * 不能跨越完整的 2^32 毫秒周期而不检查截止时间。
 */

#ifndef A_LIB_H
#define A_LIB_H

#include "aStatus.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 编译器属性适配；非 GNU/Clang 构建下为空定义。 */
#if defined(__GNUC__) || defined(__clang__)
#define ALIB_USED __attribute__((used))
#define ALIB_WEAK __attribute__((weak))
#define ALIB_SECTION(name_) __attribute__((section(name_), used))
#define ALIB_NORETURN __attribute__((noreturn))
#else
#define ALIB_USED
#define ALIB_WEAK
#define ALIB_SECTION(name_)
#define ALIB_NORETURN
#endif

/**
 * @brief 流式 I/O 结果：非负为实际字节数，-1 为失败。
 * 仅失败后读取 aOS errno；成功不清除历史错误。各 API 单独说明部分进度/超时语义。
 * 非字节流设备不要求提供 Read/Write，不应机械套用该类型。
 */
typedef ptrdiff_t aSSize_t;

/**
 * @brief 项目统一逻辑类型，通过 A_TRUE/A_FALSE 表达真假。
 * 不承诺存储大小；不得直接用于协议、持久化、硬件寄存器或外部 ABI 布局。
 */
typedef bool aBool_t;

#define A_FALSE ((aBool_t)false)
#define A_TRUE  ((aBool_t)true)

/** @brief 项目流式 I/O 错误码，不保证与 POSIX errno 数值相同。 */
typedef enum {
    A_ERRNO_NONE = 0,
    A_EINVAL,
    A_EAGAIN,
    A_ETIMEDOUT,
    A_EIO,
    A_ENODEV,
    A_ENOTSUP,
    A_EINTR,
    A_ENOMEM,
    A_ECANCELED,
} aErrno_t;

/**
 * @brief 把统一状态映射为项目 errno，不修改任何线程状态。
 * @param[in] status 待转换状态。
 * @return 对应 aErrno_t；未知状态映射 A_EIO，OK 映射 A_ERRNO_NONE。
 * @note 任务错误槽由 aOSSetErrno() 管理，不是 C 运行库 errno。
 */
static inline aErrno_t aStatusToErrno(aStatus_t status)
{
    switch (status) {
    case A_STATUS_OK:
        return A_ERRNO_NONE;
    case A_STATUS_INVALID_PARAM:
        return A_EINVAL;
    case A_STATUS_BUSY:
        return A_EAGAIN;
    case A_STATUS_TIMEOUT:
        return A_ETIMEDOUT;
    case A_STATUS_NOT_FOUND:
    case A_STATUS_NOT_READY:
        return A_ENODEV;
    case A_STATUS_UNSUPPORTED:
        return A_ENOTSUP;
    case A_STATUS_NO_MEMORY:
        return A_ENOMEM;
    case A_STATUS_CANCELLED:
        return A_ECANCELED;
    case A_STATUS_ERROR:
    default:
        return A_EIO;
    }
}

/** @brief 有限相对超时或无限等待；不存在隐式绝对日期时间。 */
typedef enum {
    A_TIMEOUT_TYPE_RELATIVE = 0,
    A_TIMEOUT_TYPE_FOREVER,
} aTimeoutType_t;

/** @brief 等待预算；通过 A_TIMEOUT_* 宏构造。 */
typedef struct {
    uint32_t milliseconds; /**< RELATIVE 时的毫秒数，0 表示不等待。 */
    aTimeoutType_t type; /**< FOREVER 时忽略 milliseconds。 */
} aTimeout_t;

/** @brief 一次操作共享的截止状态，由 aTimepointCalc() 构造。 */
typedef struct {
    uint32_t start_ms; /**< 同一单调时基上的起点。 */
    uint32_t duration_ms; /**< 有限预算的毫秒数。 */
    aBool_t forever; /**< A_TRUE 表示不检查有限预算。 */
} aTimepoint_t;

/** @brief 只尝试当前状态，不等待未来进展。 */
#define A_TIMEOUT_NO_WAIT \
    ((aTimeout_t){.milliseconds = 0U, .type = A_TIMEOUT_TYPE_RELATIVE})

/** @brief 构造有限毫秒预算；调用方保证值能由 uint32_t 表示。 */
#define A_TIMEOUT_MS(value_) \
    ((aTimeout_t){ \
        .milliseconds = (uint32_t)(value_), \
        .type = A_TIMEOUT_TYPE_RELATIVE, \
    })

/** @brief 无限等待业务条件，不表示忽略硬件错误。 */
#define A_TIMEOUT_FOREVER \
    ((aTimeout_t){.milliseconds = 0U, .type = A_TIMEOUT_TYPE_FOREVER})

/**
 * @brief 检查超时类型是否合法。
 * @param[in] timeout 超时值。
 * @return RELATIVE 或 FOREVER 返回 A_TRUE；不额外校验 milliseconds。
 */
static inline aBool_t aTimeoutIsValid(aTimeout_t timeout)
{
    return (timeout.type == A_TIMEOUT_TYPE_RELATIVE) ||
           (timeout.type == A_TIMEOUT_TYPE_FOREVER);
}

/**
 * @brief 从相对超时构造一次操作的截止状态。
 * @param[in] timeout 已通过 aTimeoutIsValid() 校验的超时。
 * @param[in] now_ms 操作起点的单调毫秒计数。
 * @return 时间点；后续各等待阶段复用它，不能反复重置总预算。
 */
static inline aTimepoint_t aTimepointCalc(aTimeout_t timeout,
                                          uint32_t now_ms)
{
    const aTimepoint_t timepoint = {
        .start_ms = now_ms,
        .duration_ms = timeout.milliseconds,
        .forever = timeout.type == A_TIMEOUT_TYPE_FOREVER,
    };

    return timepoint;
}

/**
 * @brief 根据传入时钟检查是否耗尽预算。
 * @param[in] timepoint 截止状态；允许为 NULL。
 * @param[in] now_ms 当前单调毫秒计数。
 * @return 有限预算耗尽返回 A_TRUE；NULL 或 FOREVER 返回 A_FALSE。
 */
static inline aBool_t aTimepointExpired(const aTimepoint_t *timepoint,
                                        uint32_t now_ms)
{
    return (timepoint != NULL) && !timepoint->forever &&
           ((uint32_t)(now_ms - timepoint->start_ms) >=
            timepoint->duration_ms);
}

/**
 * @brief 计算剩余预算，不获取系统时间。
 * @param[in] timepoint 截止状态；NULL 视为无限等待。
 * @param[in] now_ms 当前单调毫秒计数。
 * @return 剩余毫秒；已到期为 NO_WAIT，无限等待为 FOREVER。
 */
static inline aTimeout_t aTimepointRemaining(const aTimepoint_t *timepoint,
                                              uint32_t now_ms)
{
    uint32_t elapsed_ms;

    if ((timepoint == NULL) || timepoint->forever) {
        return A_TIMEOUT_FOREVER;
    }

    elapsed_ms = (uint32_t)(now_ms - timepoint->start_ms);
    if (elapsed_ms >= timepoint->duration_ms) {
        return A_TIMEOUT_NO_WAIT;
    }

    return A_TIMEOUT_MS(timepoint->duration_ms - elapsed_ms);
}

#endif
