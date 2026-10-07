/** @brief 静态实例存储；全部字段私有，初始化后禁止复制或移动。 */
#ifndef A_MODBUS_INSTANCE_H
#define A_MODBUS_INSTANCE_H

#include "aModbus.h"
#include "nanomodbus.h"
#include <stdatomic.h>

struct aModbusHandle {
    nmbs_t core;
    aModbusConfig_t config;
    aTimepoint_t deadline;
    aStatus_t io_status;
    uint8_t exception;
    aBool_t ready;
    aBool_t dynamic;
    aBool_t fault;
    aBool_t recover_input;
    atomic_bool active;
};

#endif
