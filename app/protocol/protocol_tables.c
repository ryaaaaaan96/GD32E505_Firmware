#include "protocol.h"
#include "app_config.h"
#if ABUS_ENABLE
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "aBus.h"
#include <stddef.h>
#if !ABUS_DYNAMIC_ENABLE
#include "aBus_instance.h"
#endif

/* 产品数据定义与存储计划：不创建总线、数据库或通信任务。 */

/* 先生成命名字段组，再展开 SIG；两者与头文件中的索引来自同一清单。
 * 默认值、规则和字段数组均有静态存储期，生命周期覆盖实例。
 */
#define ABUS_BOUND_SIG(...) ABUS_SIG(__VA_ARGS__)
#define ABUS_SIG(name, key_value, ...)
#define ABUS_PARAM(name, ...) [name] = {__VA_ARGS__},
#define ABUS_PARAMS(group, ...) \
    static const aBusParam_t group##_params[] = {__VA_ARGS__};
#include "sig/IDU_test.inc"
#include "sig/FAN_test.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

#define ABUS_PARAMS(group, ...)
#define ABUS_PARAM_REF(group) \
    .params = group##_params, \
    .param_count = sizeof(group##_params) / sizeof(group##_params[0])
#define ABUS_SIG(name, key_value, value_type, ...) \
    {.sigKey = (key_value), .size = sizeof(value_type), __VA_ARGS__},

static const aBusSig_t IDU_sigs[] = {
#include "sig/IDU_test.inc"
};
static const aBusSig_t FAN_sigs[] = {
#include "sig/FAN_test.inc"
};
#undef ABUS_SIG
#undef ABUS_PARAM_REF
#undef ABUS_PARAMS
#undef ABUS_BOUND_SIG

/* 直接借用 Flash 中的连续表描述，不在初始化时复制到 RAM。 */
static const aBusTable_t tables[] = {
    {
        .sigs = IDU_sigs,
        .sig_count = IDU_SIG_COUNT,
        .deviceID = IDU_SIG_DEVICE_ID
    },
    {
        .sigs = FAN_sigs,
        .sig_count = FAN_SIG_COUNT,
        .deviceID = FAN_SIG_DEVICE_ID
    }
};

/* FAN TEST_001 由协议持有；IDU TEST_001 绑定任务自己的变量。 */
static FANTest_t test_storage;
ABUS_RAM_BIND_EXPORT(test_binding,
                     PROTOCOL_BUS_INSTANCE_ID,
                     FAN_SIG_DEVICE_ID,
                     FAN_SIG_TEST_001,
                     test_storage);
/* 纯静态配置为新增点提供存储；动态配置沿用 aBus 一次分配兜底。
 * 两组 TEST_001 的显式绑定始终保留，用于验证混合存储。
 */
#if !ABUS_DYNAMIC_ENABLE
/* 已有绑定的项不再分配；其余项按 C 类型生成静态存储。 */
#define ABUS_BOUND_SIG(...)
#define ABUS_PARAMS(group, ...)
#define ABUS_SIG(name, key_value, value_type, ...) \
    static value_type IDU_##name##_storage; \
    ABUS_RAM_BIND_EXPORT(IDU_##name##_binding, PROTOCOL_BUS_INSTANCE_ID, \
        IDU_SIG_DEVICE_ID, IDU_SIG_##name, IDU_##name##_storage);
#include "sig/IDU_test.inc"
#undef ABUS_SIG
#define ABUS_SIG(name, key_value, value_type, ...) \
    static value_type FAN_##name##_storage; \
    ABUS_RAM_BIND_EXPORT(FAN_##name##_binding, PROTOCOL_BUS_INSTANCE_ID, \
        FAN_SIG_DEVICE_ID, FAN_SIG_##name, FAN_##name##_storage);
#include "sig/FAN_test.inc"
#undef ABUS_SIG
#undef ABUS_PARAMS
#undef ABUS_BOUND_SIG

/* 容量随产品点表变化，静态存储留在这里；初始化和句柄生命周期由 system 管理。 */
static aBusSigState_t bus_states[IDU_SIG_COUNT + FAN_SIG_COUNT];
#if ABUS_EVENT_ENABLE
/* 从同一字段清单计算状态容量，静态模式不依赖堆。 */
enum { BUS_PARAM_COUNT = 0
#define ABUS_BOUND_SIG(...)
#define ABUS_SIG(...)
#define ABUS_PARAM(name, ...) + 1
#define ABUS_PARAMS(group, ...) __VA_ARGS__
#include "sig/IDU_test.inc"
#include "sig/FAN_test.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG
#undef ABUS_BOUND_SIG
};
static aBusParamState_t bus_params[BUS_PARAM_COUNT];
#endif
static aBusHandle_t bus_instance = {
#if ABUS_EVENT_ENABLE
    .params = bus_params,
    .param_capacity = BUS_PARAM_COUNT,
#endif
    .sigs = bus_states,
    .capacity = IDU_SIG_COUNT + FAN_SIG_COUNT
};
#endif

aStatus_t protocolBusConfigInit(dataBusConfig_t *config)
{
    if (config == NULL) return A_STATUS_INVALID_PARAM;

    /* 产品主动检查组合测点的长度总和，不改变 aBus 的通用初始化规则。
     * Modbus 还会独立验证连续性、偶数字段长度和显式 quantity。
     */
    for (size_t t = 0U; t < sizeof(tables) / sizeof(tables[0]); t++) {
        for (size_t s = 0U; s < tables[t].sig_count; s++) {
            const aBusSig_t *sig = &tables[t].sigs[s];
            aStatus_t status;
            if (sig->type != ALIB_DATA_STRUCT) continue;
            status = aBusSigParamSizeCheck(sig);
            if (status != A_STATUS_OK) return status;
        }
    }

    config->instanceID = PROTOCOL_BUS_INSTANCE_ID;
    config->tables = tables;
    config->table_count = sizeof(tables) / sizeof(tables[0]);
#if !ABUS_DYNAMIC_ENABLE
    config->instance = &bus_instance;
#endif
    return A_STATUS_OK;
}

#if APP_DATABASE_ENABLE

#define PERSIST_SIG(device, key) {(device), (key)},
static const aDataBaseSigKey_t persistent_sigs[] = {
#include "persistence/sig_persist.inc"
};
#undef PERSIST_SIG

_Static_assert(sizeof(persistent_sigs) / sizeof(persistent_sigs[0]) ==
               PROTOCOL_PERSIST_SIG_COUNT, "Persistence count mismatch");

void protocolPersistenceConfigInit(aDataBaseKvConfig_t *config)
{
    if (config == NULL) return;
    config->tables = tables;
    config->table_count = sizeof(tables) / sizeof(tables[0]);
    config->persist_sigs = persistent_sigs;
    config->persist_count =
        sizeof(persistent_sigs) / sizeof(persistent_sigs[0]);
}
#endif
#endif
