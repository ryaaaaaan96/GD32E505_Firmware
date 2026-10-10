/**
 * @file system_init.h
 * @brief 应用基础服务的统一启动入口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 由 main 在驱动和 aOS 初始化后调用；业务任务仍归 APP 所有。
 */

#ifndef APP_SYSTEM_INIT_H
#define APP_SYSTEM_INIT_H

#include "aStatus.h"

/**
 * @brief 按顺序启动 status、可选 Shell、SIG 数据与自增测试任务。
 * @return 全部成功为 A_STATUS_OK，否则返回初始化错误；
 * 若控制台清理也失败，优先返回清理错误。
 * @warning 启动阶段单线程调用一次；失败不回滚已经创建的 status 任务。
 * 失败进入启动停止流程，不支持重试；已创建的 Shell 任务保持等待，不处理命令。
 * @note Shell 关闭时不创建控制台任务；Shell 启用时其串口初始化属于 Shell 启动流程。
 */
aStatus_t aSystemInit(void);

#endif
