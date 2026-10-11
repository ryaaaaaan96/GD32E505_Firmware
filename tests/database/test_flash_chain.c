/* 复用字节级 NOR 模型，整条链路使用实际 SFUD 和 FlashDB 官方源码。 */
#define main flash_model_existing_main
#include "../flash25q/test_flash25q.c"
#undef main
#include "system_device.h"
#include "aDataBase_instance.h"
#include <assert.h>

/* 本测试仅执行存储链路，不启动系统状态灯。 */
aStatus_t aDevLedInitStatic(const aDevLedConfig_t *config,
                           aDevLedHandle_t *handle)
{
    (void)config;
    (void)handle;
    assert(0);
    return A_STATUS_ERROR;
}

static size_t records;
static aBool_t verify_record(const aDataBaseTsRecord_t *record, void *context)
{
    assert(record->timestamp == 100 && record->size == 4U);
    assert(memcmp(record->data, context, 4U) == 0);
    ++records;
    return A_TRUE;
}

int main(void)
{
    aMemoryConfig_t memory_config;
    aDataBaseKvConfig_t kc;
    aDataBaseTsConfig_t tc;
    aDataBaseKvHandle_t kv;
    aDataBaseTsHandle_t ts;
    aDataBaseKvSetRequest_t set;
    aDataBaseKvGetRequest_t get;
    aDataBaseTsAppendRequest_t append;
    aDataBaseTsIterateRequest_t iterate;
    uint32_t value = UINT32_C(0x12345678), output;
    size_t length;

    memset(memory, 0xFF, sizeof(memory));
    appSystemMemoryConfigInit(&memory_config);
    /* 实际产品回调的失败回滚也必须释放总线和 OS 对象。 */
    fail_mutex = 1;
    assert(aMemoryInit(&memory_config) == A_STATUS_NO_MEMORY);
    assert(allocation_count == 0U);
    fail_mutex = 0;
    unknown_id = 1;
    assert(aMemoryInit(&memory_config) == A_STATUS_NOT_FOUND);
    assert(aMemoryFind("param") == NULL && allocation_count == 0U);
    unknown_id = 0;
    assert(aMemoryInit(&memory_config) == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    appSystemDatabaseKvConfigInit(&kc);
    kc.format_if_needed = A_TRUE;
    appSystemDatabaseTsConfigInit(&tc);
    tc.format_if_needed = A_TRUE;
    assert(aDataBaseKvInitStatic(&kc, &kv) == A_STATUS_OK);
    assert(aDataBaseTsInitStatic(&tc, &ts) == A_STATUS_OK);
    aDataBaseKvSetRequestStructInit(&set);
    set.key = "number";
    set.data = &value;
    set.size = sizeof(value);
    assert(aDataBaseKvSet(&kv, &set) == A_STATUS_OK);
    aDataBaseKvGetRequestStructInit(&get);
    get.key = set.key;
    get.data = &output;
    get.capacity = sizeof(output);
    get.size_out = &length;
    assert(aDataBaseKvGet(&kv, &get) == A_STATUS_OK);
    assert(output == value && length == sizeof(value));
    aDataBaseTsAppendRequestStructInit(&append);
    append.timestamp = 100;
    append.data = &value;
    append.size = sizeof(value);
    assert(aDataBaseTsAppend(&ts, &append) == A_STATUS_OK);
    assert(aDataBaseKvDeInitStatic(&kv) == A_STATUS_OK);
    assert(aDataBaseTsDeInitStatic(&ts) == A_STATUS_OK);
    kc.format_if_needed = A_FALSE;
    tc.format_if_needed = A_FALSE;
    assert(aDataBaseKvInitStatic(&kc, &kv) == A_STATUS_OK);
    assert(aDataBaseTsInitStatic(&tc, &ts) == A_STATUS_OK);
    assert(aDataBaseKvGet(&kv, &get) == A_STATUS_OK && output == value);
    aDataBaseTsIterateRequestStructInit(&iterate);
    iterate.buffer = &output;
    iterate.capacity = sizeof(output);
    iterate.callback = verify_record;
    iterate.context = &value;
    assert(aDataBaseTsIterate(&ts, &iterate) == A_STATUS_OK && records == 1U);
    assert(aDataBaseKvDeInitStatic(&kv) == A_STATUS_OK);
    assert(aDataBaseTsDeInitStatic(&ts) == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(aMemoryInit(&memory_config) == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(allocation_count == 0U);
    puts("FlashDB → aMemory → Flash25q → SFUD → SPI 整条链路验证通过");
    return 0;
}
