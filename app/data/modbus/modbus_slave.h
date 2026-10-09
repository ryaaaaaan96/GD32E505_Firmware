#ifndef APP_MODBUS_SLAVE_H
#define APP_MODBUS_SLAVE_H

#include "app_config.h"
#include "aLib.h"

#if !APP_MODBUS_MASTER_ENABLE
/* sigDataInit 后调用一次；创建从站串口和协议实例，句柄由本模块私有持有。 */
aStatus_t modbusSlaveInit(void);
/* 唯一通信任务调用，每次处理一笔从站请求。 */
aStatus_t modbusSlaveProcess(void);
/* 所有调用停止后销毁；发送未完成时可稍后重试，也用于任务创建失败回收。 */
aStatus_t modbusSlaveDeInit(void);
#endif

#endif
