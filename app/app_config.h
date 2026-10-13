#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* IDU 与 FAN 点表共用的 aBus 实例号，挂载和 RAM 绑定必须一致。 */
#define PROTOCOL_BUS_INSTANCE_ID 1U

/* 板载 RS485：串口配置、协议传输预算和分帧时序共用这些常量。
 * 当前为 8N1：1 起始位 + 8 数据位 + 1 停止位，共 10 位。
 * 修改校验位/停止位后同步修改 CHARACTER_BITS，装配时校验实际格式。 */
#define APP_RS485_BAUD_RATE 115200U
#define APP_RS485_CHARACTER_BITS 10U

/* 应用调试角色：0 为 Server（从站），1 为 Client（主站）。
 * USART2 同时只运行一种角色，不另设可与之冲突的 Server 开关。
 * 主站同时启用 test 任务的定时写入 Demo，见 app/protocol/README.md。
 * 正常调试直接修改本文件；自动化测试可通过编译宏覆盖默认值。
 * 模块能力与 Demo 总开关仍由 config/aclass_config.cmake 控制。 */
#ifndef APP_MODBUS_CLIENT_ENABLE
#define APP_MODBUS_CLIENT_ENABLE 1
#endif

/* 主站采集的远端地址，以及本机作为从站时的地址。 */
#ifndef APP_MODBUS_CLIENT_TARGET_ID
#define APP_MODBUS_CLIENT_TARGET_ID 1U
#endif
#ifndef APP_MODBUS_SERVER_UNIT_ID
#define APP_MODBUS_SERVER_UNIT_ID 1U
#endif

#if APP_MODBUS_CLIENT_ENABLE != 0 && APP_MODBUS_CLIENT_ENABLE != 1
#error "APP_MODBUS_CLIENT_ENABLE must be 0 or 1"
#endif
#if APP_MODBUS_CLIENT_TARGET_ID < 1 || APP_MODBUS_CLIENT_TARGET_ID > 247
#error "APP_MODBUS_CLIENT_TARGET_ID must be 1..247"
#endif
#if APP_MODBUS_SERVER_UNIT_ID < 1 || APP_MODBUS_SERVER_UNIT_ID > 247
#error "APP_MODBUS_SERVER_UNIT_ID must be 1..247"
#endif

#endif
