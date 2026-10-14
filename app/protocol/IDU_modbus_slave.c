#include "aModbus_mapping.h"
#include "aOS.h"
#include "app_config.h"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "protocol_generated.h"

#include <string.h>

#if AMODBUS_SERVER_ENABLE

/* 描述 IDU 对外提供的地址空间和访问权限，可同时映射本机及 FAN 数据。
 * 本文件定义映射、示例地址段回调和通信策略；端口、实例及任务由 protocol.c 装配。
 */

/* 地址段回调示例 --------------------------------------------------------- */

typedef struct {
    uint16_t address;
    size_t count;
    uint16_t *values;
} demoRegisters_t;

/* 仅协议任务访问的演示 RAM，不占用 aBus 测点；重启恢复以下初始值。 */
static uint16_t test_registers[] = {10U, 20U, 30U, 40U};
static uint16_t board_registers[] = {0x4944U, 0x5500U}; /* ASCII：IDU */
_Static_assert(sizeof(test_registers) / sizeof(test_registers[0]) ==
    IDU_MODBUS_SLAVE_TEST_BLOCK_QUANTITY, "Update test callback storage");
_Static_assert(sizeof(board_registers) / sizeof(board_registers[0]) ==
    IDU_MODBUS_SLAVE_BOARD_BLOCK_QUANTITY, "Update board callback storage");
_Static_assert(IDU_MODBUS_SLAVE_UPTIME_QUANTITY == 2U,
    "Uptime callback requires two registers");
static demoRegisters_t test_block = {
    IDU_MODBUS_SLAVE_TEST_BLOCK_ADDRESS,
    IDU_MODBUS_SLAVE_TEST_BLOCK_QUANTITY, test_registers};
static demoRegisters_t board_block = {
    IDU_MODBUS_SLAVE_BOARD_BLOCK_ADDRESS,
    IDU_MODBUS_SLAVE_BOARD_BLOCK_QUANTITY, board_registers};

static aStatus_t registersRead(void *context,
    const aModbusAddressReadRequest_t *request)
{
    const demoRegisters_t *block = context;
    size_t offset = (size_t)request->address - block->address;

    if (offset >= block->count || request->quantity > block->count - offset) {
        return A_STATUS_NOT_FOUND;
    }
    /* data 是本机 uint16_t 数组，线上的大端编码由 aModbus 完成。 */
    memcpy(request->data, block->values + offset,
           request->quantity * sizeof(uint16_t));
    return A_STATUS_OK;
}

static aStatus_t registersWrite(void *context,
    const aModbusAddressWriteRequest_t *request)
{
    demoRegisters_t *block = context;
    const uint16_t *values = request->data;
    size_t offset = (size_t)request->address - block->address;

    if (offset >= block->count || request->quantity > block->count - offset) {
        return A_STATUS_NOT_FOUND;
    }
    /* 示例限制每个寄存器为 0..10000；整段校验通过后才修改数据。 */
    for (size_t i = 0U; i < request->quantity; i++) {
        if (values[i] > 10000U) return A_STATUS_INVALID_PARAM;
    }
    memcpy(block->values + offset, values,
           request->quantity * sizeof(uint16_t));
    return A_STATUS_OK;
}

static aStatus_t uptimeRead(void *context,
    const aModbusAddressReadRequest_t *request)
{
    uint32_t milliseconds = aOSGetUptimeMs();
    uint16_t registers[] = {
        (uint16_t)(milliseconds >> 16U), (uint16_t)milliseconds
    };
    size_t offset = (size_t)request->address -
                    IDU_MODBUS_SLAVE_UPTIME_ADDRESS;
    (void)context;

    if (offset >= IDU_MODBUS_SLAVE_UPTIME_QUANTITY ||
        request->quantity > IDU_MODBUS_SLAVE_UPTIME_QUANTITY - offset) {
        return A_STATUS_NOT_FOUND;
    }
    /* 一次回调取一个快照，也允许只读高字或低字。 */
    memcpy(request->data, registers + offset,
           request->quantity * sizeof(uint16_t));
    return A_STATUS_OK;
}

/* 地址与权限由 JSON 生成，业务回调仍在本文件实现。 */
#include "generated/IDU_modbus_slave.inc"

/* 产品协议参数；只读对象由本文件持有，注册配置仅保存引用。 */
static const aModbusCommConfig_t comm_config = {
    .role = AMODBUS_ROLE_SERVER,
    .transport_type = AMODBUS_TRANSPORT_RTU,
    .unit_id = APP_MODBUS_SERVER_UNIT_ID,
    /* 接收/响应预算按一对最大帧估算，500 ms 同时作为等待主站的余量。 */
    .byte_timeout = AMODBUS_RTU_TIMEOUT_INIT(
        APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS,
        AMODBUS_RTU_MAX_FRAME_SIZE, 20U),
    .timeout = AMODBUS_RTU_TIMEOUT_INIT(
        APP_RS485_BAUD_RATE, APP_RS485_CHARACTER_BITS,
        2U * AMODBUS_RTU_MAX_FRAME_SIZE, 500U),
    .ranges = ranges,
    .range_count = sizeof(ranges) / sizeof(ranges[0]),
};

/* 应用选择协议回调；从站由 process 处理接收请求。 */
static const aCommProtocolOps_t comm_ops = {
    .check = aModbusCommCheck,
    .init = aModbusCommInit,
    .process = aModbusCommProcess,
    .deinit = aModbusCommDeInit,
};

const aCommConfig_t IDU_modbus_slave_config = {
    .protocol = {
        .config = &comm_config,
        .items = maps,
        .count = sizeof(maps) / sizeof(maps[0]),
        .ops = &comm_ops,
    },
    .error_delay_ms = 5U,
};
#endif
