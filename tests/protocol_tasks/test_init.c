#include "protocol.h"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "data_bus_service.h"
#include <assert.h>
#include <stdlib.h>

static unsigned stage, failure;
static const aBusSig_t sigs[1];
static const aBusTable_t *borrowed_tables;

static aStatus_t step(void)
{
    stage++;
    return stage == failure ? A_STATUS_ERROR : A_STATUS_OK;
}

aStatus_t IDUSigTableInit(aBusTable_t *table)
{
    assert(stage == 0U && table != NULL);
    table->deviceID = IDU_SIG_DEVICE_ID;
    table->sigs = sigs;
    table->sig_count = 1U;
    return step();
}

aStatus_t FANSigTableInit(aBusTable_t *table)
{
    assert(stage == 1U && table != NULL);
    table->deviceID = FAN_SIG_DEVICE_ID;
    table->sigs = sigs;
    table->sig_count = 1U;
    return step();
}

aStatus_t dataBusInit(const dataBusConfig_t *config)
{
    assert(stage == 2U && config != NULL);
    assert(config->instanceID == PROTOCOL_BUS_INSTANCE_ID);
    assert(config->table_count == 2U);
    assert(config->tables[0].deviceID == IDU_SIG_DEVICE_ID);
    assert(config->tables[1].deviceID == FAN_SIG_DEVICE_ID);
#if !ABUS_DYNAMIC_ENABLE
    assert(config->instance != NULL);
#endif
    borrowed_tables = config->tables;
    return step();
}

aStatus_t appSigTaskInit(void)
{
    assert(stage == 3U);
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
        expected = ABUS_ENABLE ? 4U : 0U;
    } else {
        assert(status == A_STATUS_ERROR);
        expected = failure;
    }
    assert(stage == expected);
    /* 重复调用不能再次装配静态 aBus 或重复创建任务。 */
    assert(protocolInit() == A_STATUS_BUSY);
    assert(stage == expected);
    if (borrowed_tables != NULL) {
        assert(borrowed_tables[0].deviceID == IDU_SIG_DEVICE_ID);
        assert(borrowed_tables[1].deviceID == FAN_SIG_DEVICE_ID);
    }
    return 0;
}
