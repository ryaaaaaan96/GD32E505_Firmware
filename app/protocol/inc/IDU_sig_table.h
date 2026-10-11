#ifndef APP_IDU_SIG_TABLE_H
#define APP_IDU_SIG_TABLE_H

#include <stdint.h>
#include "aLayout.h"

/* IDU 本板设备号；与 FAN 表共用应用配置中的实例命名空间。 */
enum {
    IDU_SIG_DEVICE_ID = 1U
};

/* 同一清单生成连续下标和固定 Key，新增测点只修改清单。 */
#define ABUS_BOUND_SIG(...) ABUS_SIG(__VA_ARGS__)
#define ABUS_PARAMS(group, ...)
typedef enum {
#define ABUS_SIG(name, key_value, ...) IDU_SIG_##name,
#include "../sig/IDU_test.inc"
#undef ABUS_SIG
    IDU_SIG_COUNT
} IDUSigIndex_t;

/* Key 独立于数组顺序，实际产品发布后应保持稳定。 */
enum {
#define ABUS_SIG(name, key_value, ...) IDU_SIG_##name##_KEY = (key_value),
#include "../sig/IDU_test.inc"
#undef ABUS_SIG
};
#undef ABUS_PARAMS

/* 组合类型的字段名称与定义由同一清单生成。 */
#define ABUS_SIG(name, key_value, ...)
#define ABUS_PARAM(name, ...) name,
#define ABUS_PARAMS(group, ...) enum { __VA_ARGS__ group##_PARAM_COUNT };
#include "../sig/IDU_test.inc"
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

typedef uint8_t IDUTestRaw_t[8];
typedef uint8_t IDUTestBytes_t[4];
typedef struct {
    uint32_t u32_value;
    int32_t s32_value;
    uint16_t u16_value;
    IDUTestBytes_t raw_value;
    uint16_t reserved; /* 显式占据末尾字节，整组映射时必须为零。 */
} IDUTest_t;

/* 产品选择固定寄存器布局；每个字段占完整寄存器，无隐式填充。 */
ALIB_ASSERT_TYPE_SIZE(IDUTest_t, 16U);
ALIB_ASSERT_MEMBER_OFFSET(IDUTest_t, u32_value, 0U);
ALIB_ASSERT_MEMBER_OFFSET(IDUTest_t, s32_value, 4U);
ALIB_ASSERT_MEMBER_OFFSET(IDUTest_t, u16_value, 8U);
ALIB_ASSERT_MEMBER_OFFSET(IDUTest_t, raw_value, 10U);
ALIB_ASSERT_MEMBER_OFFSET(IDUTest_t, reserved, 14U);

#undef ABUS_BOUND_SIG

#endif
