# 生成的 TEST 测点定义

本目录的 IDU_test.inc、FAN_test.inc 由
[config/points.json](../config/points.json) 生成，各含 50 个测点，不再手工维护。
JSON 结构及命令见 [生成说明](../config/README.md)。

保留 X-Macro 表达只读定义，供 .c 展开字段数组与静态 RAM 绑定：

- ABUS_SIG 定义普通测点；ABUS_BOUND_SIG 表示应用提供已有 RAM 绑定。
- ABUS_PARAMS、ABUS_PARAM 定义 STRUCT 字段；ABUS_PARAM_REF 引用字段数组。
- 类型、下标、Key、字段枚举直接生成在 inc/<设备>_sig_table.h。

纯静态模式为 managed 项生成存储；动态模式由 aBus 为未绑定项分配数据池。
IDU TEST_001 的演示绑定仍由 test_task.c 提供；FAN TEST_001 和产品构建的
IDU TEST_001 绑定在 protocol_tables.c。

.inc 不单独编译，无头文件保护；展开者负责定义和清理宏。
范围、默认值及定义具有静态生命周期。主从协议使用生成的普通 C 数组，
运行时不依赖 JSON、Python 或 Mako。
