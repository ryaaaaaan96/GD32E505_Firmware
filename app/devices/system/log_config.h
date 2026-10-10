#ifndef APP_LOG_CONFIG_H
#define APP_LOG_CONFIG_H

#include "aLog.h"

/* 填充日志配置；输出直接共享控制台，要求 appSystemConsoleInit 已成功。
 * 不创建资源。回调使用 20 ms 总发送预算，部分提交返回 ERROR，不重试。
 * 日志锁由 aLog 管理，串口 TX 锁由控制台管理；此处不增加互斥对象。 */
void appSystemLogConfigInit(aLogConfig_t *config);

#endif
