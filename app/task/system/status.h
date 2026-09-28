/**
 * @file status.h
 * @brief 应用状态指示任务的启动接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 应用层负责设备初始化和任务创建；设备层不拥有闪烁任务。
 */

#ifndef APP_SYSTEM_STATUS_H
#define APP_SYSTEM_STATUS_H

#include "aStatus.h"

/**
 * @brief 初始化状态 LED 并创建 status 任务。
 * @return 成功 A_STATUS_OK；失败返回设备初始化或任务创建错误。
 * @warning 启动阶段只调用一次；本函数没有幂等任务创建保护。
 */
aStatus_t statusInit(void);

#endif
