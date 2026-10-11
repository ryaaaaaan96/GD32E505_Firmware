#ifndef APP_FAN_SIG_TABLE_H
#define APP_FAN_SIG_TABLE_H

#include <stdint.h>
#include "aLayout.h"

/* FAN 表是风扇控制板在 IDU 内的本地数据副本。
 * 此设备号属于 aBus 命名空间，与远端 Modbus 站号分别配置。
 */
enum {
    FAN_SIG_DEVICE_ID = 2U
};

/* 同一清单生成连续下标和固定 Key，描述中的类型在此阶段不展开。 */
#define ABUS_BOUND_SIG(...) ABUS_SIG(__VA_ARGS__)
#define ABUS_PARAMS(group, ...)
typedef enum {
#define ABUS_SIG(name, key_value, ...) FAN_SIG_##name,
#include "../sig/FAN_test.inc"
#undef ABUS_SIG
    FAN_SIG_COUNT
} FANSigIndex_t;

/* Key 独立于数组顺序，实际产品发布后应保持稳定。 */
enum {
#define ABUS_SIG(name, key_value, ...) FAN_SIG_##name##_KEY = (key_value),
#include "../sig/FAN_test.inc"
#undef ABUS_SIG
};
#undef ABUS_PARAMS

/* 字段名称与描述同源；整组映射要求描述按内存偏移连续排列。 */
#define ABUS_SIG(name, key_value, ...)
#define ABUS_PARAM(name, ...) name,
#define ABUS_PARAMS(group, ...) enum { __VA_ARGS__ group##_PARAM_COUNT };
#include "../sig/FAN_test.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

typedef uint8_t FANTestRaw_t[8];

typedef struct {
    uint32_t u32_value;
    int32_t s32_value;
} FANTest_t;

ALIB_ASSERT_TYPE_SIZE(FANTest_t, 8U);
ALIB_ASSERT_MEMBER_OFFSET(FANTest_t, u32_value, 0U);
ALIB_ASSERT_MEMBER_OFFSET(FANTest_t, s32_value, 4U);

#undef ABUS_BOUND_SIG

#endif
