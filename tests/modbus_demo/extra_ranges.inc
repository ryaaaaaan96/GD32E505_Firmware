/* 仅测试副本使用：同一区域增加稀疏段，并在输入寄存器区复用 Counter。 */
AMODBUS_MAPS(SPARSE,
    AMODBUS_MAP(COUNTER_ALIAS,
        .address = 16U,
        .flags = AMODBUS_ACCESS_READ,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_COUNTER, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
    AMODBUS_MAP(SPEED_ALIAS,
        .address = 20U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, FAN_MOTOR_SPEED},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
)
AMODBUS_RANGE(SPARSE,
    .area = AMODBUS_AREA_HOLDING_REGISTERS,
    .address = 16U,
    .quantity = 6U,
    .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
    AMODBUS_MAP_REF(SPARSE)
)
AMODBUS_MAPS(INPUT,
    AMODBUS_MAP(COUNTER_INPUT,
        .address = 0U,
        .flags = AMODBUS_ACCESS_READ,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_COUNTER, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
)
AMODBUS_RANGE(INPUT,
    .area = AMODBUS_AREA_INPUT_REGISTERS,
    .address = 0U,
    .quantity = 2U,
    .flags = AMODBUS_ACCESS_READ,
    AMODBUS_MAP_REF(INPUT)
)
