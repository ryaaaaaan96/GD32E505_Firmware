#ifndef APP_MODBUS_PORT_H
#define APP_MODBUS_PORT_H

#include "aModbus.h"

/* 本 Demo 私有的单链路适配，只有 Modbus 任务读写该串口。 */
aStatus_t appModbusPortInit(aModbusTransport_t *transport);
aStatus_t appModbusPortDeInit(void);
/* 每笔协议操作结束后丢弃帧缓存中的剩余字节，不能跨帧补读。 */
void appModbusPortFrameReset(void);

#endif
