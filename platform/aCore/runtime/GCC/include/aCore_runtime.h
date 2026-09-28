/**
 * @file aCore_runtime.h
 * @brief GCC/newlib 标准输入输出的弱符号适配入口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 默认弱实现返回 -1 并设置 C 运行库 errno=ENOSYS；应用可提供同名强实现。
 * 此处使用 newlib errno，不是 aOSGetErrno()；重定向实现负责转换错误。
 * 上下文、阻塞和并发行为由应用适配器决定，不保证 ISR 安全。
 */

#ifndef ACORE_RUNTIME_H
#define ACORE_RUNTIME_H

#include "aLib.h"

#include <stddef.h>

/**
 * @brief 为 newlib 标准输入提供实际读取操作。
 * @param[out] buffer 至少 size 字节的可写区域。
 * @param[in] size 本次请求的最大字节数。
 * @return 0..size 为实际读取长度；失败返回 -1 并设置 C errno。
 * @note 默认不访问 buffer，直接 ENOSYS；上层 _read 处理零长度。
 */
aSSize_t aCoreRuntimeRead(void *buffer, size_t size);

/**
 * @brief 为 newlib 标准输出/标准错误提供实际写入操作。
 * @param[in] data 至少 size 字节的源区域。
 * @param[in] size 本次请求的最大字节数。
 * @return 0..size 为实际写入长度；失败返回 -1 并设置 C errno。
 * @note 覆盖实现返回后不得继续引用调用方缓冲区。
 */
aSSize_t aCoreRuntimeWrite(const void *data, size_t size);

#endif
