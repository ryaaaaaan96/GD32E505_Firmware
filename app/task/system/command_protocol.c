#include "aShell.h"
#include "protocol.h"
#include <string.h>

/* 仅触发事件，不在 Shell 任务中执行阻塞式 Modbus 收发。 */
static int protocol_command(int argc, char **argv)
{
    aStatus_t status;
    if (argc != 2 || strcmp(argv[1], "refresh") != 0) {
        ASHELL_REPLY("protocol refresh\r\n");
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

ASHELL_CMD_EXPORT(protocol, protocol_command, "Request a fresh polling round");
