#ifndef APP_IDU_SIG_TABLE_H
#define APP_IDU_SIG_TABLE_H

/* IDU 本板设备号；与 FAN 表共用应用配置中的实例命名空间。 */
enum {
    IDU_SIG_DEVICE_ID = 1U
};

/* 同一清单生成连续下标和固定 Key，新增测点只修改清单。 */
typedef enum {
#define ABUS_SIG(name, key_value, ...) IDU_SIG_##name,
#include "../sig/IDU_sig.inc"
#undef ABUS_SIG
    IDU_SIG_COUNT
} IDUSigIndex_t;

/* Key 独立于数组顺序，实际产品发布后应保持稳定。 */
enum {
#define ABUS_SIG(name, key_value, ...) IDU_SIG_##name##_KEY = (key_value),
#include "../sig/IDU_sig.inc"
#undef ABUS_SIG
};

#endif
