#ifndef APP_MODBUS_TASK_H
#define APP_MODBUS_TASK_H

#include "aLib.h"

/* 对应主站或从站初始化成功后调用；失败时回收其协议和串口实例。 */
aStatus_t appModbusTaskInit(void);

#endif
