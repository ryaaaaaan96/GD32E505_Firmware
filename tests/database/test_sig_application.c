/* 真实应用点表 + aBus + FlashDB：保存、重开、恢复及选择性持久化。 */
#define main database_fixture_main
#include "test_database.c"
#undef main
#include "system_init.c"
#include "IDU_sig_table.h"
#include "FAN_sig_table.h"
#include "aBus_instance.h"

static uint32_t counter;
ABUS_RAM_BIND_EXPORT(counter_binding, PROTOCOL_BUS_INSTANCE_ID,
                     IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001, counter);

static aBusSigInfo_t value_info(uint16_t device, size_t index)
{
    aBusSigInfo_t info;
    const aBusSigQuery_t query = {device, index};
    assert(dataBusGetInfo(&query, &info) == A_STATUS_OK);
    return info;
}

static void set_blob(uint16_t device, size_t index,
                     const void *data, size_t size)
{
    const aBusSetIndexRequest_t request = {
        .deviceID = device, .sigIndex = index, .src = data,
        .size = size, .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(dataBusSet(&request) == A_STATUS_OK);
}

static void get_blob(uint16_t device, size_t index, void *data, size_t size)
{
    const aBusGetIndexRequest_t request = {
        .deviceID = device, .sigIndex = index, .dst = data,
        .size = size, .timeout = A_TIMEOUT_NO_WAIT
    };
    assert(dataBusGet(&request) == A_STATUS_OK);
}

static void set_value(uint16_t device, size_t index, uint32_t value)
{
    aBusSigInfo_t info = value_info(device, index);
    uint8_t u8 = (uint8_t)value;
    uint16_t u16 = (uint16_t)value;
    const void *data = info.type == ALIB_DATA_U8 ? (const void *)&u8 :
        info.type == ALIB_DATA_U16 ? (const void *)&u16 : &value;
    set_blob(device, index, data, info.size);
}

static uint32_t get_value(uint16_t device, size_t index)
{
    aBusSigInfo_t info = value_info(device, index);
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    void *data = info.type == ALIB_DATA_U8 ? (void *)&u8 :
        info.type == ALIB_DATA_U16 ? (void *)&u16 : &u32;
    get_blob(device, index, data, info.size);
    return info.type == ALIB_DATA_U8 ? u8 :
        info.type == ALIB_DATA_U16 ? u16 : u32;
}

static void bus_close(void)
{
#if ABUS_DYNAMIC_ENABLE
    assert(aBusDestroy(bus_handle) == A_STATUS_OK);
#else
    assert(aBusDeInitStatic(bus_handle) == A_STATUS_OK);
#endif
    bus_handle = NULL;
}

/* 通过真实 KV 路径模拟旧固件留下的记录，包含正确 CRC 和索引。 */
static void legacy_record_set(size_t size)
{
    const uint8_t old_data[20U] = {0xA5U};
    const aDataBaseKvSetRequest_t request = {
        .key = "@sig:0001:07D7", /* deviceID=1、sigKey=2007。 */
        .data = old_data, .size = size, .timeout = A_TIMEOUT_MS(5000U)
    };

    assert(size <= sizeof(old_data));
    assert(aDataBaseKvSet(kv_handle, &request) == A_STATUS_OK);
}

static void legacy_restore_test(void)
{
    const size_t old_sizes[] = {12U, 20U};
    const aBusSigKeyQuery_t key = {IDU_SIG_DEVICE_ID, IDU_SIG_TEST_007_KEY};
    IDUTest_t initial, current;
    const aDataBaseSigGetRequest_t get = {
        .deviceID = IDU_SIG_DEVICE_ID, .sigIndex = IDU_SIG_TEST_007,
        .data = &current, .size = sizeof(current),
        .timeout = A_TIMEOUT_MS(5000U)
    };
    unsigned before, erase_before;

    /* 新定义变大或变小都作废旧记录，后续项及另一设备继续恢复。 */
    for (size_t i = 0U; i < sizeof(old_sizes) / sizeof(old_sizes[0]); i++) {
        legacy_record_set(old_sizes[i]);
        assert(appDatabaseClose() == A_STATUS_OK);
        assert(aDataBaseDeInit() == A_STATUS_OK);
        bus_close();
        assert(busInit() == A_STATUS_OK);
        get_blob(key.deviceID, IDU_SIG_TEST_007, &initial, sizeof(initial));
        before = writes;
        erase_before = erases;
        assert(databaseInit() == A_STATUS_OK);
        assert(writes > before && erases == erase_before);
        get_blob(key.deviceID, IDU_SIG_TEST_007, &current, sizeof(current));
        assert(memcmp(&initial, &current, sizeof(initial)) == 0);
        assert(aDataBaseSigGet(kv_handle, &get) == A_STATUS_NOT_FOUND);
        assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001) == 777U);
        assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_008) ==
               9000U + IDU_SIG_TEST_008);
        assert(get_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_002) == 1U);

        /* 删除持久生效，重开不再重复删除，也不自动保存默认值。 */
        assert(appDatabaseClose() == A_STATUS_OK);
        assert(aDataBaseDeInit() == A_STATUS_OK);
        before = writes;
        assert(databaseInit() == A_STATUS_OK && writes == before);
        assert(aDataBaseSigGet(kv_handle, &get) == A_STATUS_NOT_FOUND);
    }

    /* 显式单项恢复也作废旧记录，但保留当前 RAM，不强制恢复默认值。 */
    initial.u32_value = 4321U;
    set_blob(key.deviceID, IDU_SIG_TEST_007, &initial, sizeof(initial));
    legacy_record_set(12U);
    assert(dataBusRestore(&key) == A_STATUS_NOT_FOUND);
    get_blob(key.deviceID, IDU_SIG_TEST_007, &current, sizeof(current));
    assert(memcmp(&initial, &current, sizeof(initial)) == 0);
    assert(aDataBaseSigGet(kv_handle, &get) == A_STATUS_NOT_FOUND);

    /* 作废后可按同一键保存新格式，重开仍能完整恢复。 */
    assert(dataBusSave(&key) == A_STATUS_OK);
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    bus_close();
    assert(busInit() == A_STATUS_OK);
    assert(databaseInit() == A_STATUS_OK);
    get_blob(key.deviceID, IDU_SIG_TEST_007, &current, sizeof(current));
    assert(memcmp(&initial, &current, sizeof(initial)) == 0);

    /* 读故障不能被当成旧记录并删除，重开后旧记录仍可检出。 */
    legacy_record_set(12U);
    before = writes;
    fail_read = 1;
    assert(dataBusRestore(&key) == A_STATUS_TIMEOUT);
    fail_read = 0;
    assert(writes == before);
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(appDatabaseOpen(A_FALSE) == A_STATUS_OK);
    assert(aDataBaseSigGet(kv_handle, &get) == A_STATUS_DATA_MISMATCH);

    /* 启动时删除写入失败必须上报；介质恢复后可重试初始化。 */
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    bus_close();
    assert(busInit() == A_STATUS_OK);
    get_blob(key.deviceID, IDU_SIG_TEST_007, &initial, sizeof(initial));
    fail_write = 1;
    assert(databaseInit() == A_STATUS_TIMEOUT);
    fail_write = 0;
    get_blob(key.deviceID, IDU_SIG_TEST_007, &current, sizeof(current));
    assert(memcmp(&initial, &current, sizeof(initial)) == 0);
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(appDatabaseOpen(A_FALSE) == A_STATUS_OK);
    assert(aDataBaseSigGet(kv_handle, &get) == A_STATUS_DATA_MISMATCH);
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(databaseInit() == A_STATUS_OK);
    assert(aDataBaseSigGet(kv_handle, &get) == A_STATUS_NOT_FOUND);
}

