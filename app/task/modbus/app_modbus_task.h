#ifndef APP_MODBUS_TASK_H
#define APP_MODBUS_TASK_H

#include "aLib.h"

/* appModbusInit 成功后调用一次；失败时回收尚未运行的协议和端口。 */
aStatus_t appModbusTaskInit(void);

#endif
