#include "aLog.h"

void aLogConfigStructInit(aLogConfig_t *config)
{
    if (config == NULL) return;
    config->output = NULL;
    config->context = NULL;
    config->level = ALOG_LEVEL_INFO;
    config->color = A_FALSE;
    config->lock_timeout = A_TIMEOUT_NO_WAIT;
}
