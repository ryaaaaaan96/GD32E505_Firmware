#include "aMemory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t first[128], second[128];
static unsigned calls;
static aStatus_t forced_error;
static uint64_t last_address;

static aStatus_t read_memory(
    void *context, const aMemoryReadRequest_t *request)
{
    ++calls;
    last_address = request->address;
    if (forced_error != A_STATUS_OK) return forced_error;
    memcpy(request->data, (uint8_t *)context + request->address, request->size);
    return A_STATUS_OK;
}
static aStatus_t write_memory(
    void *context, const aMemoryWriteRequest_t *request)
{
    ++calls;
    last_address = request->address;
    memcpy((uint8_t *)context + request->address, request->data, request->size);
    return A_STATUS_OK;
}
static aStatus_t erase_memory(
    void *context, const aMemoryEraseRequest_t *request)
{
    ++calls;
    memset((uint8_t *)context + request->address, 0xFF, request->size);
    return A_STATUS_OK;
}
static const aMemoryOps_t ops = {
    .read = read_memory, .write = write_memory, .erase = erase_memory
};
AMEMORY_DEVICE_DEFINE(dev_one,
    .name = "first", .context = first, .ops = &ops,
    .geometry = { 128U, 1U, 1U, 16U, 1U, 0xFFU }
);
AMEMORY_DEVICE_DEFINE(dev_two,
    .name = "second", .context = second, .ops = &ops,
    .geometry = { 128U, 1U, 2U, 32U, 16U, 0xFFU }
);
AMEMORY_PARTITION_DEFINE(one, "one", dev_one, 16U, 32U,
                         AMEMORY_ACCESS_ALL);
AMEMORY_PARTITION_DEFINE(two, "two", dev_two, 32U, 64U,
                         AMEMORY_ACCESS_ALL);
AMEMORY_PARTITION_DEFINE(ro, "readonly", dev_one, 64U, 32U,
                         AMEMORY_ACCESS_READ);

static unsigned lifecycle_events[16], lifecycle_count;
static unsigned init_failure, deinit_failure;

static aStatus_t device_init(void *context,
                             const aMemoryGeometry_t *geometry)
{
    unsigned id = context == first ? 1U : 2U;

    assert(geometry->capacity == 128U);
    assert(aMemoryFind("one") == NULL);
    lifecycle_events[lifecycle_count++] = id;
    return id == init_failure ? A_STATUS_ERROR : A_STATUS_OK;
}

static aStatus_t device_deinit(void *context)
{
    unsigned id = context == first ? 1U : 2U;

    assert(aMemoryFind("one") == NULL);
    lifecycle_events[lifecycle_count++] = 100U + id;
    return id == deinit_failure ? A_STATUS_BUSY : A_STATUS_OK;
}

