/* 复用真实 FlashDB/NOR 模型及逐次写入故障注入。 */
#define main index_fixture_main
#include "test_index.c"
#undef main

static const char data_key[] = "@sig:0001:000A";
static const char mark_key[] = "@sig:0001:000A:layout";
static aBusParam_t fields[] = {
    {.offset = 0U, .size = 2U, .type = ALIB_DATA_U16},
    {.offset = 4U, .size = 4U, .type = ALIB_DATA_U32}
};
static uint8_t checkpoint[sizeof(memory)];

static void raw_set(const char *key, const void *data, size_t size)
{
    const aDataBaseKvSetRequest_t request = {
        .key = key, .data = data, .size = size,
        .timeout = A_TIMEOUT_MS(5000U)
    };
    assert(aDataBaseKvSet(kv, &request) == A_STATUS_OK);
}

static void raw_get(const char *key, void *data, size_t size)
{
    size_t actual = 0U;
    const aDataBaseKvGetRequest_t request = {
        .key = key, .data = data, .capacity = size, .size_out = &actual,
        .timeout = A_TIMEOUT_MS(5000U)
    };
    assert(aDataBaseKvGet(kv, &request) == A_STATUS_OK && actual == size);
}

static void snapshot_save(void)
{
    uint8_t data[16];
    memset(data, 0xA5, sizeof(data));
    const aDataBaseSigSetRequest_t request = {
        .deviceID = 1U, .sigIndex = 0U, .data = data,
        .size = sigs[0].size, .timeout = A_TIMEOUT_MS(5000U)
    };
    assert(aDataBaseSigSet(kv, &request) == A_STATUS_OK);
}

static void snapshot_check(aStatus_t expected_status)
{
    uint8_t data[16];
    memset(data, 0x6C, sizeof(data));
    const aDataBaseSigGetRequest_t request = {
        .deviceID = 1U, .sigIndex = 0U, .data = data,
        .size = sigs[0].size, .timeout = A_TIMEOUT_MS(5000U)
    };
    assert(aDataBaseSigGet(kv, &request) == expected_status);
    for (size_t i = 0U; i < sizeof(data); ++i)
        assert(data[i] == (expected_status == A_STATUS_OK && i < request.size
                          ? 0xA5U : 0x6CU));
}

static void unaffected_check(void)
{
    uint32_t value;
    assert(get(2U, 0U, &value) == A_STATUS_OK && value == 123U);
    raw_get("ordinary", &value, sizeof(value));
    assert(value == 456U);
}

