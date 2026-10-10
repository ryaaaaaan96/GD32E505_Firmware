#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include "aLib.h"

/* 统一初始化产品点表、协议端口及相关任务，由系统启动任务调用一次。
 * 调用前 aOS 和协议使用的基础服务须就绪，Shell 消费任务应在成功后启动。
 * 新任务可能立即运行，因此先完成共享点表初始化，再创建任务。
 * 失败停止后续初始化，回收当前未启动任务的协议，保留之前已启动的任务。
 * 成功或失败后再次调用均返回 BUSY；不提供运行时重启或并发初始化。
 */
aStatus_t protocolInit(void);

#endif
