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

#include "protocol_generated.h"
#include "generated/sig_tables.inc"

/* FAN TEST_001 由协议持有；演示时 IDU TEST_001 绑定测试任务变量。 */
static FANTest_t test_storage;
ABUS_RAM_BIND_EXPORT(test_binding,
                     PROTOCOL_BUS_INSTANCE_ID,
                     FAN_SIG_DEVICE_ID,
                     FAN_SIG_TEST_001,
                     test_storage);
#if !APP_DEMO_ENABLE
/* 产品配置不链接测试任务，原绑定改由点表提供，静态模式也无需堆兜底。 */
static uint32_t idu_storage;
ABUS_RAM_BIND_EXPORT(idu_binding, PROTOCOL_BUS_INSTANCE_ID,
                     IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001, idu_storage);
#endif
#if !ABUS_DYNAMIC_ENABLE
/* 容量由同一 JSON 生成，增加测点无需维护第二份计数。 */
static aBusSigState_t bus_states[PROTOCOL_SIG_COUNT];
#if ABUS_EVENT_ENABLE && PROTOCOL_PARAM_COUNT > 0
static aBusParamState_t bus_params[PROTOCOL_PARAM_COUNT];
#endif
static aBusHandle_t bus_instance = {
#if ABUS_EVENT_ENABLE && PROTOCOL_PARAM_COUNT > 0
    .params = bus_params,
    .param_capacity = PROTOCOL_PARAM_COUNT,
#endif
    .sigs = bus_states,
    .capacity = PROTOCOL_SIG_COUNT
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

#if PROTOCOL_PERSIST_SIG_COUNT > 0
#define PERSIST_SIG(device, key) {(device), (key)},
static const aDataBaseSigKey_t persistent_sigs[] = {
#include "persistence/sig_persist.inc"
};
#undef PERSIST_SIG

_Static_assert(sizeof(persistent_sigs) / sizeof(persistent_sigs[0]) ==
               PROTOCOL_PERSIST_SIG_COUNT, "Persistence count mismatch");
#endif

void protocolPersistenceConfigInit(aDataBaseKvConfig_t *config)
{
    if (config == NULL) return;
    config->tables = tables;
    config->table_count = sizeof(tables) / sizeof(tables[0]);
    config->persist_count = PROTOCOL_PERSIST_SIG_COUNT;
#if PROTOCOL_PERSIST_SIG_COUNT > 0
    config->persist_sigs = persistent_sigs;
#else
    config->persist_sigs = NULL;
#endif
}
#endif
#endif
