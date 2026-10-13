#include "aModbus_mapping.h"
#include "aOS.h"
#include "app_config.h"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"

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
static demoRegisters_t test_block = {512U, 4U, test_registers};
static demoRegisters_t board_block = {520U, 2U, board_registers};

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
    size_t offset = (size_t)request->address - 512U;
    (void)context;

    if (offset >= 2U || request->quantity > 2U - offset) {
        return A_STATUS_NOT_FOUND;
    }
    /* 一次回调取一个快照，也允许只读高字或低字。 */
    memcpy(request->data, registers + offset,
           request->quantity * sizeof(uint16_t));
    return A_STATUS_OK;
}

/* 回调段按区域、地址排序，与下面的离散映射互不重叠。 */
static const aModbusMappingAddressRange_t ranges[] = {
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 512U, .quantity = 4U,
        .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
        .read = registersRead, .write = registersWrite,
        .context = &test_block,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 520U, .quantity = 2U,
        .flags = ACOMM_MAP_FLAG_READ,
        .read = registersRead,
        .context = &board_block,
    },
    {
        .area = AMODBUS_AREA_INPUT_REGISTERS,
        .address = 512U, .quantity = 2U,
        .flags = ACOMM_MAP_FLAG_READ,
        .read = uptimeRead,
    },
};

/* 离散测点映射 ----------------------------------------------------------- */

/* 离散映射按区域、地址排序；quantity 显式声明寄存器数。
 * 初始化检查其与 aBus 目标匹配，地址空洞无需额外定义。
 */
static const aModbusServerMap_t maps[] = {
    /* 基础测试值及 FAN 采集副本，地址 0..5。 */
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_001,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
            .subscribe_events = ACOMM_SUBSCRIBE_MARK_ONLY,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 0U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_001,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 2U,
        .quantity = 4U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },

    /* 本地测试控制值，地址 32..47，不执行真实 FAN 控制。 */
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_002,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 32U,
        .quantity = 1U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_003,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 34U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_004,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 36U,
        .quantity = 1U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_005,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 38U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_006,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 40U,
        .quantity = 4U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_007,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 44U,
        .quantity = 4U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },

    /* FAN TEST_008..050，地址 64..149。 */
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_008,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 64U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_009,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 66U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_010,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 68U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_011,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 70U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_012,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 72U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_013,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 74U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_014,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 76U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_015,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 78U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_016,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 80U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_017,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 82U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_018,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 84U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_019,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 86U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_020,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 88U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_021,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 90U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_022,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 92U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_023,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 94U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_024,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 96U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_025,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 98U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_026,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 100U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_027,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 102U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_028,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 104U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_029,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 106U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_030,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 108U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_031,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 110U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_032,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 112U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_033,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 114U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_034,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 116U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_035,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 118U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_036,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 120U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_037,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 122U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_038,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 124U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_039,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 126U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_040,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 128U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_041,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 130U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_042,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 132U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_043,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 134U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_044,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 136U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_045,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 138U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_046,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 140U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_047,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 142U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_048,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 144U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_049,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 146U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_050,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 148U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },

    /* IDU 混合类型，地址 256..272。 */
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_002,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 256U,
        .quantity = 1U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_003,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 257U,
        .quantity = 1U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_004,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 258U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_005,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 260U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_006,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 262U,
        .quantity = 4U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_007,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 266U,
        .quantity = 8U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },

    /* IDU TEST_008..050，地址 288..373。 */
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_008,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 288U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_009,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 290U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_010,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 292U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_011,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 294U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_012,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 296U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_013,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 298U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_014,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 300U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_015,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 302U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_016,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 304U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_017,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 306U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_018,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 308U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_019,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 310U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_020,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 312U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_021,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 314U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_022,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 316U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_023,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 318U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_024,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 320U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_025,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 322U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_026,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 324U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_027,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 326U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_028,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 328U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_029,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 330U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_030,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 332U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_031,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 334U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_032,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 336U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_033,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 338U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_034,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 340U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_035,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 342U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_036,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 344U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_037,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 346U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_038,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 348U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_039,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 350U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_040,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 352U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_041,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 354U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_042,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 356U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_043,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 358U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_044,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 360U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_045,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 362U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_046,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 364U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_047,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 366U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_048,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 368U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_049,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 370U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .comm = {
            .flags = ACOMM_MAP_FLAG_READ | ACOMM_MAP_FLAG_WRITE,
            .deviceID = IDU_SIG_DEVICE_ID,
            .sigIndex = IDU_SIG_TEST_050,
            .paramIndex = ACOMM_MAP_SIG_WHOLE,
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 372U,
        .quantity = 2U,
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
};

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
