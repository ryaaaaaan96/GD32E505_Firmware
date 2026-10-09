#ifndef APP_DATA_BUS_MODBUS_H
#define APP_DATA_BUS_MODBUS_H

#include "aModbus.h"

/* 应用内的协议装配入口，自动借用系统数据总线私有实例。
 * config->bus 不使用；数据总线须持续有效至协议销毁。 */
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t dataBusModbusCreate(const aModbusConfig_t *config,
                                   aModbusHandle_t **handle_out);
#endif
#if AMODBUS_STATIC_ENABLE
aStatus_t dataBusModbusInitStatic(const aModbusConfig_t *config,
                                       aModbusHandle_t *handle);
#endif

#endif
