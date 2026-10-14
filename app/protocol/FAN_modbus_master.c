#include "aModbus_mapping.h"
#include "app_config.h"
#include "FAN_sig_table.h"

#if AMODBUS_CLIENT_ENABLE

/* 点表由 JSON 生成，端口、实例和任务由 protocol.c 装配。 */
#include "generated/FAN_modbus_master.inc"

/* 产品协议参数；只读对象由本文件持有，注册配置仅保存引用。 */
static const aModbusCommConfig_t comm_config = {
    .role = AMODBUS_ROLE_CLIENT,
    .transport_type = AMODBUS_TRANSPORT_RTU,
    .unit_id = APP_MODBUS_CLIENT_TARGET_ID,
    /* 单次底层读写可能传递整帧；整笔事务覆盖请求和响应的线路耗时。 */
    .byte_timeout = AMODBUS_RTU_TIMEOUT_INIT(
        APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS,
        AMODBUS_RTU_MAX_FRAME_SIZE, 20U),
    .timeout = AMODBUS_RTU_TIMEOUT_INIT(
        APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS,
        2U * AMODBUS_RTU_MAX_FRAME_SIZE, 500U),
};

/* 应用选择协议回调；主站使用初始化返回的采集表执行，无服务步骤。 */
static const aCommProtocolOps_t comm_ops = {
    .check = aModbusCommCheck,
    .init = aModbusCommInit,
    .process = NULL,
    .deinit = aModbusCommDeInit,
};

/* 周期仅定义采集策略；protocol.c 装配 aComm 和通信任务。 */
const aCommConfig_t FAN_modbus_master_config = {
    .protocol = {
        .config = &comm_config,
        .items = client_maps,
        .count = sizeof(client_maps) / sizeof(client_maps[0]),
        .ops = &comm_ops,
    },
    .retry_count = 2U,
    .interval_ms = 1U,
    .request_capacity = 4U,
    .error_delay_ms = 5U,
};
#endif
