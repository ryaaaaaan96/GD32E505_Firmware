#ifndef APP_TEST_TASK_H
#define APP_TEST_TASK_H

#include "aStatus.h"

/* 应用测试任务的统一入口，由 appInitTask 在 aSystemInit 成功后调用一次。
 * 当前为每秒递增的 TEST_001 静态绑定测试，后续测试任务在本模块集中管理。
 * 测试项按自身依赖裁剪；无测试项时返回 OK，不创建任务。
 * 创建失败返回 aOS 错误，不支持重复初始化或运行期重启。
 */
aStatus_t appTestTaskInit(void);

#endif
