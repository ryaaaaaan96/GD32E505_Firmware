#ifndef APP_IDU_SIG_TABLE_H
#define APP_IDU_SIG_TABLE_H

/* IDU 本板设备号；与 FAN 表共用应用配置中的实例命名空间。 */
enum {
    IDU_SIG_DEVICE_ID = 1U
};

/* 同一清单生成连续下标和固定 Key，新增测点只修改清单。 */
#define ABUS_PARAMS(group, ...)
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
#undef ABUS_PARAMS

/* 当前清单只有标量；新增 STRUCT 字段组时也由同一规则生成名称。 */
#define ABUS_SIG(name, key_value, ...)
#define ABUS_PARAM(name, ...) name,
#define ABUS_PARAMS(group, ...) enum { __VA_ARGS__ group##_PARAM_COUNT };
#include "../sig/IDU_sig.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

#endif
