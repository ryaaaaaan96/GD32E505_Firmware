#ifndef APP_IDU_SIG_IDS_H
#define APP_IDU_SIG_IDS_H

#include "protocol.h"

/* IDU 本板设备号；与 FAN 表共用 protocol 的实例命名空间。 */
enum {
    IDU_SIG_DEVICE_ID = 1U
};

/* 测点下标与表内顺序一致，用于快速访问；不作为持久化标识。 */
typedef enum {
    IDU_SIG_COUNTER,
    IDU_SIG_COUNT
} IDUSigIndex_t;

/* Key 独立于数组顺序，实际产品发布后应保持稳定。 */
enum {
    IDU_SIG_COUNTER_KEY = 42U
};

#endif
