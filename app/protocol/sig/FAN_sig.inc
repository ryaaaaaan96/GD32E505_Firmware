/* 测点清单：描述、默认值及字段规则只在文件作用域展开，存储期为静态。
 * 沿用转速/温度演示，真实子板点表确定后在此替换。
 */
ABUS_PARAMS(FAN_MOTOR,
    ABUS_PARAM(FAN_MOTOR_SPEED,
        .offset = offsetof(FANMotor_t, speed),
        .type = ALIB_DATA_U32,
        .size = sizeof(uint32_t),
        .range = &(const aBusRange_t){
            .min.u32 = 0U, .max.u32 = 6000U
        }
    )
    ABUS_PARAM(FAN_MOTOR_TEMPERATURE,
        .offset = offsetof(FANMotor_t, temperature),
        .type = ALIB_DATA_S32,
        .size = sizeof(int32_t)
    )
)

ABUS_SIG(MOTOR, 1001U,
    .type = ALIB_DATA_STRUCT,
    .flags = ABUS_SIG_FLAG_LOCK,
    .size = sizeof(FANMotor_t),
    .default_data = &(const FANMotor_t){100U, 25},
    ABUS_PARAM_REF(FAN_MOTOR)
)
