#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* IDU 与 FAN 点表共用的 aBus 实例号，挂载和 RAM 绑定必须一致。 */
enum {
    PROTOCOL_BUS_INSTANCE_ID = 1U
};

/* 应用调试角色：0 为从站，1 为主站；USART2 同时只运行一种角色。
 * 正常调试直接修改本文件；自动化测试可通过编译宏覆盖默认值。
 * 模块能力与 Demo 总开关仍由 config/aclass_config.cmake 控制。 */
#ifndef APP_MODBUS_MASTER_ENABLE
#define APP_MODBUS_MASTER_ENABLE 0
#endif

/* 主站采集的远端地址，以及本机作为从站时的地址。 */
#ifndef APP_MODBUS_MASTER_TARGET_ID
#define APP_MODBUS_MASTER_TARGET_ID 1U
#endif
#ifndef APP_MODBUS_SLAVE_UNIT_ID
#define APP_MODBUS_SLAVE_UNIT_ID 1U
#endif

#if APP_MODBUS_MASTER_ENABLE != 0 && APP_MODBUS_MASTER_ENABLE != 1
#error "APP_MODBUS_MASTER_ENABLE must be 0 or 1"
#endif
#if APP_MODBUS_MASTER_TARGET_ID < 1 || APP_MODBUS_MASTER_TARGET_ID > 247
#error "APP_MODBUS_MASTER_TARGET_ID must be 1..247"
#endif
#if APP_MODBUS_SLAVE_UNIT_ID < 1 || APP_MODBUS_SLAVE_UNIT_ID > 247
#error "APP_MODBUS_SLAVE_UNIT_ID must be 1..247"
#endif

#endif
