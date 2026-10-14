#include "aShell.h"
#include "protocol.h"
#include <string.h>

/* 仅触发事件，不在 Shell 任务中执行阻塞式 Modbus 收发。 */
static int protocol_command(int argc, char **argv)
{
    aStatus_t status;
    if (argc == 2 && strcmp(argv[1], "stats") == 0) {
        protocolStats_t stats;
        status = protocolGetStats(&stats);
        if (status != A_STATUS_OK) {
            ASHELL_REPLY("protocol stats failed: %d\r\n", (int)status);
            return -1;
        }
        ASHELL_REPLY("work=%lu errors=%lu timeouts=%lu max_process_ms=%lu\r\n",
            (unsigned long)stats.work_count, (unsigned long)stats.error_count,
            (unsigned long)stats.timeout_count,
            (unsigned long)stats.max_process_ms);
        ASHELL_REPLY("rx_queued=%lu rx_dropped=%lu\r\n",
            (unsigned long)stats.queued_frames,
            (unsigned long)stats.dropped_frames);
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "refresh") != 0) {
        ASHELL_REPLY("protocol refresh | stats\r\n");
        return -1;
    }
    status = protocolRefresh();
    if (status != A_STATUS_OK) {
        ASHELL_REPLY("protocol refresh failed: %d\r\n", (int)status);
        return -1;
    }
    ASHELL_REPLY("Refresh requested\r\n");
    return 0;
}

ASHELL_CMD_EXPORT(protocol, protocol_command, "Refresh polling or show stats");
