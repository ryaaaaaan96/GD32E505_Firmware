#ifndef APP_FAN_SIG_TABLE_H
#define APP_FAN_SIG_TABLE_H

#include <stdint.h>

/* FAN 表是风扇控制板在 IDU 内的本地数据副本。
 * 此设备号属于 aBus 命名空间，与远端 Modbus 站号分别配置。
 */
enum {
    FAN_SIG_DEVICE_ID = 2U
};

/* 同一清单生成连续下标和固定 Key，描述中的类型在此阶段不展开。 */
typedef enum {
#define ABUS_SIG(name, key_value, ...) FAN_SIG_##name,
#include "../sig/FAN_sig.inc"
#undef ABUS_SIG
    FAN_SIG_COUNT
} FANSigIndex_t;

/* Key 独立于数组顺序，实际产品发布后应保持稳定。 */
enum {
#define ABUS_SIG(name, key_value, ...) FAN_SIG_##name##_KEY = (key_value),
#include "../sig/FAN_sig.inc"
#undef ABUS_SIG
};

typedef struct {
    uint32_t speed;
    int32_t temperature;
} FANMotor_t;

#endif