static void lifecycle_check(void)
{
    aMemoryOps_t managed_ops = ops;
    aMemoryDevice_t devices[] = { dev_one, dev_two };
    aMemoryPartition_t partitions[] = { one, two };
    const aMemoryDevice_t *device_table[] = { &devices[0], &devices[1] };
    const aMemoryPartition_t *part_table[] = {
        &partitions[0], &partitions[1]
    };
    aMemoryConfig_t config;
    aMemoryReadRequest_t request;
    const aMemoryHandle_t *handle;

    aMemoryConfigStructInit(&config);
    config.devices = device_table;
    config.device_count = 2U;
    config.partitions = part_table;
    config.partition_count = 2U;
    managed_ops.init = device_init;
    for (size_t i = 0U; i < 2U; ++i) {
        devices[i].ops = &managed_ops;
        partitions[i].device = &devices[i];
    }
    /* 定义有误时不触碰硬件；生命周期回调必须成对。 */
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    assert(lifecycle_count == 0U);
    managed_ops.deinit = device_deinit;
    partitions[1].name = "one";
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    assert(lifecycle_count == 0U);
    partitions[1].name = "two";

    /* 第二个设备部分初始化后失败，包括它自身在内逆序清理。 */
    init_failure = 2U;
    assert(aMemoryInit(&config) == A_STATUS_ERROR);
    assert(lifecycle_count == 4U);
    assert(lifecycle_events[0] == 1U && lifecycle_events[1] == 2U);
    assert(lifecycle_events[2] == 102U && lifecycle_events[3] == 101U);
    assert(aMemoryRetain() == A_STATUS_NOT_READY);
    assert(aMemoryDeInit() == A_STATUS_NOT_READY);

    /* 回滚失败保存进度，禁止重新初始化及访问，DeInit 可以重试。 */
    lifecycle_count = 0U;
    deinit_failure = 2U;
    assert(aMemoryInit(&config) == A_STATUS_BUSY);
    assert(lifecycle_count == 3U && aMemoryFind("one") == NULL);
    assert(aMemoryInit(&config) == A_STATUS_BUSY);
    deinit_failure = 0U;
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(lifecycle_count == 5U);
    assert(lifecycle_events[3] == 102U && lifecycle_events[4] == 101U);

    lifecycle_count = 0U;
    init_failure = 0U;
    assert(aMemoryInit(&config) == A_STATUS_OK);
    handle = aMemoryFind("one");
    assert(handle != NULL && lifecycle_count == 2U);
    assert(aMemoryRetain() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_BUSY && lifecycle_count == 2U);
    assert(aMemoryRelease() == A_STATUS_OK);
    deinit_failure = 1U;
    assert(aMemoryDeInit() == A_STATUS_BUSY);
    assert(lifecycle_count == 4U);
    assert(lifecycle_events[2] == 102U && lifecycle_events[3] == 101U);
    aMemoryReadRequestStructInit(&request);
    assert(aMemoryRead(handle, &request) == A_STATUS_NOT_READY);
    deinit_failure = 0U;
    assert(aMemoryDeInit() == A_STATUS_OK);
    /* 已成功释放的第二个设备不能再次释放。 */
    assert(lifecycle_count == 5U && lifecycle_events[4] == 101U);
}

static void overwrite_memory_check(void)
{
    aMemoryOps_t byte_ops = ops;
    aMemoryDevice_t device = dev_one;
    aMemoryPartition_t partition = one;
    const aMemoryDevice_t *device_table[] = { &device };
    const aMemoryPartition_t *part_table[] = { &partition };
    aMemoryConfig_t config;
    aMemoryWriteRequest_t write;
    aMemoryEraseRequest_t erase;
    const aMemoryHandle_t *handle;
    uint8_t value = 0U;

    /* 可覆盖写且无擦除操作的介质仍使用同一套区域接口。 */
    byte_ops.erase = NULL;
    device.ops = &byte_ops;
    device.geometry.erase_granularity = 0U;
    device.geometry.program_bits = 8U;
    partition.device = &device;
    partition.access = AMEMORY_ACCESS_READ | AMEMORY_ACCESS_WRITE;
    aMemoryConfigStructInit(&config);
    config.devices = device_table;
    config.device_count = 1U;
    config.partitions = part_table;
    config.partition_count = 1U;
    assert(aMemoryInit(&config) == A_STATUS_OK);
    handle = aMemoryFind("one");
    aMemoryWriteRequestStructInit(&write);
    write.data = &value;
    write.size = sizeof(value);
    assert(aMemoryWrite(handle, &write) == A_STATUS_OK);
    value = 0xFFU;
    assert(aMemoryWrite(handle, &write) == A_STATUS_OK);
    assert(first[partition.offset] == value);
    aMemoryEraseRequestStructInit(&erase);
    erase.size = 1U;
    assert(aMemoryErase(handle, &erase) == A_STATUS_UNSUPPORTED);
    assert(aMemoryDeInit() == A_STATUS_OK);
}

