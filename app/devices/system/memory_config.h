#ifndef APP_MEMORY_CONFIG_H
#define APP_MEMORY_CONFIG_H

#include "aMemory.h"

/* 设备完成探测后，校验本产品布局并注册到 aMemory；不擦写介质。 */
aStatus_t appSystemMemoryInit(void);

#endif
