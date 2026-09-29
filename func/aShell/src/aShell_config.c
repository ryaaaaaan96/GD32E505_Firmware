#include "aShell.h"

void aShellConfigStructInit(aShellConfig_t *config)
{
    if (config == NULL) return;
    aStreamStructInit(&config->stream);
    config->read_timeout = A_TIMEOUT_NO_WAIT;
    config->write_timeout = A_TIMEOUT_MS(100U);
}

