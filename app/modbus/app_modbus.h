#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include "aLib.h"

/* 从站地址；主站模式下用作远端目标站号。 */
enum { APP_MODBUS_UNIT_ID = 1U };

/* appSigInit 后调用一次；创建端口和协议实例。 */
aStatus_t appModbusInit(void);
/* 唯一通信任务调用，每次处理一笔请求或进行一次主站采集。 */
aStatus_t appModbusProcess(void);
/* 所有调用停止后销毁；也用于任务创建失败时回收资源。 */
aStatus_t appModbusDeInit(void);

#endif
