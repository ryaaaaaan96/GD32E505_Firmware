/** @brief 静态实例存储；全部字段私有，初始化后禁止复制或移动。 */
#ifndef A_MODBUS_INSTANCE_H
#define A_MODBUS_INSTANCE_H

#include "aModbus.h"
#include "nanomodbus.h"
#include <stdatomic.h>

struct aModbusHandle {
    nmbs_t core;
    size_t plan_count; /**< 存在采集计划时禁止销毁协议实例。 */
    aModbusConfig_t config;
    aTimepoint_t deadline;
    aStatus_t io_status;
    uint8_t exception;
    aBool_t ready;
    aBool_t dynamic;
    aBool_t fault;
    aBool_t recover_input;
    atomic_bool active;
    uint8_t snapshot[AMODBUS_MAX_VALUE_SIZE];
};

/* 静态计划帧数组可按采集项数提供上限，实际帧数初始化后查询。 */
typedef struct {
    size_t first;
    size_t count;
    uint16_t quantity;
    uint8_t rtu[8];
} aModbusPlanFrame_t;

struct aModbusPlan {
    aModbusPlanFrame_t *frames;
    size_t capacity;
    aModbusPlanConfig_t config;
    aModbusHandle_t *owner;
    size_t count;
    aBool_t ready;
    aBool_t dynamic;
};

#endif
