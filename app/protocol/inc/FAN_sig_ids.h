#ifndef APP_FAN_SIG_IDS_H
#define APP_FAN_SIG_IDS_H

#include "protocol.h"

/* FAN 表是风扇控制板在 IDU 内的本地数据副本。
 * 此设备号属于 aBus 命名空间，与远端 Modbus 站号分别配置。
 */
enum {
    FAN_SIG_DEVICE_ID = 2U
};

typedef enum {
    FAN_SIG_MOTOR,
    FAN_SIG_COUNT
} FANSigIndex_t;

enum {
    FAN_SIG_MOTOR_KEY = 1001U
};

#endif
