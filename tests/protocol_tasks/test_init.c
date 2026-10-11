#include "protocol.h"
#include "app_config.h"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "aBus.h"
#if !ABUS_DYNAMIC_ENABLE
#include "aBus_instance.h"
#endif
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#if ABUS_ENABLE
static const aBusTable_t *borrowed_tables;

/* 仅向清单头部插入一项，也必须同步改变索引、描述及静态容量。 */
#ifdef TEST_ADDED_SIG
_Static_assert(IDU_SIG_SPARE == 0 && IDU_SIG_TEST_001 == 1,
               "SIG indices must follow list order");
_Static_assert(IDU_SIG_SPARE_KEY == 900U && IDU_SIG_TEST_001_KEY == 2001U,
               "SIG keys must not follow list order");
_Static_assert(IDU_SIG_COUNT == 51, "SIG count must follow list size");
#endif

static void definitions_check(void)
{
    const aBusSig_t *counter =
        &borrowed_tables[0].sigs[IDU_SIG_TEST_001];
    const aBusSig_t *motor = &borrowed_tables[1].sigs[FAN_SIG_TEST_001];
    const FANTest_t *defaults = motor->default_data;

    assert(borrowed_tables[0].deviceID == IDU_SIG_DEVICE_ID);
    assert(borrowed_tables[1].deviceID == FAN_SIG_DEVICE_ID);
    assert(borrowed_tables[0].sig_count == IDU_SIG_COUNT);
    assert(borrowed_tables[1].sig_count == FAN_SIG_COUNT);
    assert(counter->sigKey == 2001U && counter->type == ALIB_DATA_U32);
    assert(counter->size == sizeof(uint32_t));
    assert(counter->flags == ABUS_SIG_FLAG_LOCK);
    assert(counter->default_data == NULL);
    assert(motor->sigKey == 2001U && motor->type == ALIB_DATA_STRUCT);
    assert(motor->size == sizeof(FANTest_t));
    assert(defaults != NULL && defaults->u32_value == 100U);
    assert(defaults->s32_value == 25);
    assert(motor->param_count == FAN_TEST_001_PARAM_COUNT);
    assert(motor->params[FAN_TEST_001_U32].offset ==
           offsetof(FANTest_t, u32_value));
    assert(motor->params[FAN_TEST_001_U32].range->min.u32 == 0U);
    assert(motor->params[FAN_TEST_001_U32].range->max.u32 == 6000U);
    assert(motor->params[FAN_TEST_001_S32].offset ==
           offsetof(FANTest_t, s32_value));
    assert(motor->params[FAN_TEST_001_S32].type == ALIB_DATA_S32);
    assert(motor->params[FAN_TEST_001_S32].range == NULL);
#ifdef TEST_ADDED_SIG
    {
        const aBusSig_t *spare = &borrowed_tables[0].sigs[IDU_SIG_SPARE];

        assert(spare->sigKey == IDU_SIG_SPARE_KEY);
        assert(spare->type == ALIB_DATA_U16);
        assert(spare->size == sizeof(uint16_t));
        assert(*(const uint16_t *)spare->default_data == 7U);
    }
#endif
}

#endif

int main(void)
{
#if ABUS_ENABLE
    dataBusConfig_t config;

    assert(protocolBusConfigInit(NULL) == A_STATUS_INVALID_PARAM);
    assert(protocolBusConfigInit(&config) == A_STATUS_OK);
    assert(config.instanceID == PROTOCOL_BUS_INSTANCE_ID);
    assert(config.table_count == 2U);
#if !ABUS_DYNAMIC_ENABLE
    assert(config.instance != NULL);
    assert(config.instance->capacity == IDU_SIG_COUNT + FAN_SIG_COUNT);
    assert(config.instance->sigs != NULL);
    assert(config.instance->tables == NULL);
#endif
    borrowed_tables = config.tables;
    definitions_check();
#if APP_DATABASE_ENABLE
    {
        aDataBaseKvConfig_t persistence = {.name = "test-database"};

        protocolPersistenceConfigInit(NULL);
        protocolPersistenceConfigInit(&persistence);
        assert(persistence.tables == config.tables);
        assert(persistence.table_count == config.table_count);
        assert(persistence.persist_count == PROTOCOL_PERSIST_SIG_COUNT);
        assert(persistence.name != NULL);
        /* 产品配置不分配索引，也不覆盖 system 提供的存储资源。 */
        assert(persistence.index_storage == NULL);
#ifdef TEST_ADDED_SIG
        _Static_assert(PROTOCOL_PERSIST_SIG_COUNT == 13,
                       "Persistence count must follow list size");
        assert(persistence.persist_sigs[0].sigKey == IDU_SIG_SPARE_KEY);
#else
        _Static_assert(PROTOCOL_PERSIST_SIG_COUNT == 12,
                       "Unexpected persistence selection");
#endif
    }
#endif
#if !ABUS_DYNAMIC_ENABLE
    /* 重复取配置不能重置已经活动的静态实例。 */
    config.instance->tables = borrowed_tables;
    assert(protocolBusConfigInit(&config) == A_STATUS_OK);
    assert(config.instance->tables == borrowed_tables);
#endif
#endif
    /* 通信关闭时无需创建总线，也不再由 protocol 创建数据测试任务。 */
    assert(protocolInit(NULL) == A_STATUS_OK);
    assert(protocolInit(NULL) == A_STATUS_BUSY);
#if ABUS_ENABLE
    definitions_check();
#endif
    return 0;
}
