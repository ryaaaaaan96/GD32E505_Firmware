/* 复用字节级 NOR 模型，整条链路使用实际 SFUD 和 FlashDB 官方源码。 */
#define main flash_model_existing_main
#include "../flash25q/test_flash25q.c"
#undef main
#include "memory_config.h"
#include "aDataBase_instance.h"
#include <assert.h>

static aDevFlash25qHandle_t *test_flash;

aStatus_t appSystemFlashGetInfo(aDevFlash25qInfo_t *info)
{
    return aDevFlash25qGetInfo(test_flash, info);
}
aStatus_t appSystemFlashRead(const aDevFlash25qReadRequest_t *request)
{
    return aDevFlash25qRead(test_flash, request);
}
aStatus_t appSystemFlashWrite(const aDevFlash25qWriteRequest_t *request)
{
    return aDevFlash25qWrite(test_flash, request);
}
aStatus_t appSystemFlashErase(const aDevFlash25qEraseRequest_t *request)
{
    return aDevFlash25qErase(test_flash, request);
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
    aDevFlash25qBusConfig_t bc;
    aDevFlash25qBus_t bus;
    aDevFlash25qConfig_t fc;
    aDevFlash25qHandle_t flash;
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
    aDevFlash25qBusConfigStructInit(&bc);
    bc.spi.spiId = ADRV_SPI_1;
    assert(aDevFlash25qBusInitStatic(&bc, &bus) == A_STATUS_OK);
    aDevFlash25qConfigStructInit(&fc);
    fc.bus = &bus;
    fc.cs_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 12);
    assert(aDevFlash25qInitStatic(&fc, &flash) == A_STATUS_OK);
    test_flash = &flash;
    assert(appSystemMemoryInit() == A_STATUS_OK);
    assert(aDataBaseInit() == A_STATUS_OK);
    aDataBaseKvConfigStructInit(&kc);
    kc.name = "kv";
    kc.partition = "param";
    kc.format_if_needed = A_TRUE;
    aDataBaseTsConfigStructInit(&tc);
    tc.name = "ts";
    tc.partition = "log";
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
    assert(aDevFlash25qDeInitStatic(&flash) == A_STATUS_OK);
    assert(aDevFlash25qBusDeInitStatic(&bus) == A_STATUS_OK);
    assert(allocation_count == 0U);
    puts("FlashDB → aMemory → Flash25q → SFUD → SPI 整条链路验证通过");
    return 0;
}
