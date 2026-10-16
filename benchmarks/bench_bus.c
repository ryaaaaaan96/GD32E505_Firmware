#include "bench_bus.h"
#include "aOS.h"

#include <stdlib.h>
#include <string.h>

static int compare(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

/* 下标编码进内容，4 字节及以上可以区分所有测试测点。 */
static void pattern(uint8_t *data, size_t size, size_t index, uint8_t salt)
{
    for (size_t i = 0U; i < size; i++) {
        data[i] = (uint8_t)(index >> ((i % 4U) * 8U)) ^
                  (uint8_t)(salt + 29U * i);
    }
}

static aStatus_t read_check(aBusHandle_t *bus, size_t index,
    const uint8_t *expected, size_t size)
{
    uint8_t actual[128];
    aBusGetIndexRequest_t request = {
        .deviceID = 65534U, .sigIndex = index, .dst = actual, .size = size,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    /* 空读不能碰巧通过；读回缓冲与写入缓冲独立。 */
    for (size_t i = 0U; i < size; i++) actual[i] = (uint8_t)~expected[i];
    aStatus_t status = aBusGetByIndex(bus, &request);
    if (status != A_STATUS_OK) return status;
    return memcmp(actual, expected, size) == 0 ? A_STATUS_OK : A_STATUS_ERROR;
}

static aStatus_t measure(const benchBusConfig_t *config, aBusHandle_t *bus,
    const aBusSig_t *definitions, unsigned operation, uint32_t *samples)
{
    static const char *const names[] = {
        "get_index", "set_index", "get_key", "set_key"
    };
    uint8_t data[128];
    uint32_t checksum = 0U;
    aBusGetIndexRequest_t get = {
        .deviceID = 65534U, .dst = data, .size = config->bytes,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    aBusSetIndexRequest_t set = {
        .deviceID = 65534U, .src = data, .size = config->bytes,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    aBusGetKeyRequest_t get_key = {
        .deviceID = 65534U, .dst = data, .size = config->bytes,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    aBusSetKeyRequest_t set_key = {
        .deviceID = 65534U, .src = data, .size = config->bytes,
        .timeout = A_TIMEOUT_NO_WAIT
    };
    benchResult_t result = {
        .operation = names[operation], .points = config->points,
        .bytes = config->bytes, .samples = config->samples,
        .batch = config->batch
    };
    uint8_t expected[128];
    aStatus_t status;

    /* 每项计时前逐点验证：空写、错下标、错 Key 和短复制均应失败。 */
    for (size_t i = 0U; i < config->points; i++) {
        pattern(data, config->bytes, i, 0x17U);
        set.sigIndex = i;
        status = aBusSetByIndex(bus, &set);
        if (status != A_STATUS_OK) return status;
    }
    for (size_t i = 0U; i < config->points; i++) {
        pattern(expected, config->bytes, i,
                (operation & 1U) ? 0xA6U : 0x17U);
        for (size_t b = 0U; b < config->bytes; b++)
            data[b] = (operation & 1U) ? expected[b] : (uint8_t)~expected[b];
        get.sigIndex = set.sigIndex = i;
        get_key.sigKey = set_key.sigKey = definitions[i].sigKey;
        switch (operation) {
        case 0U: status = aBusGetByIndex(bus, &get); break;
        case 1U: status = aBusSetByIndex(bus, &set); break;
        case 2U: status = aBusGetByKey(bus, &get_key); break;
        default: status = aBusSetByKey(bus, &set_key); break;
        }
        if (status != A_STATUS_OK) return status;
        if (memcmp(data, expected, config->bytes) != 0) return A_STATUS_ERROR;
        status = read_check(bus, i, expected, config->bytes);
        if (status != A_STATUS_OK) return status;
        /* 写入不得提前覆盖下一个测点。 */
        if ((operation & 1U) && i + 1U < config->points) {
            pattern(expected, config->bytes, i + 1U, 0x17U);
            status = read_check(bus, i + 1U, expected, config->bytes);
            if (status != A_STATUS_OK) return status;
        }
    }
    /* 再检查已写区域，避免后写项破坏前面已经通过检查的值。 */
    if (operation & 1U) {
        for (size_t i = 0U; i < config->points; i++) {
            pattern(expected, config->bytes, i, 0xA6U);
            status = read_check(bus, i, expected, config->bytes);
            if (status != A_STATUS_OK) return status;
        }
    }

    for (uint32_t i = 0U; i < config->samples; i++) {
        uint32_t start = config->clock();
        samples[i] = config->clock() - start;
    }
    qsort(samples, config->samples, sizeof(*samples), compare);
    result.clock_pair = samples[config->samples / 2U];
    size_t index = 0U;
    /* 前四批预热不计入统计；计时包含请求定位、状态检查及循环开销。 */
    for (uint32_t s = 0U; s < config->samples + 4U; s++) {
        size_t first = index;
        /* 写批次每次更换数据；读取缓冲先置为预期末次读值的反码。 */
        size_t last = (first + config->batch - 1U) % config->points;
        pattern(expected, config->bytes,
                (operation & 1U) ? s : last,
                (operation & 1U) ? 0xC3U : 0x17U);
        for (size_t b = 0U; b < config->bytes; b++)
            data[b] = (operation & 1U) ? expected[b] : (uint8_t)~expected[b];
        uint32_t start = config->clock();
        for (uint32_t n = 0U; n < config->batch; n++) {
            get.sigIndex = index;
            set.sigIndex = index;
            get_key.sigKey = definitions[index].sigKey;
            set_key.sigKey = definitions[index].sigKey;
            switch (operation) {
            case 0U: status = aBusGetByIndex(bus, &get); break;
            case 1U: status = aBusSetByIndex(bus, &set); break;
            case 2U: status = aBusGetByKey(bus, &get_key); break;
            default: status = aBusSetByKey(bus, &set_key); break;
            }
            if (status != A_STATUS_OK) return status;
            checksum += data[0];
            if (++index == config->points) index = 0U;
        }
        uint32_t elapsed = config->clock() - start;
        if (s >= 4U) samples[s - 4U] = elapsed / config->batch;
        /* 校验在计时区间外，但会影响缓存状态；跨版本基准须区分工作负载。 */
        if (memcmp(data, expected, config->bytes) != 0) return A_STATUS_ERROR;
        if (operation & 1U) {
            for (uint32_t n = 0U; n < config->batch; n++) {
                status = read_check(bus, (first + n) % config->points,
                                    expected, config->bytes);
                if (status != A_STATUS_OK) return status;
            }
        }
    }
    qsort(samples, config->samples, sizeof(*samples), compare);
    result.minimum = samples[0];
    result.median = samples[config->samples / 2U];
    result.p99 = samples[(config->samples * 99U + 99U) / 100U - 1U];
    result.maximum = samples[config->samples - 1U];
    result.checksum = checksum;
    config->report(&result);
    return A_STATUS_OK;
}

aStatus_t benchBusRun(const benchBusConfig_t *config)
{
    aBusHandle_t *bus = NULL;
    aBusSig_t *definitions;
    uint32_t *samples;
    uint8_t initial[128];
    aBusTable_t table;
    aStatus_t status;
    aStatus_t cleanup;

    if (config == NULL || config->clock == NULL || config->report == NULL ||
        config->points == 0U || config->points > 2048U ||
        config->bytes == 0U || config->bytes > sizeof(initial) ||
        config->samples < 16U || config->samples > 256U ||
        config->batch == 0U || config->batch > 64U) {
        return A_STATUS_INVALID_PARAM;
    }
    definitions = aOSAlloc(config->points * sizeof(*definitions));
    if (definitions == NULL) return A_STATUS_NO_MEMORY;
    samples = aOSAlloc(config->samples * sizeof(*samples));
    if (samples == NULL) {
        aOSFree(definitions);
        return A_STATUS_NO_MEMORY;
    }
    memset(initial, 0x5A, config->bytes);
    for (size_t i = 0U; i < config->points; i++) {
        /* 奇数乘法模 2^16 产生不重复、非有序 Key，不依赖二分搜索前提。 */
        definitions[i] = (aBusSig_t){
            .sigKey = (uint16_t)(i * 4051U), .flags = ABUS_SIG_FLAG_LOCK,
            .type = config->bytes == 4U ? ALIB_DATA_U32 : ALIB_DATA_RAW,
            .size = config->bytes, .default_data = initial
        };
    }
    table = (aBusTable_t){.deviceID = 65534U, .sigs = definitions,
        .sig_count = config->points};
    status = aBusCreate(65534U, &table, 1U, &bus);
    if (status == A_STATUS_OK) {
        for (unsigned operation = 0U; operation < 4U; operation++) {
            status = measure(config, bus, definitions, operation, samples);
            if (status != A_STATUS_OK) break;
        }
    }
    cleanup = aBusDestroy(bus);
    aOSFree(samples);
    aOSFree(definitions);
    return status == A_STATUS_OK ? cleanup : status;
}
