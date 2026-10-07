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
    if (fail_write) return A_STATUS_TIMEOUT;
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

static aDataBaseKvHandle_t *kv;
static aDataBaseTsHandle_t *ts;
#if ADATABASE_STATIC_ENABLE
static aDataBaseKvHandle_t kv_storage;
static aDataBaseTsHandle_t ts_storage;
#endif

static aStatus_t kv_open(const aDataBaseKvConfig_t *config)
{
#if ADATABASE_DYNAMIC_ENABLE
    return aDataBaseKvCreate(config, &kv);
#else
    aStatus_t status = aDataBaseKvInitStatic(config, &kv_storage);
    kv = status == A_STATUS_OK ? &kv_storage : NULL;
    return status;
#endif
}
static aStatus_t ts_open(const aDataBaseTsConfig_t *config)
{
#if ADATABASE_DYNAMIC_ENABLE
    return aDataBaseTsCreate(config, &ts);
#else
    aStatus_t status = aDataBaseTsInitStatic(config, &ts_storage);
    ts = status == A_STATUS_OK ? &ts_storage : NULL;
    return status;
#endif
}
static void kv_close(void)
{
#if ADATABASE_DYNAMIC_ENABLE
    assert(aDataBaseKvDestroy(kv) == A_STATUS_OK);
#else
    assert(aDataBaseKvDeInitStatic(kv) == A_STATUS_OK);
#endif
    kv = NULL;
}
static void ts_close(void)
{
#if ADATABASE_DYNAMIC_ENABLE
    assert(aDataBaseTsDestroy(ts) == A_STATUS_OK);
#else
    assert(aDataBaseTsDeInitStatic(ts) == A_STATUS_OK);
#endif
    ts = NULL;
}

static size_t record_count;
static aDataBaseTime_t first_time, previous_time;
static aBool_t stop_first;
static aBool_t collect_record(const aDataBaseTsRecord_t *record, void *context)
{
    assert(context == &record_count);
    assert(record->timestamp > previous_time);
    assert(record->size == 4U || record->size == 256U);
    assert(record->data != NULL);
    if (record_count == 0U) first_time = record->timestamp;
    previous_time = record->timestamp;
    ++record_count;
    return !stop_first;
}

static void iterate(aDataBaseTsIterateRequest_t *request)
{
    record_count = 0U;
    previous_time = 0;
    assert(aDataBaseTsIterate(ts, request) == A_STATUS_OK);
}

