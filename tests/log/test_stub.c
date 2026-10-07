#include "aLog.h"
#include <assert.h>

int main(void)
{
    unsigned evaluated = 0U;
    aLogConfig_t config;
    aLogStats_t stats;

    aLogConfigStructInit(&config);
    assert(aLogInit(&config) == A_STATUS_OK);
    assert(ALOG_ERROR("test", "%u", ++evaluated) == A_STATUS_OK);
    assert(ALOG_INFO("test", "%u", ++evaluated) == A_STATUS_OK);
    assert(ALOG_HEXDUMP("test", NULL, ++evaluated) == A_STATUS_OK);
    assert(evaluated == 0U);
    assert(aLogGetStats(&stats) == A_STATUS_OK);
    assert(stats.output_records == 0U && stats.dropped_records == 0U);
    assert(aLogDeInit() == A_STATUS_OK);
    return 0;
}
