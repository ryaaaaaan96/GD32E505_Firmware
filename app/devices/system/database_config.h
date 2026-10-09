#ifndef APP_DATABASE_CONFIG_H
#define APP_DATABASE_CONFIG_H

#include "aDataBase.h"

/* 填充产品数据库配置，不打开分区；是否允许格式化由调用方指定。 */
void appSystemDatabaseKvConfigInit(aDataBaseKvConfig_t *config);
void appSystemDatabaseTsConfigInit(aDataBaseTsConfig_t *config);

#endif
