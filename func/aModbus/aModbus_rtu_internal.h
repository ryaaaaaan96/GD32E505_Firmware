#ifndef A_MODBUS_RTU_INTERNAL_H
#define A_MODBUS_RTU_INTERNAL_H

#include "aModbus_rtu_instance.h"

/* 供模块内部的组合实例使用，不分配内存，不启动接收。 */
aStatus_t aModbusRtuInstanceInit(const aModbusRtuConfig_t *config,
                               aModbusRtuHandle_t *handle);

#endif