int main(void)
{
    aDataBaseKvConfig_t kc;
    aDataBaseTsConfig_t tc;
    aDataBaseKvSetRequest_t set;
    aDataBaseKvGetRequest_t get;
    aDataBaseKvDeleteRequest_t del;
    aDataBaseTsAppendRequest_t append;
    aDataBaseTsIterateRequest_t query;
    aDataBaseTsInfo_t info;
    uint8_t input[256], output[256];
    size_t length;
    unsigned before;
    const aDataBaseTime_t epoch = INT64_C(5000000000);

    memset(memory, 0xFF, sizeof(memory));
    memset(input, 0xA5, sizeof(input));
    assert(memory_start() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    aDataBaseKvConfigStructInit(&kc);
    kc.name = "kv";
    kc.partition = AMEMORY_PART_PARAM_NAME;
    aDataBaseTsConfigStructInit(&tc);
    tc.name = "ts";
    tc.partition = AMEMORY_PART_LOG_NAME;
    /* 空白介质不得在默认初始化时被擦写。 */
    assert(kv_open(&kc) == A_STATUS_NOT_READY);
    assert(ts_open(&tc) == A_STATUS_NOT_READY);
    assert(writes == 0U && erases == 0U);
    /* 初始化总预算耗尽时，不得因后续官方修复尝试而擦写数据。 */
    kc.format_if_needed = A_TRUE;
    kc.timeout = A_TIMEOUT_MS(1U);
    io_tick = 1U;
    assert(kv_open(&kc) == A_STATUS_TIMEOUT);
    assert(writes == 0U && erases == 0U);
    io_tick = 0U;
    kc.timeout = A_TIMEOUT_MS(5000U);
    kc.format_if_needed = A_TRUE;
    tc.format_if_needed = A_TRUE;
    assert(kv_open(&kc) == A_STATUS_OK);
    assert(ts_open(&tc) == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_BUSY);
#if ADATABASE_STATIC_ENABLE && ADATABASE_DYNAMIC_ENABLE
    assert(aDataBaseKvInitStatic(&kc, &kv_storage) == A_STATUS_BUSY);
    assert(aDataBaseTsInitStatic(&tc, &ts_storage) == A_STATUS_BUSY);
    assert(aDataBaseKvDeInitStatic(kv) == A_STATUS_INVALID_PARAM);
    assert(aDataBaseTsDeInitStatic(ts) == A_STATUS_INVALID_PARAM);
#endif

    aDataBaseKvSetRequestStructInit(&set);
    set.key = "binary";
    set.data = input;
    set.size = sizeof(input);
    assert(aDataBaseKvSet(kv, &set) == A_STATUS_OK);
    aDataBaseKvGetRequestStructInit(&get);
    get.key = set.key;
    get.size_out = &length;
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_OK && length == sizeof(input));
    get.data = output;
    get.capacity = 1U;
    memset(output, 0x55, sizeof(output));
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_NO_MEMORY);
    assert(output[0] == 0x55 && length == sizeof(input));
    get.capacity = sizeof(output);
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_OK);
    assert(memcmp(input, output, sizeof(input)) == 0);
    before = erases;
    /* 多次替换同一个 key，跨越分区容量以触发真实垃圾回收。 */
    for (uint32_t i = 0U; i < 1000U; ++i) {
        memcpy(input, &i, sizeof(i));
        assert(aDataBaseKvSet(kv, &set) == A_STATUS_OK);
    }
    assert(erases > before);
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_OK);
    assert(memcmp(input, output, sizeof(input)) == 0);
    set.timeout = A_TIMEOUT_NO_WAIT;
    assert(aDataBaseKvSet(kv, &set) == A_STATUS_UNSUPPORTED);
    set.timeout = A_TIMEOUT_MS(5000U);
    before = writes;
    database_fail_lock = 1;
    assert(aDataBaseKvSet(kv, &set) == A_STATUS_TIMEOUT && writes == before);
    database_fail_lock = 0;

    aDataBaseTsAppendRequestStructInit(&append);
    append.data = input;
    append.size = 4U;
    for (int i = 1; i <= 3; ++i) {
        append.timestamp = epoch + i;
        assert(aDataBaseTsAppend(ts, &append) == A_STATUS_OK);
    }
    assert(aDataBaseTsAppend(ts, &append) == A_STATUS_INVALID_PARAM);
    aDataBaseTsIterateRequestStructInit(&query);
    query.buffer = output;
    query.capacity = sizeof(output);
    query.callback = collect_record;
    query.context = &record_count;
    query.from = epoch + 2;
    query.to = epoch + 3;
    iterate(&query);
    assert(record_count == 2U && first_time == epoch + 2);
    stop_first = A_TRUE;
    iterate(&query);
    assert(record_count == 1U);
    stop_first = A_FALSE;
    query.capacity = 1U;
    assert(aDataBaseTsIterate(ts, &query) == A_STATUS_NO_MEMORY);
    query.capacity = sizeof(output);
    /* 写满后验证覆盖最旧扇区，最新记录和升序遍历仍正常。 */
    append.size = sizeof(input);
    for (int i = 4; i <= 2100; ++i) {
        append.timestamp = epoch + i;
        assert(aDataBaseTsAppend(ts, &append) == A_STATUS_OK);
    }
    query.from = 0;
    query.to = INT64_MAX;
    iterate(&query);
    assert(record_count > 0U && record_count < 2100U);
    assert(previous_time == epoch + 2100 && first_time > epoch + 3);
    assert(aDataBaseTsGetInfo(ts, &info) == A_STATUS_OK);
    assert(info.last_timestamp == epoch + 2100 && info.rollover);

    /* 关闭、重新绑定和重开后，KV 及六十四位时间戳均从 Flash 恢复。 */
    kv_close();
    ts_close();
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    kc.format_if_needed = A_FALSE;
    tc.format_if_needed = A_FALSE;
    assert(kv_open(&kc) == A_STATUS_OK);
    assert(ts_open(&tc) == A_STATUS_OK);
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_OK);
    assert(memcmp(input, output, sizeof(input)) == 0);
    assert(aDataBaseTsGetInfo(ts, &info) == A_STATUS_OK);
    assert(info.last_timestamp == epoch + 2100);

    /* 禁止覆盖时，写满返回空间不足，并保持最后成功时间戳。 */
    ts_close();
    tc.rollover = A_FALSE;
    assert(ts_open(&tc) == A_STATUS_OK);
    append.timestamp = epoch + 2100;
    for (unsigned i = 0U; i < 128U; ++i) {
        aStatus_t status;

        ++append.timestamp;
        status = aDataBaseTsAppend(ts, &append);
        if (status == A_STATUS_NO_MEMORY) break;
        assert(status == A_STATUS_OK);
        assert(i < 127U);
    }
    assert(aDataBaseTsGetInfo(ts, &info) == A_STATUS_OK);
    assert(!info.rollover && info.last_timestamp == append.timestamp - 1);

    aDataBaseKvDeleteRequestStructInit(&del);
    del.key = get.key;
    assert(aDataBaseKvDelete(kv, &del) == A_STATUS_OK);
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_NOT_FOUND);
    assert(aDataBaseKvDelete(kv, &del) == A_STATUS_NOT_FOUND);
    /* 存储首错必须保留，后续官方回调不得继续访问实际介质。 */
    before = writes;
    fail_write = 1;
    assert(aDataBaseKvSet(kv, &set) == A_STATUS_TIMEOUT);
    assert(writes == before + 1U);
    fail_write = 0;
    assert(aDataBaseKvGet(kv, &get) == A_STATUS_NOT_READY);
    before = reads;
    record_count = 0U;
    fail_read = 1;
    assert(aDataBaseTsIterate(ts, &query) == A_STATUS_TIMEOUT);
    assert(reads == before + 1U && record_count == 0U);
    fail_read = 0;
    assert(aDataBaseTsGetInfo(ts, &info) == A_STATUS_NOT_READY);
    kv_close();
    ts_close();
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(database_allocations == 0U);
    puts("真实 FlashDB KV/TSDB：回收、覆盖、持久化及错误传播验证通过");
    return 0;
}
