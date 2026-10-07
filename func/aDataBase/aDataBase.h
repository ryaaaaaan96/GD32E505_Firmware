#ifndef ADATABASE_H
#define ADATABASE_H

#include "aLib.h"
#include "aBus.h"

#ifndef ADATABASE_STATIC_ENABLE
#define ADATABASE_STATIC_ENABLE 1
#endif
#ifndef ADATABASE_DYNAMIC_ENABLE
#define ADATABASE_DYNAMIC_ENABLE 1
#endif

typedef struct aDataBaseKvHandle aDataBaseKvHandle_t;
typedef struct aDataBaseTsHandle aDataBaseTsHandle_t;
typedef struct aDataBaseKvIndexStorage aDataBaseKvIndexStorage_t;
typedef int64_t aDataBaseTime_t;

/** @brief 需要持久化的 SIG，按 deviceID、sigKey 升序且不得重复。
 * 建议使用 const 数组存放于程序 Flash；清单与 aBus 业务表分别维护。 */
typedef struct {
    uint16_t deviceID;
    uint16_t sigKey;
} aDataBaseSigKey_t;

/** @brief Flash 映射表中表示该 SIG 不参与持久化。 */
#define ADATABASE_SIG_SLOT_NONE UINT16_MAX

typedef struct {
    const char *name; /**< 实例名，借用至反初始化或销毁。 */
    const char *partition; /**< 产品布局中的分区名，仅初始化时读取。 */
    aBool_t format_if_needed; /**< 允许官方初始化修复或格式化，默认关闭。 */
    aTimeout_t timeout; /**< 初始化总预算，包含锁等待及全部存储访问。 */
    const aBusTable_t *tables; /**< 可选 SIG 定义；借用至关闭，不修改 aBus。 */
    size_t table_count; /**< 表内 sigKey、表间 deviceID 必须唯一。 */
    const aDataBaseSigKey_t *persist_sigs; /**< NULL 表示不启用 SIG 持久化。 */
    size_t persist_count; /**< 仅这些 SIG 占用记录偏移 RAM，每项 4 字节。 */
    /** 可选程序 Flash 映射：外层与 tables 一一对应，内层按 sigIndex 排列，
     * 长度为对应表的 sig_count，值为 persist_sigs 下标或 SLOT_NONE。
     * 某张表为 NULL 时用二分；映射表及指针数组均借用至关闭。
     * 使用映射时槽位编号必须小于 UINT16_MAX，初始化检查映射一致性。 */
    const uint16_t *const *sig_slots;
    /** 静态索引存储，布局见 instance.h；Create 未提供时一次动态分配。
     * InitStatic 配置持久化清单时必填；数组不得由活动实例共用。 */
    aDataBaseKvIndexStorage_t *index_storage;
} aDataBaseKvConfig_t;

/** @brief 持久化完整 SIG 快照；定位使用当前表下标。 */
typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    const void *data;
    size_t size; /**< 必须与 SIG 定义完全一致。 */
    aTimeout_t timeout;
} aDataBaseSigSetRequest_t;

typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    void *data;
    size_t size; /**< 必须与 SIG 定义及已保存数据长度完全一致。 */
    aTimeout_t timeout;
} aDataBaseSigGetRequest_t;

typedef struct {
    uint16_t deviceID;
    size_t sigIndex;
    aTimeout_t timeout;
} aDataBaseSigDeleteRequest_t;

typedef struct {
    const char *name;
    const char *partition;
    size_t max_record_size; /**< 单条记录上限，必须小于擦除块减去元数据。 */
    aBool_t rollover; /**< 空间耗尽时覆盖最旧扇区，默认开启。 */
    aBool_t format_if_needed;
    aTimeout_t timeout;
} aDataBaseTsConfig_t;

typedef struct {
    const char *key; /**< 非空，最大 63 字节，大小写敏感。 */
    const void *data; /**< 任意二进制数据，仅调用期间借用。 */
    size_t size; /**< 字节数，必须大于零。 */
    aTimeout_t timeout;
} aDataBaseKvSetRequest_t;

typedef struct {
    const char *key;
    void *data; /**< 输出缓冲区；NULL 且 capacity 为零时仅查询长度。 */
    size_t capacity;
    size_t *size_out; /**< 必填；成功或缓冲不足时返回完整值的长度。 */
    aTimeout_t timeout;
} aDataBaseKvGetRequest_t;

typedef struct {
    const char *key;
    aTimeout_t timeout;
} aDataBaseKvDeleteRequest_t;

typedef struct {
    aDataBaseTime_t timestamp; /**< 正数，严格大于上一条；时间单位由应用统一。 */
    const void *data;
    size_t size;
    aTimeout_t timeout;
} aDataBaseTsAppendRequest_t;

typedef struct {
    aDataBaseTime_t timestamp;
    const void *data; /**< 使用迭代请求缓冲区，仅本次回调期间有效。 */
    size_t size;
} aDataBaseTsRecord_t;

typedef struct {
    aDataBaseTime_t from; /**< 包含起点；默认零。 */
    aDataBaseTime_t to; /**< 包含终点；默认 INT64_MAX，按时间升序遍历。 */
    void *buffer; /**< 每条记录的复用缓冲区；不足时停止并返回 NO_MEMORY。 */
    size_t capacity;
    /** 任务上下文、持有模块锁；返回 A_TRUE 继续、A_FALSE 停止。
     * 不得重入数据库，也不得调用存储绑定或生命周期接口。 */
    aBool_t (*callback)(const aDataBaseTsRecord_t *record, void *context);
    void *context;
    aTimeout_t timeout;
} aDataBaseTsIterateRequest_t;

