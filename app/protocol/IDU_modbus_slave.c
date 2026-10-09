#include "aModbus.h"
#include "protocol.h"
#include "app_config.h"
#include "IDU_sig_ids.h"
#include "FAN_sig_ids.h"

#if !APP_MODBUS_MASTER_ENABLE
#if !AMODBUS_SERVER_ENABLE
#error "Modbus slave demo requires AMODBUS_SERVER_ENABLE"
#endif

/* 地址是从零开始的保持寄存器偏移；各 32 位值均为高字在前。 */
static const aModbusBusMap_t maps[] = {
    {
        .address = 0U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_COUNTER, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .address = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, 0U},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .address = 4U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, 1U},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
};
static const aModbusAddressRange_t ranges[] = {
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U,
        .quantity = 6U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .maps = maps,
        .map_count = sizeof(maps) / sizeof(maps[0]),
    },
};

const aModbusServiceConfig_t IDU_modbus_slave_config = {
    .modbus = {
        .role = AMODBUS_ROLE_SERVER,
        .transport_type = AMODBUS_TRANSPORT_RTU,
        .unit_id = APP_MODBUS_SLAVE_UNIT_ID,
        .ranges = ranges,
        .range_count = sizeof(ranges) / sizeof(ranges[0]),
        .byte_timeout = {
            .milliseconds = 20U, .type = A_TIMEOUT_TYPE_RELATIVE
        },
    },
    .server = {
        .timeout = {.milliseconds = 500U, .type = A_TIMEOUT_TYPE_RELATIVE},
    },
    .task = {
        .name = "modbus",
        .stack_bytes = 4096U,
        .priority = AOS_TASK_PRIO_NORMAL,
    },
    .interval_ms = 0U,
    .error_delay_ms = 5U,
};
#endif
