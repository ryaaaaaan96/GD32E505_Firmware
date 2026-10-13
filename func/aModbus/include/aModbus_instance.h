/** @brief 静态实例布局；初次使用须零初始化，运行后禁止复制或移动。 */
#ifndef A_MODBUS_INSTANCE_H
#define A_MODBUS_INSTANCE_H

#include "aModbus_client.h"
#include "aModbus_server.h"
#include "nanomodbus.h"
#include "aAtomic.h"
#include "aComm_endpoint.h"

struct aModbusHandle {
    aCommEndpoint_t comm; /**< 公共首成员，运行端点随协议实例一起生存。 */
    nmbs_t core;
    /* 主从配置共用存储；config 指向当前角色的 modbus 成员。 */
    union {
        aModbusClientConfig_t client;
        aModbusServerConfig_t server;
    };
    aModbusConfig_t *config;
    aModbusRole_t role;
    aTimepoint_t deadline;
    aStatus_t io_status;
    uint8_t exception;
    aBool_t ready;
    aBool_t dynamic;
    aBool_t fault;
    aBool_t recover_input;
    aAtomicBool_t active;
};

#endif
