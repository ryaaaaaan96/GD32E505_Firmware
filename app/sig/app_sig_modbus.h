#ifndef APP_SIG_MODBUS_H
#define APP_SIG_MODBUS_H

#include "aModbus.h"

/* 应用内的协议装配入口，自动借用私有 SIG 实例。
 * config->bus 不使用；成功后协议实例由调用者管理，SIG 须持续有效。 */
#if AMODBUS_DYNAMIC_ENABLE
aStatus_t appSigModbusCreate(const aModbusConfig_t *config,
                             aModbusHandle_t **handle_out);
#endif
#if AMODBUS_STATIC_ENABLE
aStatus_t appSigModbusInitStatic(const aModbusConfig_t *config,
                                 aModbusHandle_t *handle);
#endif

#endif
