#include "protocol_internal.h"
#if !AMODBUS_DYNAMIC_ENABLE
#include "aModbus_instance.h"
#endif
#include "app_config.h"
#include "FAN_sig_table.h"

#if AMODBUS_CLIENT_ENABLE

/* 描述 IDU 作为主站访问 FAN 的业务，不表示 FAN 自己运行主站。
 * 本文件只定义采集项和通信策略；端口、实例及任务由 protocol.c 装配。
 */
/* 采集 TEST_001～030 共 30 个 SIG，组合类型整组配置，共 30 项。
 * Demo 远端地址沿用 IDU 从站对 FAN 副本的映射，便于双板主从联调；
 * 接真实 FAN 时应按其寄存器手册调整，不能从本地 sigIndex 推导地址。
 * quantity 显式填写远端寄存器数，aModbus 校验其与 aBus 目标匹配。
 * 数组按地址递增；aModbus 自动合并连续项，不跨空洞，断帧由 flags 控制。
 * TEST_031～050 不参与采集；一帧失败后继续下一组，下轮再尝试。
 */
static const aModbusPoll_t polls[] = {
    /* TEST_001：整组读取两个字段，地址 2～5。 */
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 2U,
        .quantity = 4U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_001,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    /* TEST_002：地址 33 未定义，单独读取地址 32。 */
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 32U,
        .quantity = 1U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_002,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    /* TEST_003～004：合并读取地址 34～36，不跨地址 37。 */
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 34U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_003,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 36U,
        .quantity = 1U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_004,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    /* TEST_005～007：S32、RAW、组合 SIG 合并读取地址 38～47。 */
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 38U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_005,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 40U,
        .quantity = 4U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_006,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 44U,
        .quantity = 4U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_007,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    /* TEST_008～030：连续 U32；TEST_020 要求断帧，分为两次读取。 */
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 64U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_008,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 66U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_009,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 68U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_010,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 70U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_011,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 72U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_012,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 74U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_013,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 76U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_014,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 78U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_015,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 80U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_016,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 82U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_017,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 84U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_018,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 86U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_019,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 88U,
        .quantity = 2U,
        /* 从 TEST_020 另起一帧；flags 改为 0U 即可恢复连续合并。 */
        .flags = AMODBUS_POLL_FLAG_BREAK_BEFORE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_020,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 90U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_021,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 92U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_022,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 94U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_023,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 96U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_024,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 98U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_025,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 100U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_026,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 102U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_027,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 104U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_028,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 106U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_029,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
    {
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 108U,
        .quantity = 2U,
        .flags = 0U,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_TEST_030,
                   AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST,
    },
};

/* 整轮最小周期 1 s，帧间短暂让出 CPU；每帧超时 500 ms。
 * 断帧后共六帧；全部超时时，一轮约 3 s 加帧间等待，不承诺 1 s 内完成。
 */
#if !AMODBUS_DYNAMIC_ENABLE
/* 按采集项数预留上限，新增配置不需手工维护静态容量。 */
static aModbusPlanFrame_t frames[sizeof(polls) / sizeof(polls[0])];
static aModbusPlan_t plan_instance = {
    .frames = frames, .capacity = sizeof(frames) / sizeof(frames[0])
};
#endif

const protocolModbusConfig_t FAN_modbus_master_config = {
    .service = {
        .modbus = {
            .role = AMODBUS_ROLE_CLIENT,
            .unit_id = APP_MODBUS_MASTER_TARGET_ID,
            .transport_type = AMODBUS_TRANSPORT_RTU,
            .byte_timeout = {
                .milliseconds = 20U, .type = A_TIMEOUT_TYPE_RELATIVE
            },
        },
        .request_timeout = {
            .milliseconds = 500U, .type = A_TIMEOUT_TYPE_RELATIVE
        },
        .polls = polls,
        .poll_count = sizeof(polls) / sizeof(polls[0]),
    },
    .schedule = {
        .interval_ms = 1U,
        .error_delay_ms = 5U,
        .poll_time_ms = 1000U,
    },
#if !AMODBUS_DYNAMIC_ENABLE
    .plan_instance = &plan_instance,
#endif
};
#endif
