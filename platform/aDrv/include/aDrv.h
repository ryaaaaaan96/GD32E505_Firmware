/**
 * @file aDrv.h
 * @brief 驱动层公共初始化及硬件中断回调类型。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * aDrv 不依赖 aOS，不负责业务线程、等待预算或业务回调调度。
 * 硬件中断回调在 ISR 执行，只处理短小的硬件状态维护，不可阻塞。
 */

#ifndef ADRV_H
#define ADRV_H

#include "aLib.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 硬件 ISR 回调，由设备层注册，不是应用工作任务回调。
 * @param[in] argument 注册时传入的借用对象，保持有效至注销及在途回调退出。
 * @warning 必须短小、不阻塞；若调用 aOS，IRQ 优先级必须合法。
 */
typedef void (*aDrvInterruptCallback_t)(void *argument);

/**
 * @brief 初始化驱动公共环境。
 * @return A_STATUS_OK；具体动作由芯片 port 决定。
 * @note 启动阶段在外设初始化前调用；不初始化所有外设，也不启动调度器。
 * GD32 使用 4 位抢占优先级、0 位子优先级；运行期间不得更改分组。
 */
aStatus_t aDrvInit(void);

#endif
