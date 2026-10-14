#include "aDrv_basic.h"
#include "aShell.h"
#include "bench_bus.h"
#include <string.h>

static void report(const benchResult_t *r)
{
    ASHELL_REPLY("%s points=%lu bytes=%lu cycles/op min=%lu median=%lu "
        "p99=%lu max=%lu clock_pair=%lu checksum=%lu\r\n",
        r->operation, (unsigned long)r->points, (unsigned long)r->bytes,
        (unsigned long)r->minimum, (unsigned long)r->median,
        (unsigned long)r->p99, (unsigned long)r->maximum,
        (unsigned long)r->clock_pair, (unsigned long)r->checksum);
}

static int benchmark_command(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "bus") != 0) {
        ASHELL_REPLY("bench bus\r\n");
        return -1;
    }
    aStatus_t status = aDrvCycleCounterEnable();
    if (status != A_STATUS_OK) {
        ASHELL_REPLY("Cycle counter unavailable: %d\r\n", (int)status);
        return -1;
    }
    const benchBusConfig_t config = {
        .points = 16U, .bytes = 4U, .samples = 128U, .batch = 8U,
        .clock = aDrvCycleCounterRead, .report = report
    };
    ASHELL_REPLY("CPU=%lu Hz lock=%u event=%u, 128 batches x 8 calls; "
        "interrupts/tasks remain enabled\r\n",
        (unsigned long)aDrvGetCoreClockHz(), ABUS_LOCK_MODE, ABUS_EVENT_ENABLE);
    status = benchBusRun(&config);
    if (status != A_STATUS_OK)
        ASHELL_REPLY("Benchmark failed: %d\r\n", (int)status);
    return status == A_STATUS_OK ? 0 : -1;
}

ASHELL_CMD_EXPORT(bench, benchmark_command, "Isolated aBus cycle benchmark");
