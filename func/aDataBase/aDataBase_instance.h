#ifndef ADATABASE_INSTANCE_H
#define ADATABASE_INSTANCE_H

#include "aDataBase.h"
#include "flashdb.h"

/* 供静态分配存储使用；字段私有，布局不是稳定的二进制接口。
 * FlashDB 配置必须与模块编译时一致，应用不得直接调用官方接口。 */
typedef struct aDataBaseInstance {
    struct aDataBaseInstance *next;
    const aMemoryHandle_t *partition;
    aBool_t dynamic;
    aBool_t fault;
} aDataBaseInstance_t;

struct aDataBaseKvHandle {
    aDataBaseInstance_t instance;
    struct fdb_kvdb db;
};

struct aDataBaseTsHandle {
    aDataBaseInstance_t instance;
    struct fdb_tsdb db;
};

#endif
