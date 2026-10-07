#include "aDataBase_instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t media[2][8192];
static unsigned accesses;

static aStatus_t read_data(void *context,
                           const aMemoryReadRequest_t *request)
{
    ++accesses;
    memcpy(request->data, (uint8_t *)context + request->address, request->size);
    return A_STATUS_OK;
}
static aStatus_t write_data(void *context,
                            const aMemoryWriteRequest_t *request)
{
    uint8_t *target = (uint8_t *)context + request->address;
    const uint8_t *source = request->data;

    ++accesses;
    for (size_t i = 0U; i < request->size; ++i) {
        assert((target[i] & source[i]) == source[i]);
        target[i] &= source[i];
    }
    return A_STATUS_OK;
}
static aStatus_t erase_data(void *context,
                            const aMemoryEraseRequest_t *request)
{
    ++accesses;
    memset((uint8_t *)context + request->address, 0xFF, request->size);
    return A_STATUS_OK;
}
static const aMemoryOps_t ops = {
    .read = read_data, .write = write_data, .erase = erase_data
};

int main(void)
{
    aMemoryDevice_t devices[2] = {
        { "chip-a", media[0], &ops, { 8192U, 1U, 1U, 4096U, 1U, 0xFFU } },
        { "chip-b", media[1], &ops, { 8192U, 1U, 1U, 4096U, 1U, 0xFFU } }
    };
    aMemoryPartition_t parts[2] = {
        { "db-a", &devices[0], 0U, 8192U, AMEMORY_ACCESS_ALL },
        { "db-b", &devices[1], 0U, 8192U, AMEMORY_ACCESS_ALL }
    };
    const aMemoryDevice_t *device_table[] = { &devices[0], &devices[1] };
    const aMemoryPartition_t *part_table[] = { &parts[0], &parts[1] };
    aMemoryConfig_t memory_config;
    aDataBaseKvConfig_t config;
    aDataBaseKvHandle_t *handles[2];
#if !ADATABASE_DYNAMIC_ENABLE
    aDataBaseKvHandle_t instances[2];
#endif
    aDataBaseKvSetRequest_t set;
    aDataBaseKvGetRequest_t get;
    uint32_t values[] = { 123U, 456U };
    uint32_t output;
    size_t size;

    memset(media, 0xFF, sizeof(media));
    aMemoryConfigStructInit(&memory_config);
    memory_config.devices = device_table;
    memory_config.device_count = 2U;
    memory_config.partitions = part_table;
    memory_config.partition_count = 2U;
    assert(aMemoryInit(&memory_config) == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    aDataBaseKvConfigStructInit(&config);
    aDataBaseKvSetRequestStructInit(&set);
    aDataBaseKvGetRequestStructInit(&get);
    set.key = "same-key";
    set.size = sizeof(values[0]);
    get.key = set.key;
    get.data = &output;
    get.capacity = sizeof(output);
    get.size_out = &size;
    for (unsigned pass = 0U; pass < 2U; ++pass) {
        for (size_t i = 0U; i < 2U; ++i) {
            config.name = parts[i].name;
            config.partition = parts[i].name;
            config.format_if_needed = pass == 0U;
#if ADATABASE_DYNAMIC_ENABLE
            assert(aDataBaseKvCreate(&config, &handles[i]) == A_STATUS_OK);
#else
            handles[i] = &instances[i];
            assert(aDataBaseKvInitStatic(&config, handles[i]) == A_STATUS_OK);
#endif
            if (pass == 0U) {
                set.data = &values[i];
                assert(aDataBaseKvSet(handles[i], &set) == A_STATUS_OK);
            }
            assert(aDataBaseKvGet(handles[i], &get) == A_STATUS_OK);
            assert(output == values[i] && size == sizeof(output));
        }
        assert(aMemoryDeInit() == A_STATUS_BUSY);
        assert(aDataBaseDeInit() == A_STATUS_BUSY);
        for (size_t i = 0U; i < 2U; ++i) {
#if ADATABASE_DYNAMIC_ENABLE
            assert(aDataBaseKvDestroy(handles[i]) == A_STATUS_OK);
#else
            assert(aDataBaseKvDeInitStatic(handles[i]) == A_STATUS_OK);
#endif
        }
    }
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    /* 与当前 FlashDB 编程位数不同的介质，在任何擦写前拒绝打开。 */
    devices[1].geometry.program_bits = 8U;
    assert(aMemoryInit(&memory_config) == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    accesses = 0U;
#if ADATABASE_DYNAMIC_ENABLE
    assert(aDataBaseKvCreate(&config, &handles[1]) == A_STATUS_NOT_READY);
    assert(handles[1] == NULL);
#else
    assert(aDataBaseKvInitStatic(&config, handles[1]) == A_STATUS_NOT_READY);
#endif
    assert(accesses == 0U);
    assert(aDataBaseDeInit() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    puts("FlashDB 多设备隔离、重开持久化及介质粒度检查通过");
    return 0;
}
