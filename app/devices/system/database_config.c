#include "database_config.h"
#include "aMemory_layout.h"

void appSystemDatabaseKvConfigInit(aDataBaseKvConfig_t *config)
{
    if (config == NULL) return;
    aDataBaseKvConfigStructInit(config);
    config->name = "parameters";
    config->partition = AMEMORY_PART_PARAM_NAME;
    config->timeout = A_TIMEOUT_MS(30000U);
}

void appSystemDatabaseTsConfigInit(aDataBaseTsConfig_t *config)
{
    if (config == NULL) return;
    aDataBaseTsConfigStructInit(config);
    config->name = "records";
    config->partition = AMEMORY_PART_LOG_NAME;
    /* 预留给采集记录；时间戳和记录格式由业务提供。 */
    config->max_record_size = 256U;
    config->timeout = A_TIMEOUT_MS(30000U);
}
