/* 从站清单：先声明映射组，再声明地址段；多次展开，不加头文件保护。
 * 地址从零开始，组内按地址递增；地址段按区域、起始地址排序。
 */
AMODBUS_MAPS(HOLDING,
    AMODBUS_MAP(COUNTER,
        .address = 0U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_COUNTER, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
    AMODBUS_MAP(FAN_SPEED,
        .address = 2U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, FAN_MOTOR_SPEED},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
    AMODBUS_MAP(FAN_TEMPERATURE,
        .address = 4U,
        .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
        .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, FAN_MOTOR_TEMPERATURE},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
)

AMODBUS_RANGE(HOLDING,
    .area = AMODBUS_AREA_HOLDING_REGISTERS,
    .address = 0U,
    .quantity = 6U,
    .flags = AMODBUS_ACCESS_READ | AMODBUS_ACCESS_WRITE,
    AMODBUS_MAP_REF(HOLDING)
)