int main(void)
{
    aDataBaseKvConfig_t config;
    uint8_t old_data[12];
    uint32_t mark, after, value = 456U;
    unsigned before, write_steps;
    size_t allocations;

    memset(memory, 0xFF, sizeof(memory));
    memset(old_data, 0xA5, sizeof(old_data));
    assert(memory_start() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    aDataBaseKvConfigStructInit(&config);
    config.name = "layout";
    config.memory = aMemoryFind(AMEMORY_PART_PARAM_NAME);
    config.format_if_needed = A_TRUE;
    /* 老固件只有数据 KV，无布局标记，即使长度相同也不能直接认可。 */
    assert(open_db(&config) == A_STATUS_OK);
    raw_set(data_key, old_data, sizeof(old_data));
    raw_set("ordinary", &value, sizeof(value));
    close_db();
    config.format_if_needed = A_FALSE;
    sigs[0] = (aBusSig_t){
        .sigKey = 10U, .size = sizeof(old_data), .type = ALIB_DATA_STRUCT,
        .params = fields, .param_count = 2U
    };
    tables[0].sig_count = 1U;
    persist[0] = (aDataBaseSigKey_t){.deviceID = 1U, .sigKey = 10U};
    persist[1] = (aDataBaseSigKey_t){.deviceID = 2U, .sigKey = 10U};
    config.tables = tables;
    config.table_count = 2U;
    config.persist_sigs = persist;
    config.persist_count = 2U;
#if !ADATABASE_DYNAMIC_ENABLE
    config.index_storage = &storage;
#endif
    allocations = database_allocations;
    before = writes;
    sigs[0].params = NULL;
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    sigs[0].params = fields;
    fields[1].offset = SIZE_MAX;
    assert(open_db(&config) == A_STATUS_INVALID_PARAM);
    fields[1].offset = 4U;
    assert(writes == before && database_allocations == allocations);
    assert(open_db(&config) == A_STATUS_OK);
    snapshot_check(A_STATUS_NOT_FOUND);
    raw_get(mark_key, &mark, sizeof(mark));
    assert(put(2U, 0U, 123U) == A_STATUS_OK);
    snapshot_save();
    /* 数据仍是原始字节，包括未登记字节；没有记录头或逐字段编码。 */
    raw_get(data_key, old_data, sizeof(old_data));
    for (size_t i = 0U; i < sizeof(old_data); ++i) assert(old_data[i] == 0xA5U);
    raw_get(mark_key, &after, sizeof(after));
    assert(after == mark);
    close_db();
    before = writes;
    assert(open_db(&config) == A_STATUS_OK && writes == before);
    snapshot_check(A_STATUS_OK);
    unaffected_check();

    /* 默认值、范围、同步标志不是原始布局的一部分。 */
    close_db();
    sigs[0].default_data = old_data;
    sigs[0].flags = ABUS_SIG_FLAG_LOCK;
    static const aBusRange_t range = {.min.u32 = 0U, .max.u32 = 100U};
    fields[1].range = &range;
    assert(open_db(&config) == A_STATUS_OK && writes == before);
    snapshot_check(A_STATUS_OK);
    close_db();
    fields[1].range = NULL;

    /* 同长度类型、偏移、字段顺序/数量及整体长度变化，仅删除对应 SIG。 */
    for (unsigned change = 0U; change < 5U; ++change) {
        if (change == 0U) fields[1].type = ALIB_DATA_S32;
        if (change == 1U) fields[1].offset = 8U;
        if (change == 2U) {
            aBusParam_t swap = fields[0];
            fields[0] = fields[1];
            fields[1] = swap;
        }
        if (change == 3U) sigs[0].param_count = 1U;
        if (change == 4U) sigs[0].size = 16U;
        assert(open_db(&config) == A_STATUS_OK);
        snapshot_check(A_STATUS_NOT_FOUND);
        unaffected_check();
        snapshot_save();
        close_db();
        before = writes;
        assert(open_db(&config) == A_STATUS_OK && writes == before);
        snapshot_check(A_STATUS_OK);
        close_db();
    }

    assert(open_db(&config) == A_STATUS_OK);
    /* 标记只在初始化使用：删去它后，正常保存/读取均不会访问或重建标记。 */
    const aDataBaseKvDeleteRequest_t del = {
        .key = mark_key, .timeout = A_TIMEOUT_MS(5000U)
    };
    assert(aDataBaseKvDelete(kv, &del) == A_STATUS_OK);
    snapshot_save();
    snapshot_check(A_STATUS_OK);
    size_t length;
    const aDataBaseKvGetRequest_t missing = {
        .key = mark_key, .size_out = &length, .timeout = A_TIMEOUT_MS(5000U)
    };
    assert(aDataBaseKvGet(kv, &missing) == A_STATUS_NOT_FOUND);
    close_db();
    assert(open_db(&config) == A_STATUS_OK);
    snapshot_check(A_STATUS_NOT_FOUND);
    snapshot_save();
    /* 无效标记长度也按不兼容处理。 */
    raw_set(mark_key, old_data, 3U);
    close_db();
    assert(open_db(&config) == A_STATUS_OK);
    snapshot_check(A_STATUS_NOT_FOUND);
    snapshot_save();

    /* 标记读故障必须上报，不删除仍然兼容的数据。 */
    struct fdb_kv record;
    assert(aDataBaseOperationBegin(A_TIMEOUT_FOREVER) == A_STATUS_OK);
    assert(fdb_kv_get_obj(&kv->db, mark_key, &record) != NULL);
    assert(aDataBaseOperationEnd(A_STATUS_OK) == A_STATUS_OK);
    close_db();
    before = writes;
    fail_read_address = AMEMORY_PART_PARAM_OFFSET + record.addr.value;
    assert(open_db(&config) == A_STATUS_TIMEOUT && kv == NULL);
    fail_read_address = UINT32_MAX;
    assert(writes == before && database_allocations == allocations);
    assert(open_db(&config) == A_STATUS_OK);
    snapshot_check(A_STATUS_OK);
    close_db();

    /* 对初始化的每次写入分别注入故障，重开后不得接受不兼容旧值。 */
    memcpy(checkpoint, memory, sizeof(memory));
    fields[0].type = ALIB_DATA_U32;
    before = writes;
    assert(open_db(&config) == A_STATUS_OK);
    write_steps = writes - before;
    assert(write_steps > 1U);
    snapshot_check(A_STATUS_NOT_FOUND);
    close_db();
    for (unsigned step = 1U; step <= write_steps; ++step) {
        memcpy(memory, checkpoint, sizeof(memory));
        fail_write_at = writes + step;
        assert(open_db(&config) == A_STATUS_TIMEOUT && kv == NULL);
        assert(database_allocations == allocations);
        fail_write_at = 0U;
        assert(open_db(&config) == A_STATUS_OK);
        snapshot_check(A_STATUS_NOT_FOUND);
        unaffected_check();
        snapshot_save();
        close_db();
        before = writes;
        assert(open_db(&config) == A_STATUS_OK && writes == before);
        snapshot_check(A_STATUS_OK);
        close_db();
    }
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(database_allocations == 0U);
    puts("SIG layout markers: init-only checks, raw snapshots, isolation and write faults passed");
    return 0;
}
