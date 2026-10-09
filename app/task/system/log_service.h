#ifndef APP_LOG_SERVICE_H
#define APP_LOG_SERVICE_H

#include "aStatus.h"

/* 由 aSystemInit 在输出端就绪后调用；输出配置由 devices/system 提供。 */
aStatus_t appLogInit(void);

#endif
