#include "aDataBase_internal.h"
#include "aMemory_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern uint32_t database_tick;
extern size_t database_allocations;
extern int database_fail_lock;
static uint8_t memory[AMEMORY_FLASH_CAPACITY_BYTES];
static unsigned reads, writes, erases;
static int fail_read, fail_write;
static unsigned fail_write_at;
static uint32_t io_tick;

static void bounds(uint32_t address, uint32_t size)
{
    assert(address <= sizeof(memory) && size <= sizeof(memory) - address);
    assert(address >= AMEMORY_PART_PARAM_OFFSET);
    assert(address + size <= AMEMORY_PART_LOG_OFFSET +
           AMEMORY_PART_LOG_SIZE);
}

static aStatus_t read_bytes(
    void *context, const aMemoryReadRequest_t *request)
{
    uint32_t address = (uint32_t)request->address;
    uint32_t size = (uint32_t)request->size;
    aTimeout_t timeout = request->timeout;
    uint8_t *buffer = request->data;
    assert(context == memory && aTimeoutIsValid(timeout));
    bounds(address, size);
    ++reads;
    database_tick += io_tick;
    if (fail_read) return A_STATUS_TIMEOUT;
    memcpy(buffer, memory + address, size);
    return A_STATUS_OK;
}

static aStatus_t write_bytes(
    void *context, const aMemoryWriteRequest_t *request)
{
    uint32_t address = (uint32_t)request->address;
    uint32_t size = (uint32_t)request->size;
    aTimeout_t timeout = request->timeout;
    const uint8_t *buffer = request->data;
    assert(context == memory && aTimeoutIsValid(timeout));
    bounds(address, size);
    ++writes;
    database_tick += io_tick;
    if (fail_write || (fail_write_at != 0U && writes >= fail_write_at))
        return A_STATUS_TIMEOUT;
    /* NOR 只能把一写成零，必须先擦除才能恢复为一。 */
    for (size_t i = 0U; i < size; ++i) {
        assert((memory[address + i] & buffer[i]) == buffer[i]);
        memory[address + i] &= buffer[i];
    }
    return A_STATUS_OK;
}

static aStatus_t erase_bytes(
    void *context, const aMemoryEraseRequest_t *request)
{
    uint32_t address = (uint32_t)request->address;
    uint32_t size = (uint32_t)request->size;
    aTimeout_t timeout = request->timeout;
    assert(context == memory && aTimeoutIsValid(timeout));
    bounds(address, size);
    assert(address % AMEMORY_FLASH_BLOCK_SIZE == 0U);
    assert(size % AMEMORY_FLASH_BLOCK_SIZE == 0U);
    ++erases;
    database_tick += io_tick;
    memset(memory + address, 0xFF, size);
    return A_STATUS_OK;
}

#include "memory_fixture.h"

#define SIG_COUNT 180U
#define PERSIST_COUNT 150U
static aBusSig_t sigs[SIG_COUNT];
static aBusSig_t other_sig = {
    .sigKey = 10U, .size = sizeof(uint32_t), .type = ALIB_DATA_U32
};
static aBusTable_t tables[2] = {
    {.deviceID = 1U, .sigs = sigs, .sig_count = SIG_COUNT},
    {.deviceID = 2U, .sigs = &other_sig, .sig_count = 1U}
};
static aDataBaseSigKey_t persist[PERSIST_COUNT + 1U];
static uint32_t expected[PERSIST_COUNT];
static uint16_t sig_slots[SIG_COUNT];
static const uint16_t other_slots[] = {PERSIST_COUNT};
static const uint16_t *const slots[] = {sig_slots, other_slots};
static aDataBaseKvHandle_t *kv;
#if ADATABASE_STATIC_ENABLE
static aDataBaseKvHandle_t instance;
static uint32_t offsets[PERSIST_COUNT + 1U];
static struct kvdb_sec_info sectors[
    AMEMORY_PART_PARAM_SIZE / AMEMORY_FLASH_BLOCK_SIZE];
static aDataBaseKvIndexStorage_t storage = {
    .sig_offsets = offsets, .sig_capacity = PERSIST_COUNT + 1U,
    .sectors = sectors, .sector_capacity = sizeof(sectors) / sizeof(sectors[0])
};
#endif

static aStatus_t open_db(aDataBaseKvConfig_t *config)
{
#if ADATABASE_DYNAMIC_ENABLE
    return aDataBaseKvCreate(config, &kv);
#else
    aStatus_t status = aDataBaseKvInitStatic(config, &instance);
    kv = status == A_STATUS_OK ? &instance : NULL;
    return status;
#endif
}

static void close_db(void)
{
#if ADATABASE_DYNAMIC_ENABLE
    assert(aDataBaseKvDestroy(kv) == A_STATUS_OK);
#else
    assert(aDataBaseKvDeInitStatic(kv) == A_STATUS_OK);
#endif
    kv = NULL;
}

static aStatus_t put(uint16_t device, size_t index, uint32_t value)
{
    aDataBaseSigSetRequest_t request;
    aDataBaseSigSetRequestStructInit(&request);
    request.deviceID = device;
    request.sigIndex = index;
    request.data = &value;
    request.size = sizeof(value);
    return aDataBaseSigSet(kv, &request);
}

