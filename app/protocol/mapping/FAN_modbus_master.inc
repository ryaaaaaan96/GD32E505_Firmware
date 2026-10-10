/* 主站采集清单：远端地址和站号独立配置，目标引用生成的 SIG/字段名称。
 * 清单顺序决定轮询顺序；多次展开，不加头文件保护。
 */
AMODBUS_POLL(SPEED,
    .unit_id = APP_MODBUS_MASTER_TARGET_ID,
    .area = AMODBUS_AREA_HOLDING_REGISTERS,
    .address = 2U,
    .target = {FAN_SIG_DEVICE_ID, FAN_SIG_MOTOR, FAN_MOTOR_SPEED},
    .word_order = AMODBUS_WORD_HIGH_FIRST,
    .timeout = {.milliseconds = 500U, .type = A_TIMEOUT_TYPE_RELATIVE}
)
