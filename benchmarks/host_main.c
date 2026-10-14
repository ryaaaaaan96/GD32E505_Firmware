#include "bench_bus.h"
#include "host_port.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void benchPlanRun(void);

static void report(const benchResult_t *r)
{
    printf("{\"kind\":\"bus\",\"operation\":\"%s\",\"points\":%zu,"
        "\"bytes\":%zu,\"samples\":%u,\"batch\":%u,\"min_ns\":%u,"
        "\"median_ns\":%u,\"p99_ns\":%u,\"max_ns\":%u,"
        "\"clock_pair_ns\":%u,\"checksum\":%u,\"peak_requested_bytes\":%zu}\n",
        r->operation, r->points, r->bytes, r->samples, r->batch,
        r->minimum, r->median, r->p99, r->maximum, r->clock_pair,
        r->checksum, benchMemoryPeak());
}

int main(int argc, char **argv)
{
    const size_t counts[] = {16U, 128U, 1024U};
    const size_t sizes[] = {4U, 32U, 128U};
    aBool_t quick = argc == 2 && strcmp(argv[1], "--quick") == 0;
    benchBusConfig_t config = {
        .samples = quick ? 16U : 256U, .batch = 32U,
        .clock = benchClock, .report = report
    };
    for (size_t n = 0U; n < sizeof(counts) / sizeof(counts[0]); n++) {
        for (size_t s = 0U; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
            config.points = counts[n];
            config.bytes = sizes[s];
            benchMemoryResetPeak();
            aStatus_t status = benchBusRun(&config);
            assert(status == A_STATUS_OK && benchMemoryLive() == 0U);
        }
    }
    benchPlanRun();
    assert(benchMemoryLive() == 0U);
    return 0;
}
