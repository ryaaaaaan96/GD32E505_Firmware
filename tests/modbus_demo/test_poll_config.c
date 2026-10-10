/* 使用不同请求对象验证轮转；映射相同便于复用真实线路回归。 */
#include "aModbus.h"
#include "app_config.h"
#include "FAN_sig_table.h"

#if APP_MODBUS_MASTER_ENABLE
#if !AMODBUS_CLIENT_ENABLE
#error "Modbus master demo requires AMODBUS_CLIENT_ENABLE"
#endif

/* 采集目标属于产品配置；协议库负责远端读取、解码及发布到 aBus。 */
static const aModbusClientSigRequest_t polls[] = {{
    .unit_id = APP_MODBUS_MASTER_TARGET_ID,
    .area = AMODBUS_AREA_HOLDING_REGISTERS,
    .address = 2U,
    .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, 0U},
    .word_order = AMODBUS_WORD_HIGH_FIRST,
    .timeout = {.milliseconds = 500U, .type = A_TIMEOUT_TYPE_RELATIVE},
}, {
    .unit_id = APP_MODBUS_MASTER_TARGET_ID,
    .area = AMODBUS_AREA_HOLDING_REGISTERS,
    .address = 2U,
    .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, 0U},
    .word_order = AMODBUS_WORD_HIGH_FIRST,
    .timeout = {.milliseconds = 501U, .type = A_TIMEOUT_TYPE_RELATIVE},
}};

/* 每处理一个采集项后等待 1 s；通信时间另计。 */
const aModbusServiceConfig_t FAN_modbus_master_config = {
    .modbus = {
        .role = AMODBUS_ROLE_CLIENT,
        .transport_type = AMODBUS_TRANSPORT_RTU,
        .unit_id = APP_MODBUS_MASTER_TARGET_ID,
        .byte_timeout = {
            .milliseconds = 20U, .type = A_TIMEOUT_TYPE_RELATIVE
        },
    },
    .polls = polls,
    .poll_count = sizeof(polls) / sizeof(polls[0]),
    .task = {
        .name = "modbus",
        .stack_bytes = 4096U,
        .priority = AOS_TASK_PRIO_NORMAL,
    },
    .interval_ms = 1000U,
    .error_delay_ms = 1000U,
};
#endif
