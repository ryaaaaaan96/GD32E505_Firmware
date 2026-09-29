#include "aShell.h"
#include <assert.h>

static int disabled_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}
ASHELL_CMD_EXPORT(disabled, disabled_command, "Not linked");

int main(void)
{
    aShellConfig_t config;
    aShellOutputStats_t stats;
    int side_effect = 0;

    aShellConfigStructInit(&config);
    assert(config.stream.read == NULL && config.stream.write == NULL);
    assert(!aShellIsEnabled());
    assert(aShellInit(NULL) == A_STATUS_OK);
    assert(aShellProcess() == A_STATUS_OK);
    assert(ASHELL_PRINT("disabled %d", ++side_effect) == A_STATUS_OK);
    assert(side_effect == 0);
    assert(aShellGetOutputStats(&stats) == A_STATUS_OK);
    assert(stats.pending_bytes == 0U && stats.dropped_messages == 0U);
    assert(aShellDeInit() == A_STATUS_OK);
    return 0;
}
