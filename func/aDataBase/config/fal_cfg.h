#ifndef PROJECT_FAL_CFG_H
#define PROJECT_FAL_CFG_H

#include "fdb_cfg.h"
#include "aDatabase_flash_layout.h"

/* 使用编译期分区表，不扫描 Flash，也不动态申请分区表。 */
#define FAL_PRINTF aDataBaseUpstreamLog
#define FAL_PART_HAS_TABLE_CFG
extern struct fal_flash_dev aDataBaseFalFlash;
#define FAL_FLASH_DEV_TABLE { &aDataBaseFalFlash }
#define FAL_PART_TABLE { \
    { FAL_PART_MAGIC_WORD, ADATABASE_PART_PARAM_NAME, \
      ADATABASE_FLASH_DEVICE_NAME, ADATABASE_PART_PARAM_OFFSET, \
      ADATABASE_PART_PARAM_SIZE, 0 }, \
    { FAL_PART_MAGIC_WORD, ADATABASE_PART_LOG_NAME, \
      ADATABASE_FLASH_DEVICE_NAME, ADATABASE_PART_LOG_OFFSET, \
      ADATABASE_PART_LOG_SIZE, 0 } \
}

#endif