int main(void)
{
    aBusSigKeyQuery_t key = {IDU_SIG_DEVICE_ID, IDU_SIG_TEST_010_KEY};
    dataBusConfig_t config;
    unsigned before;
    IDUTestRaw_t raw = {8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U};
    IDUTestRaw_t raw_read;
    IDUTest_t mixed = {2345U, -123, 2U, {0x11, 0x22, 0x33, 0x44}, 0U};
    IDUTest_t mixed_read;

    _Static_assert(IDU_SIG_COUNT + FAN_SIG_COUNT == 100, "Demo size");
    memset(memory, 0xFF, sizeof(memory));
    assert(memory_start() == A_STATUS_OK);
    assert(busInit() == A_STATUS_OK);
    assert(protocolBusConfigInit(&config) == A_STATUS_OK);
    assert(config.table_count == 2U);
    assert(databaseInit() == A_STATUS_OK && erases == 0U);
    assert(dataBusSave(NULL) == A_STATUS_NOT_READY);
    assert(appDatabaseOpen(A_TRUE) == A_STATUS_OK);
    /* system 装配的静态索引与动态索引均仅为选择清单分配槽位。 */
    assert(kv_handle->index.sig_capacity == PROTOCOL_PERSIST_SIG_COUNT);
    before = writes;
    for (size_t i = IDU_SIG_TEST_008; i < IDU_SIG_COUNT; i++) {
        set_value(IDU_SIG_DEVICE_ID, i, (uint32_t)(9000U + i));
    }
    set_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_002, 123U);
    set_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_003, 23456U);
    set_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_004, 3456U);
    set_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_005, (uint32_t)-456);
    set_blob(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_006, raw, sizeof(raw));
    set_blob(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_007, &mixed, sizeof(mixed));
    set_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_002, 1U);
    set_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_003, 3210U);
    set_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_004, 2U);
    set_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001, 777U);
    assert(writes == before); /* RAM 写入不产生逐次 Flash 写入。 */
    assert(dataBusSave(&key) == A_STATUS_NOT_FOUND); /* 非持久化项 */
    assert(writes == before);
    assert(dataBusSave(NULL) == A_STATUS_OK && writes > before);
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    bus_close();
    assert(busInit() == A_STATUS_OK);
    assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001) == 0U);
    assert(databaseInit() == A_STATUS_OK); /* 启动恢复，协议尚未启动。 */
    assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_001) == 777U);
    for (size_t i = IDU_SIG_TEST_008; i <= IDU_SIG_TEST_009; i++) {
        assert(get_value(IDU_SIG_DEVICE_ID, i) == 9000U + i);
    }
    assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_010) == 10U);
    assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_002) == 123U);
    assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_003) == 23456U);
    assert(get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_004) == 3456U);
    assert((int32_t)get_value(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_005) == -456);
    get_blob(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_006, raw_read, sizeof(raw_read));
    get_blob(IDU_SIG_DEVICE_ID, IDU_SIG_TEST_007,
             &mixed_read, sizeof(mixed_read));
    assert(memcmp(raw, raw_read, sizeof(raw)) == 0);
    assert(memcmp(&mixed, &mixed_read, sizeof(mixed)) == 0);
    assert(get_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_003) == 3210U);
    assert(get_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_002) == 1U);
    assert(get_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_004) == 2U);
    /* 错误的持久值必须经过总线范围校验，不能污染 RAM。 */
    {
        uint32_t bad = 6001U;
        aDataBaseSigSetRequest_t set = {
            .deviceID = FAN_SIG_DEVICE_ID,
            .sigIndex = FAN_SIG_TEST_003,
            .data = &bad, .size = sizeof(bad),
            .timeout = A_TIMEOUT_MS(5000U)
        };
        key = (aBusSigKeyQuery_t){FAN_SIG_DEVICE_ID,
                                FAN_SIG_TEST_003_KEY};
        assert(aDataBaseSigSet(kv_handle, &set) == A_STATUS_OK);
        before = writes;
        assert(dataBusRestore(&key) == A_STATUS_INVALID_PARAM);
        assert(get_value(FAN_SIG_DEVICE_ID, FAN_SIG_TEST_003) == 3210U);
        assert(writes == before); /* 范围错误不能冒充长度不兼容而删除。 */
        assert(dataBusSave(&key) == A_STATUS_OK);
    }
    legacy_restore_test();
    assert(appDatabaseClose() == A_STATUS_OK);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    bus_close();
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(database_allocations == 0U);
    puts("100 SIG application: persistence/restart/ranges/"
         "stale records passed");
    return 0;
}
