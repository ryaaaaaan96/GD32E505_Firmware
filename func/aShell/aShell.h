/**
 * @file aShell.h
 * @brief 全局唯一 Shell 的生命周期与处理接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 模块不创建任务；APP 指定一个任务或主循环调用 Process。
 * 除 IsEnabled/ConfigStructInit 外均按任务上下文使用，不在 ISR 调用。
 * Init/DeInit 必须外部串行化；销毁前停止 Process 并退出所有 API 用户。
 * 关闭模块后链接空实现，保留 API 可调用性，而不是继续访问串口。
 */

#ifndef A_SHELL_H
#define A_SHELL_H

#include "aLib.h"

#include <stdint.h>

/**
 * @brief 同步输入回调，由 Process 的应用任务调用。
 * @param[out] buffer 接收区，至少 size 字节。
 * @param[in] size 请求字节数；返回长度必须能用 int16_t 表示。
 * @return 实际长度；0 暂无数据，负数失败。不可返回大于 size 的值。
 * @note 等待预算由应用适配器决定，不在返回后保留 buffer。
 */
typedef int16_t (*aShellRead_t)(char *buffer, uint16_t size);

/**
 * @brief 同步输出回调；不得修改 buffer，返回后不再引用它。
 * @param[in] buffer 待发送字符数组，历史签名没有 const，不代表允许修改。
 * @param[in] size 请求字节数。
 * @return 实际写入长度，失败负数；不得超过 size 或 INT16_MAX。
 */
typedef int16_t (*aShellWrite_t)(char *buffer, uint16_t size);

/** @brief 单例配置；回调在初始化时保存，缓冲区由模块分配。 */
typedef struct {
    aShellRead_t read; /**< 必填输入适配器。 */
    aShellWrite_t write; /**< 必填输出适配器。 */
    uint16_t buffer_size; /**< Shell 编辑/解析区大小，字节，至少 64。 */
} aShellConfig_t;

/**
 * @brief 填充默认 Shell 配置。
 * @param[out] config 配置对象；NULL 不操作。
 * @note 默认回调为空、缓冲区 256 字节；启用模块时必须提供读写回调。
 */
void aShellConfigStructInit(aShellConfig_t *config);

/**
 * @brief 创建单例 Shell 状态及内部缓冲区，不创建任务。
 * @param[in] config 初始化期间读取的配置；回调及其依赖资源必须保持有效至 DeInit。
 * @retval A_STATUS_OK 初始化成功；模块关闭时直接成功且不访问 config。
 * @retval A_STATUS_INVALID_PARAM 启用时配置为空、回调缺失或 buffer_size 小于 64。
 * @retval A_STATUS_BUSY 单例已初始化。
 * @retval A_STATUS_NO_MEMORY 内部状态、缓冲区或锁分配失败。
 */
aStatus_t aShellInit(const aShellConfig_t *config);

/**
 * @brief 从 read 回调获取并处理至多 64 个输入字符。
 * @retval A_STATUS_OK 已处理输入；模块关闭时为空操作。
 * @retval A_STATUS_BUSY 当前没有输入。
 * @retval A_STATUS_NOT_READY 启用但未初始化。
 * @retval A_STATUS_ERROR read 回调返回负数。
 * @note 只允许一个处理者；可能等待 read 的超时，也可能同步执行耗时命令。
 */
aStatus_t aShellProcess(void);

/**
 * @brief 释放单例缓冲区、Shell 状态和递归锁。
 * @retval A_STATUS_OK 已释放；模块关闭时为空操作。
 * @retval A_STATUS_NOT_READY 启用但未初始化。
 * @return 也可能返回 aOS 递归锁错误。
 * @warning 调用方须先停止处理任务和所有并发 API 用户；内部锁不保证并发销毁安全。
 */
aStatus_t aShellDeInit(void);

/**
 * @brief 查询本次构建是否包含实际 Shell 实现。
 * @return 启用为 A_TRUE，空实现为 A_FALSE；不表示已经初始化。
 */
aBool_t aShellIsEnabled(void);

/**
 * @brief 格式化输出到单例 Shell。
 * @param[in] format printf 风格格式字符串；NULL 不操作。
 * @param[in] ... 与格式匹配的可变参数。
 * @note 单次文本最多保留 255 字节，超长截断；返回值不报告写入失败。
 * @warning 可能等待内部锁/输出回调；未初始化或模块关闭时不输出。
 */
void aShellPrint(const char *format, ...);

#endif
