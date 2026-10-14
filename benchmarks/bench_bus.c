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

    for (uint32_t i = 0U; i < config->samples; i++) {
        uint32_t start = config->clock();
        samples[i] = config->clock() - start;
    }
    qsort(samples, config->samples, sizeof(*samples), compare);
    result.clock_pair = samples[config->samples / 2U];
    memset(data, 0x5A, config->bytes);
    size_t index = 0U;
    /* 前四批预热不计入统计；计时包含请求定位、状态检查及循环开销。 */
    for (uint32_t s = 0U; s < config->samples + 4U; s++) {
        uint32_t start = config->clock();
        for (uint32_t n = 0U; n < config->batch; n++) {
            aStatus_t status;
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
    }
    /* 验证实际复制内容；不可用空回调或未消费的返回值冒充读写。 */
    for (size_t i = 0U; i < config->bytes; i++) {
        if (data[i] != 0x5AU) return A_STATUS_ERROR;
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