static aStatus_t get(uint16_t device, size_t index, uint32_t *value)
{
    aDataBaseSigGetRequest_t request;
    aDataBaseSigGetRequestStructInit(&request);
    request.deviceID = device;
    request.sigIndex = index;
    request.data = value;
    request.size = sizeof(*value);
    return aDataBaseSigGet(kv, &request);
}

static void verify_all(aBool_t reversed)
{
    uint32_t value;
    for (size_t i = 0U; i < PERSIST_COUNT; ++i) {
        size_t index = reversed ? SIG_COUNT - 1U - i : i;
        assert(get(1U, index, &value) == A_STATUS_OK);
        assert(value == expected[i]);
    }
    assert(get(2U, 0U, &value) == A_STATUS_OK && value == 123U);
}

int main(void)
{
    aDataBaseKvConfig_t config;
    aDataBaseSigDeleteRequest_t del;
    uint32_t value;
    unsigned before, hot_reads;

    memset(memory, 0xFF, sizeof(memory));
    for (size_t i = 0U; i < SIG_COUNT; ++i) {
        sigs[i] = (aBusSig_t){
            .sigKey = (uint16_t)(i + 10U),
            .size = sizeof(value), .type = ALIB_DATA_U32
        };
        sig_slots[i] = i < PERSIST_COUNT ? (uint16_t)i
                       : ADATABASE_SIG_SLOT_NONE;
        if (i < PERSIST_COUNT) persist[i] =
            (aDataBaseSigKey_t){.deviceID = 1U, .sigKey = sigs[i].sigKey};
    }
    persist[PERSIST_COUNT] =
        (aDataBaseSigKey_t){.deviceID = 2U, .sigKey = other_sig.sigKey};
    assert(memory_start() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    aDataBaseKvConfigStructInit(&config);
    config.name = "indexed";
    config.partition = AMEMORY_PART_PARAM_NAME;
    config.tables = tables;
    config.table_count = 2U;
    config.persist_sigs = persist;
    config.persist_count = PERSIST_COUNT + 1U;
    config.format_if_needed = A_TRUE;
    config.sig_slots = slots;
#if !ADATABASE_DYNAMIC_ENABLE
    config.index_storage = &storage;
#endif
    sig_slots[0] = 1U;
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    sig_slots[0] = 0U;
    sig_slots[PERSIST_COUNT] = 0U;
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    sig_slots[PERSIST_COUNT] = ADATABASE_SIG_SLOT_NONE;
    /* 重复、乱序、未知键和静态容量错误必须在写介质前拒绝。 */
    before = writes;
    persist[1] = persist[0];
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    persist[1].sigKey = 11U;
    persist[0].sigKey = 500U;
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    persist[0].sigKey = 10U;
    persist[PERSIST_COUNT].deviceID = 3U;
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    persist[PERSIST_COUNT].deviceID = 2U;
#if ADATABASE_STATIC_ENABLE
    config.index_storage = &storage;
    storage.sig_capacity = PERSIST_COUNT;
    assert(aDataBaseKvInitStatic(&config, &instance) == A_STATUS_NO_MEMORY);
    storage.sig_capacity = PERSIST_COUNT + 1U;
#if ADATABASE_DYNAMIC_ENABLE
    config.index_storage = NULL;
#endif
#endif
    assert(writes == before);
    assert(open_db(&config) == A_STATUS_OK);
    assert(kv->index.sig_capacity == PERSIST_COUNT + 1U);
    before = reads;
    assert(get(1U, 0U, &value) == A_STATUS_NOT_FOUND);
    assert(get(1U, PERSIST_COUNT, &value) == A_STATUS_UNSUPPORTED);
    assert(get(1U, SIG_COUNT, &value) == A_STATUS_NOT_FOUND);
    assert(get(99U, 0U, &value) == A_STATUS_NOT_FOUND);
    assert(put(1U, PERSIST_COUNT, 1U) == A_STATUS_UNSUPPORTED);
    assert(reads == before);
    for (size_t i = 0U; i < PERSIST_COUNT; ++i) {
        expected[i] = (uint32_t)i;
        before = reads;
        assert(put(1U, i, expected[i]) == A_STATUS_OK);
        /* 没有旧记录的新 SIG 不会遍历已有记录。 */
        assert(reads - before <= 1U);
    }
    assert(put(2U, 0U, 123U) == A_STATUS_OK);
    before = reads;
    assert(put(1U, 0U, 800U) == A_STATUS_OK);
    expected[0] = 800U;
    hot_reads = reads - before;
    assert(hot_reads <= 6U);
    verify_all(A_FALSE);
    /* 多于默认缓存的冷 SIG：轮流更新、跨分区容量，触发真实 GC 搬迁。 */
    before = erases;
    for (uint32_t i = 0U; i < 5000U; ++i) {
        size_t index = i % PERSIST_COUNT;
        expected[index] = i + 10000U;
        assert(put(1U, index, expected[index]) == A_STATUS_OK);
    }
    assert(erases > before);
    verify_all(A_FALSE);
    close_db();
    /* 固件表重新排序：sigIndex 改变，但稳定键仍找回原值。 */
    for (size_t i = 0U; i < SIG_COUNT / 2U; ++i) {
        aBusSig_t swap = sigs[i];
        sigs[i] = sigs[SIG_COUNT - 1U - i];
        sigs[SIG_COUNT - 1U - i] = swap;
    }
    aBusTable_t swap = tables[0];
    tables[0] = tables[1];
    tables[1] = swap;
    config.format_if_needed = A_FALSE;
    /* 重排后的固件也可不提供映射，改走二分，保存格式完全一致。 */
    config.sig_slots = NULL;
    assert(open_db(&config) == A_STATUS_OK);
    verify_all(A_TRUE);
    aDataBaseSigDeleteRequestStructInit(&del);
    del.deviceID = 1U;
    del.sigIndex = SIG_COUNT - 1U;
    assert(aDataBaseSigDelete(kv, &del) == A_STATUS_OK);
    before = reads;
    assert(get(1U, del.sigIndex, &value) == A_STATUS_NOT_FOUND);
    assert(aDataBaseSigDelete(kv, &del) == A_STATUS_NOT_FOUND);
    assert(reads == before);
    close_db();
    assert(open_db(&config) == A_STATUS_OK);
    before = reads;
    assert(get(1U, del.sigIndex, &value) == A_STATUS_NOT_FOUND);
    assert(reads == before);
    assert(put(1U, del.sigIndex, expected[0]) == A_STATUS_OK);
    verify_all(A_TRUE);
    /* 字符串路径修改索引键，仍更新同一槽位；普通键保持可用。 */
    aDataBaseKvSetRequest_t generic;
    aDataBaseKvSetRequestStructInit(&generic);
    generic.key = "@sig:0002:000A";
    generic.data = &value;
    generic.size = sizeof(value);
    value = 321U;
    assert(aDataBaseKvSet(kv, &generic) == A_STATUS_OK);
    assert(get(2U, 0U, &value) == A_STATUS_OK && value == 321U);
    assert(put(2U, 0U, 123U) == A_STATUS_OK);
    generic.key = "ordinary";
    assert(aDataBaseKvSet(kv, &generic) == A_STATUS_OK);
    /* 每个写步骤前断电：关闭、恢复后仅能读到旧值或已提交的新值。 */
    for (unsigned step = 1U; step <= 10U; ++step) {
        assert(put(2U, 0U, 123U) == A_STATUS_OK);
        fail_write_at = writes + step;
        aStatus_t status = put(2U, 0U, 456U);
        if (status != A_STATUS_OK) {
            assert(status == A_STATUS_TIMEOUT);
            assert(get(2U, 0U, &value) == A_STATUS_NOT_READY);
        }
        fail_write_at = 0U;
        close_db();
        assert(open_db(&config) == A_STATUS_OK);
        assert(get(2U, 0U, &value) == A_STATUS_OK);
        assert(value == 123U || value == 456U);
    }
    /* 固件改变值长度时，不把旧记录截断或填入不匹配的结构体。 */
    close_db();
    other_sig.size = sizeof(uint64_t);
    assert(open_db(&config) == A_STATUS_OK);
    uint64_t wider = UINT64_MAX;
    aDataBaseSigGetRequest_t wide_get;
    aDataBaseSigGetRequestStructInit(&wide_get);
    wide_get.deviceID = 2U;
    wide_get.data = &wider;
    wide_get.size = sizeof(wider);
    assert(aDataBaseSigGet(kv, &wide_get) == A_STATUS_INVALID_PARAM);
    assert(wider == UINT64_MAX);
    close_db();
    other_sig.size = sizeof(value);
    assert(open_db(&config) == A_STATUS_OK);
    /* 总超时使实例失效，重新打开重建 RAM 缓存。 */
    fail_read = 1;
    assert(get(2U, 0U, &value) == A_STATUS_TIMEOUT);
    fail_read = 0;
    assert(get(2U, 0U, &value) == A_STATUS_NOT_READY);
    close_db();
    assert(open_db(&config) == A_STATUS_OK);
    close_db();
    /* 有索引但 CRC 损坏时必须报错且停止访问，不当成不存在继续替换。 */
    assert(open_db(&config) == A_STATUS_OK);
    struct fdb_kv saved;
    assert(aDataBaseOperationBegin(A_TIMEOUT_FOREVER) == A_STATUS_OK);
    assert(fdb_kv_get_obj(&kv->db, "@sig:0002:000A", &saved) != NULL);
    assert(aDataBaseOperationEnd(A_STATUS_OK) == A_STATUS_OK);
    memory[AMEMORY_PART_PARAM_OFFSET + saved.addr.value] ^= 1U;
    assert(get(2U, 0U, &value) == A_STATUS_ERROR);
    assert(put(2U, 0U, 1U) == A_STATUS_NOT_READY);
    close_db();
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(database_allocations == 0U);
    printf("SIG 直接映射/二分：稀疏存储、GC、重排、重开及写故障通过；"
           "普通更新 %u 次介质读取\n", hot_reads);
    return 0;
}
