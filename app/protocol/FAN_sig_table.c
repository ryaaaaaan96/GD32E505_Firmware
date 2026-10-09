#include "FAN_sig_table.h"
#include <stddef.h>

/* 沿用现有转速/温度演示，真实子板点表确定后在本文件替换。 */
static const FANMotor_t motor_default = {100U, 25};
static const aBusRange_t speed_range = {
    .min.u32 = 0U, .max.u32 = 6000U
};
static const aBusParam_t motor_params[] = {
    {
        .offset = offsetof(FANMotor_t, speed),
        .type = ALIB_DATA_U32,
        .size = sizeof(uint32_t),
        .range = &speed_range
    },
    {
        .offset = offsetof(FANMotor_t, temperature),
        .type = ALIB_DATA_S32,
        .size = sizeof(int32_t)
    }
};

static const aBusSig_t data_sigs[FAN_SIG_COUNT] = {
    [FAN_SIG_MOTOR] = {
        .sigKey = FAN_SIG_MOTOR_KEY,
        .type = ALIB_DATA_STRUCT,
        .flags = ABUS_SIG_FLAG_LOCK,
        .size = sizeof(FANMotor_t),
        .default_data = &motor_default,
        .params = motor_params,
        .param_count = sizeof(motor_params) / sizeof(motor_params[0])
    }
};

static FANMotor_t motor_storage;
FAN_SIG_BIND(motor_binding, FAN_SIG_MOTOR, motor_storage);

aStatus_t FANSigTableInit(aBusTable_t *table)
{
    const aBusTable_t definition = {
        .sigs = data_sigs,
        .sig_count = FAN_SIG_COUNT,
        .deviceID = FAN_SIG_DEVICE_ID
    };

    if (table == NULL) return A_STATUS_INVALID_PARAM;
    *table = definition;
    return A_STATUS_OK;
}