typedef struct {
    aDataBaseTime_t last_timestamp; /**< 无记录时为零。 */
    size_t max_record_size;
    aBool_t rollover;
} aDataBaseTsInfo_t;

void aDataBaseKvConfigStructInit(aDataBaseKvConfig_t *config);
void aDataBaseSigSetRequestStructInit(aDataBaseSigSetRequest_t *request);
void aDataBaseSigGetRequestStructInit(aDataBaseSigGetRequest_t *request);
void aDataBaseSigDeleteRequestStructInit(aDataBaseSigDeleteRequest_t *request);
void aDataBaseTsConfigStructInit(aDataBaseTsConfig_t *config);
void aDataBaseKvSetRequestStructInit(aDataBaseKvSetRequest_t *request);
void aDataBaseKvGetRequestStructInit(aDataBaseKvGetRequest_t *request);
void aDataBaseKvDeleteRequestStructInit(aDataBaseKvDeleteRequest_t *request);
void aDataBaseTsAppendRequestStructInit(aDataBaseTsAppendRequest_t *request);
void aDataBaseTsIterateRequestStructInit(aDataBaseTsIterateRequest_t *request);

/** 生命周期由应用串行编排，不得与任何数据库调用并发。
 * 先初始化 aMemory，再初始化本模块；存在活动实例时 DeInit 返回 BUSY。
 * 静态实例布局在 aDataBase_instance.h，初始化后禁止复制或移动。
 * 同一分区仅允许一个实例；所有接口均限任务上下文。 */
aStatus_t aDataBaseInit(void);
aStatus_t aDataBaseDeInit(void);

#if ADATABASE_STATIC_ENABLE
aStatus_t aDataBaseKvInitStatic(
    const aDataBaseKvConfig_t *config, aDataBaseKvHandle_t *handle);
aStatus_t aDataBaseKvDeInitStatic(aDataBaseKvHandle_t *handle);
aStatus_t aDataBaseTsInitStatic(
    const aDataBaseTsConfig_t *config, aDataBaseTsHandle_t *handle);
aStatus_t aDataBaseTsDeInitStatic(aDataBaseTsHandle_t *handle);
#endif
#if ADATABASE_DYNAMIC_ENABLE
aStatus_t aDataBaseKvCreate(
    const aDataBaseKvConfig_t *config, aDataBaseKvHandle_t **handle_out);
aStatus_t aDataBaseKvDestroy(aDataBaseKvHandle_t *handle);
aStatus_t aDataBaseTsCreate(
    const aDataBaseTsConfig_t *config, aDataBaseTsHandle_t **handle_out);
aStatus_t aDataBaseTsDestroy(aDataBaseTsHandle_t *handle);
#endif

/** 完整操作统一串行化，超时包含锁等待及存储调用；不支持 NO_WAIT。
 * 获取值不截断；未知 key 返回 NOT_FOUND，缓冲不足返回 NO_MEMORY。
 * 首个存储错误优先返回；I/O 失败后实例需关闭并重新打开以恢复缓存。
 * 失败可能已有部分数据写入，不承诺回滚。 */
aStatus_t aDataBaseKvSet(
    aDataBaseKvHandle_t *handle, const aDataBaseKvSetRequest_t *request);
aStatus_t aDataBaseKvGet(
    aDataBaseKvHandle_t *handle, const aDataBaseKvGetRequest_t *request);
aStatus_t aDataBaseKvDelete(
    aDataBaseKvHandle_t *handle, const aDataBaseKvDeleteRequest_t *request);
/** 仅保存/读取请求中的字节快照，不自动同步 aBus RAM，不校验业务范围。
 * 磁盘标识为分区内的 deviceID + sigKey，保留键前缀 "@sig:"。
 * 未保存返回 NOT_FOUND；旧值长度不符返回 INVALID_PARAM，不修改输出。
 * 定长记录格式及跨版本迁移由应用管理；不自动格式化或写默认值。
 * 查找设备 O(表数)；配置 sig_slots 后表内 O(1)，否则二分持久化清单。
 * 正常查询不扫描 Flash 记录；记录 CRC 仍需读取当前记录的数据。
 * 未列入清单的 SIG 返回 UNSUPPORTED；启动和 GC 仍需遍历 Flash。
 * 并发、超时及故障契约与 KV 接口相同。 */
aStatus_t aDataBaseSigSet(
    aDataBaseKvHandle_t *handle, const aDataBaseSigSetRequest_t *request);
aStatus_t aDataBaseSigGet(
    aDataBaseKvHandle_t *handle, const aDataBaseSigGetRequest_t *request);
aStatus_t aDataBaseSigDelete(
    aDataBaseKvHandle_t *handle, const aDataBaseSigDeleteRequest_t *request);
aStatus_t aDataBaseTsAppend(
    aDataBaseTsHandle_t *handle, const aDataBaseTsAppendRequest_t *request);
aStatus_t aDataBaseTsIterate(
    aDataBaseTsHandle_t *handle, const aDataBaseTsIterateRequest_t *request);
/** 查询动态状态，等待模块锁；不执行介质读写。 */
aStatus_t aDataBaseTsGetInfo(
    aDataBaseTsHandle_t *handle, aDataBaseTsInfo_t *info);

#endif
