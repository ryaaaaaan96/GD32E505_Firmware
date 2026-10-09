#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include "aLib.h"

#if defined(APP_MODBUS_ENABLE) && APP_MODBUS_ENABLE
#include "aModbus.h"

/* 产品只读配置；类型由 aModbus 提供，实例由 protocol.c 装配。 */
extern const aModbusServiceConfig_t FAN_modbus_master_config;
extern const aModbusServiceConfig_t IDU_modbus_slave_config;
#endif

/* IDU 与 FAN 点表共用的 aBus 实例命名空间，仅用于静态 RAM 绑定。 */
enum {
    PROTOCOL_BUS_INSTANCE_ID = 1U
};

/* 统一初始化产品点表、协议端口及相关任务，由系统启动任务调用一次。
 * 调用前 aOS 和协议使用的基础服务须就绪，Shell 消费任务应在成功后启动。
 * 新任务可能立即运行，因此先完成共享点表初始化，再创建任务。
 * 失败停止后续初始化，回收当前未启动任务的协议，保留之前已启动的任务。
 * 成功或失败后再次调用均返回 BUSY；不提供运行时重启或并发初始化。
 */
aStatus_t protocolInit(void);

#endif