int main(void)
{
    const aMemoryDevice_t *devices[] = { &dev_one, &dev_two };
    const aMemoryPartition_t *parts[] = { &one, &two, &ro };
    aMemoryConfig_t config;
    aMemoryReadRequest_t read;
    aMemoryWriteRequest_t write;
    aMemoryEraseRequest_t erase;
    aMemoryInfo_t info;
    aMemoryPartition_t invalid;
    aMemoryDevice_t invalid_device;
    const aMemoryHandle_t *a, *b, *r;
    uint8_t value[4] = { 1U, 2U, 3U, 4U }, output[4];
    unsigned before;

    aMemoryConfigStructInit(&config);
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    config.devices = devices;
    config.device_count = 2U;
    config.partitions = parts;
    config.partition_count = 3U;
    /* 初始化错误不得留下部分注册。 */
    invalid = one;
    parts[2] = &invalid;
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    invalid.name = "overlap";
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    invalid.offset = UINT64_MAX;
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    invalid = ro;
    invalid.device = NULL;
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    parts[2] = &ro;
    invalid_device = dev_two;
    invalid_device.name = "first";
    devices[1] = &invalid_device;
    assert(aMemoryInit(&config) == A_STATUS_INVALID_PARAM);
    devices[1] = &dev_two;
    config.partition_count = AMEMORY_MAX_PARTITIONS + 1U;
    assert(aMemoryInit(&config) == A_STATUS_NO_MEMORY);
    config.partition_count = 3U;
    assert(aMemoryFind("one") == NULL);
    assert(aMemoryInit(&config) == A_STATUS_OK);
    assert(aMemoryInit(&config) == A_STATUS_BUSY);
    a = aMemoryFind("one");
    b = aMemoryFind("two");
    r = aMemoryFind("readonly");
    assert(a != NULL && b != NULL && r != NULL);
    assert(aMemoryFind("unknown") == NULL);
    assert(aMemoryGetInfo(b, &info) == A_STATUS_OK);
    assert(info.geometry.capacity == 64U && info.offset == 32U);
    assert(strcmp(info.device_name, "second") == 0);
    aMemoryWriteRequestStructInit(&write);
    write.data = value;
    write.size = sizeof(value);
    write.address = 4U;
    assert(aMemoryWrite(a, &write) == A_STATUS_OK && last_address == 20U);
    assert(aMemoryWrite(b, &write) == A_STATUS_OK && last_address == 36U);
    assert(memcmp(first + 20, value, 4U) == 0);
    assert(memcmp(second + 36, value, 4U) == 0);
    aMemoryReadRequestStructInit(&read);
    read.address = 4U;
    read.size = 4U;
    read.data = output;
    assert(aMemoryRead(a, &read) == A_STATUS_OK);
    assert(memcmp(output, value, 4U) == 0);
    before = calls;
    assert(aMemoryWrite(r, &write) == A_STATUS_UNSUPPORTED);
    write.address = 1U;
    assert(aMemoryWrite(b, &write) == A_STATUS_INVALID_PARAM);
    read.address = UINT64_MAX;
    assert(aMemoryRead(a, &read) == A_STATUS_INVALID_PARAM);
    read.address = 31U;
    assert(aMemoryRead(a, &read) == A_STATUS_INVALID_PARAM);
    read.address = 32U;
    read.size = 0U;
    read.data = NULL;
    assert(aMemoryRead(a, &read) == A_STATUS_OK);
    assert(calls == before);
    aMemoryEraseRequestStructInit(&erase);
    erase.size = 16U;
    assert(aMemoryErase(b, &erase) == A_STATUS_INVALID_PARAM);
    assert(aMemoryErase(r, &erase) == A_STATUS_UNSUPPORTED);
    assert(aMemoryErase(a, &erase) == A_STATUS_OK);
    for (size_t i = 16U; i < 32U; ++i) assert(first[i] == 0xFFU);
    read.address = 0U;
    read.size = 4U;
    read.data = output;
    forced_error = A_STATUS_TIMEOUT;
    assert(aMemoryRead(a, &read) == A_STATUS_TIMEOUT);
    assert(aMemoryRetain() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_BUSY);
    assert(aMemoryRelease() == A_STATUS_OK);
    assert(aMemoryDeInit() == A_STATUS_OK);
    assert(aMemoryRead(a, &read) == A_STATUS_NOT_READY);
    assert(aMemoryDeInit() == A_STATUS_NOT_READY);
    lifecycle_check();
    overwrite_memory_check();
    puts("aMemory 多设备、分区、权限、边界及生命周期测试通过");
    return 0;
}
