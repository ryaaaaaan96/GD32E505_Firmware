/* 故障只作用于测试链接：证明基准能拒绝空操作、错目标和数据损坏。 */
#include "bench_bus.h"
#include "host_port.h"
#include <assert.h>
#include <stdlib.h>

static unsigned fault;
static unsigned reports;

aStatus_t __real_aBusSetByIndex(aBusHandle_t *, const aBusSetIndexRequest_t *);
aStatus_t __real_aBusSetByKey(aBusHandle_t *, const aBusSetKeyRequest_t *);
aStatus_t __real_aBusGetByIndex(aBusHandle_t *, const aBusGetIndexRequest_t *);
aStatus_t __real_aBusGetByKey(aBusHandle_t *, const aBusGetKeyRequest_t *);

aStatus_t __wrap_aBusSetByIndex(aBusHandle_t *bus,
    const aBusSetIndexRequest_t *request)
{
    if (fault == 1U) return A_STATUS_OK;
    aBusSetIndexRequest_t copy = *request;
    if (fault == 5U) copy.sigIndex = 0U;
    return __real_aBusSetByIndex(bus, &copy);
}

aStatus_t __wrap_aBusSetByKey(aBusHandle_t *bus,
    const aBusSetKeyRequest_t *request)
{
    if (fault == 2U) return A_STATUS_OK;
    aBusSetKeyRequest_t copy = *request;
    if (fault == 6U) copy.sigKey = 0U;
    if (fault == 9U && copy.sigKey != 0U) {
        /* 正确目标也写入，但额外破坏前面已经写过的测点。 */
        copy.sigKey = 0U;
        aStatus_t status = __real_aBusSetByKey(bus, &copy);
        if (status != A_STATUS_OK) return status;
        copy = *request;
    }
    return __real_aBusSetByKey(bus, &copy);
}

aStatus_t __wrap_aBusGetByIndex(aBusHandle_t *bus,
    const aBusGetIndexRequest_t *request)
{
    if (fault == 3U) return A_STATUS_OK;
    aStatus_t status = __real_aBusGetByIndex(bus, request);
    if (status == A_STATUS_OK && fault == 8U)
        ((uint8_t *)request->dst)[request->size - 1U] ^= 1U;
    return status;
}

aStatus_t __wrap_aBusGetByKey(aBusHandle_t *bus,
    const aBusGetKeyRequest_t *request)
{
    if (fault == 4U) return A_STATUS_OK;
    aBusGetKeyRequest_t copy = *request;
    if (fault == 7U) copy.sigKey = 0U;
    return __real_aBusGetByKey(bus, &copy);
}

static void report(const benchResult_t *result)
{
    assert(result->samples == 16U);
    reports++;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    fault = (unsigned)strtoul(argv[1], NULL, 10);
    const size_t sizes[] = {1U, 4U, 32U, 128U};
    for (size_t i = 0U; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        reports = 0U;
        benchBusConfig_t config = {
            .points = 16U, .bytes = sizes[i], .samples = 16U, .batch = 8U,
            .clock = benchClock, .report = report
        };
        aStatus_t status = benchBusRun(&config);
        assert(status == (fault == 0U ? A_STATUS_OK : A_STATUS_ERROR));
        assert(fault != 0U || reports == 4U);
        assert(benchMemoryLive() == 0U);
    }
    return 0;
}
