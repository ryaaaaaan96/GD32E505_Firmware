/**
 * @file shell_cfg_user.h
 * @brief 第三方 Shell 的项目适配配置。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 锁开启；时基来自 aOS，关闭启动信息和登录清屏。不在此处选择 USART 设备。
 */

#ifndef SHELL_CFG_USER_H
#define SHELL_CFG_USER_H

#include "aOS.h"

#define SHELL_USING_LOCK 1
#define SHELL_GET_TICK() aOSGetUptimeMs()
#define SHELL_SHOW_INFO 0
#define SHELL_CLS_WHEN_LOGIN 0

#endif
