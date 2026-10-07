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

/** 静态索引容量：SIG 数为持久化清单长度；扇区数为分区长度 / 擦除块大小。
 * 存储在关闭前保持有效，内容由模块管理；不保存业务值的副本。
 * sector 数组使用 FlashDB 私有类型，仅在本静态布局头文件暴露。 */
struct aDataBaseKvIndexStorage {
    uint32_t *sig_offsets;
    size_t sig_capacity;
    struct kvdb_sec_info *sectors;
    size_t sector_capacity;
};

struct aDataBaseKvHandle {
    aDataBaseInstance_t instance;
    struct fdb_kvdb db;
    const aBusTable_t *tables;
    size_t table_count;
    const aDataBaseSigKey_t *persist_sigs;
    size_t persist_count;
    const uint16_t *const *sig_slots;
    aDataBaseKvIndexStorage_t index;
    void *index_memory; /**< 仅非 NULL 时由本实例释放。 */
    uint32_t *active_slot; /**< 当前请求的 O(1) 槽位，模块锁保护。 */
    char active_key[15]; /**< @sig:DDDD:KKKK，十六进制大写。 */
};

struct aDataBaseTsHandle {
    aDataBaseInstance_t instance;
    struct fdb_tsdb db;
};

#endif
