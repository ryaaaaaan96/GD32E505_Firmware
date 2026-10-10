#include "aModbus.h"
#include "app_config.h"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"

#if !APP_MODBUS_MASTER_ENABLE
#if !AMODBUS_SERVER_ENABLE
#error "Modbus slave demo requires AMODBUS_SERVER_ENABLE"
#endif

/* 先展开映射组，各组各有一份只读数组。 */
#define AMODBUS_RANGE(name, ...)
#define AMODBUS_MAP(name, ...) {__VA_ARGS__},
#define AMODBUS_MAPS(group, ...) \
    static const aModbusBusMap_t group##_maps[] = {__VA_ARGS__};
#include "mapping/IDU_modbus_slave.inc"
#undef AMODBUS_MAPS
#undef AMODBUS_MAP
#undef AMODBUS_RANGE

/* 再生成地址段。映射数量由组推导，地址及跨度仍由产品显式指定。 */
#define AMODBUS_MAPS(group, ...)
#define AMODBUS_MAP_REF(group) \
    .maps = group##_maps, \
    .map_count = sizeof(group##_maps) / sizeof(group##_maps[0])
#define AMODBUS_RANGE(name, ...) {__VA_ARGS__},
static const aModbusAddressRange_t ranges[] = {
#include "mapping/IDU_modbus_slave.inc"
};
#undef AMODBUS_RANGE
#undef AMODBUS_MAP_REF
#undef AMODBUS_MAPS

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
