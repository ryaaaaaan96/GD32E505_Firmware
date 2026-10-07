#include "aLog_internal.h"
#include "aOS.h"
#include <elog.h>
#include <stdio.h>

ElogErrCode elog_port_init(void)
{
    return ELOG_NO_ERR;
}

/* 上游 elog_deinit 的内部声明返回 ElogErrCode，此处保持一致。 */
ElogErrCode elog_port_deinit(void)
{
    return ELOG_NO_ERR;
}

void elog_port_output(const char *data, size_t size)
{
    aLogPortOutput(data, size);
}

/* 上游锁关闭；外层锁覆盖过滤、格式化及输出，避免重复加锁。
 * 上游头仅私有可见，应用只能通过 aLog 入口访问。 */
void elog_port_output_lock(void) {}
void elog_port_output_unlock(void) {}

const char *elog_port_get_time(void)
{
    static char time_text[16];

    (void)snprintf(time_text, sizeof(time_text), "%lu ms",
                   (unsigned long)aOSGetUptimeMs());
    return time_text;
}

const char *elog_port_get_p_info(void) { return ""; }
const char *elog_port_get_t_info(void) { return ""; }
