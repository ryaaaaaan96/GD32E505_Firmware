#ifndef PROTOCOL_INTERNAL_H
#define PROTOCOL_INTERNAL_H

#include "aModbus.h"
#include "aPoll.h"

/* 产品组合配置；协议核心与调度核心互不依赖此类型。 */
typedef struct {
    aModbusServiceConfig_t service;
    aPollJobConfig_t schedule;
#if !AMODBUS_DYNAMIC_ENABLE
    aModbusPlan_t *plan_instance;
#endif
} protocolModbusConfig_t;

#endif
