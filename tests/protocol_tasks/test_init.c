#include "protocol.h"
#include "app_config.h"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "data_bus_service.h"
#if !ABUS_DYNAMIC_ENABLE
#include "aBus_instance.h"
#endif
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

static unsigned stage, failure;
static const aBusTable_t *borrowed_tables;

/* 仅向清单头部插入一项，也必须同步改变索引、描述及静态容量。 */
#ifdef TEST_ADDED_SIG
_Static_assert(IDU_SIG_SPARE == 0 && IDU_SIG_COUNTER == 1,
               "SIG indices must follow list order");
_Static_assert(IDU_SIG_SPARE_KEY == 900U && IDU_SIG_COUNTER_KEY == 42U,
               "SIG keys must not follow list order");
_Static_assert(IDU_SIG_COUNT == 2, "SIG count must follow list size");
#endif

static void definitions_check(void)
{
    const aBusSig_t *counter =
        &borrowed_tables[0].sigs[IDU_SIG_COUNTER];
    const aBusSig_t *motor = &borrowed_tables[1].sigs[FAN_SIG_MOTOR];
    const FANMotor_t *defaults = motor->default_data;

    assert(borrowed_tables[0].deviceID == IDU_SIG_DEVICE_ID);
    assert(borrowed_tables[1].deviceID == FAN_SIG_DEVICE_ID);
    assert(borrowed_tables[0].sig_count == IDU_SIG_COUNT);
    assert(borrowed_tables[1].sig_count == FAN_SIG_COUNT);
    assert(counter->sigKey == 42U && counter->type == ALIB_DATA_U32);
    assert(counter->size == sizeof(uint32_t));
    assert(counter->flags == ABUS_SIG_FLAG_LOCK);
    assert(counter->default_data == NULL);
    assert(motor->sigKey == 1001U && motor->type == ALIB_DATA_STRUCT);
    assert(motor->size == sizeof(FANMotor_t));
    assert(defaults != NULL && defaults->speed == 100U);
    assert(defaults->temperature == 25);
    assert(motor->param_count == FAN_MOTOR_PARAM_COUNT);
    assert(motor->params[FAN_MOTOR_SPEED].offset ==
           offsetof(FANMotor_t, speed));
    assert(motor->params[FAN_MOTOR_SPEED].range->min.u32 == 0U);
    assert(motor->params[FAN_MOTOR_SPEED].range->max.u32 == 6000U);
    assert(motor->params[FAN_MOTOR_TEMPERATURE].offset ==
           offsetof(FANMotor_t, temperature));
    assert(motor->params[FAN_MOTOR_TEMPERATURE].type == ALIB_DATA_S32);
    assert(motor->params[FAN_MOTOR_TEMPERATURE].range == NULL);
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

static aStatus_t step(void)
{
    stage++;
    return stage == failure ? A_STATUS_ERROR : A_STATUS_OK;
}

aStatus_t dataBusInit(const dataBusConfig_t *config)
{
    assert(stage == 0U && config != NULL);
    assert(config->instanceID == PROTOCOL_BUS_INSTANCE_ID);
    assert(config->table_count == 2U);
#if !ABUS_DYNAMIC_ENABLE
    assert(config->instance != NULL);
    assert(config->instance->capacity == IDU_SIG_COUNT + FAN_SIG_COUNT);
    assert(config->instance->sigs != NULL);
#endif
    borrowed_tables = config->tables;
    definitions_check();
    return step();
}

aStatus_t appSigTaskInit(void)
{
    assert(stage == 1U);
    return step();
}

int main(int argc, char **argv)
{
    aStatus_t status;
    unsigned expected;

    assert(argc == 2);
    failure = (unsigned)strtoul(argv[1], NULL, 10);
    status = protocolInit();
    if (failure == 0U) {
        assert(status == A_STATUS_OK);
        expected = ABUS_ENABLE ? 2U : 0U;
    } else {
        assert(status == A_STATUS_ERROR);
        expected = failure;
    }
    assert(stage == expected);
    /* 重复调用不能再次装配静态 aBus 或重复创建任务。 */
    assert(protocolInit() == A_STATUS_BUSY);
    assert(stage == expected);
    if (borrowed_tables != NULL) {
        /* 初始化返回后仍能读取规则和默认值，不能借用栈上的临时对象。 */
        definitions_check();
    }
    return 0;
}
