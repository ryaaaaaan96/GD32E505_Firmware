# 协议映射与采集清单

本目录保存参与编译的 X-Macro 清单；JSON 输入预留在
[config](../config/README.md)，数据定义位于 [sig](../sig/README.md)。
新增映射或采集项通常只修改清单，所属 `.c` 负责展开并装配服务配置。
清单不加头文件保护、不单独编译，不承担资源创建或任务运行。

| 文件 | 描述内容 | 展开位置 |
| --- | --- | --- |
| `IDU_modbus_slave.inc` | IDU 对上提供的映射组和地址段 | `IDU_modbus_slave.c` |
| `FAN_modbus_master.inc` | IDU 向 FAN 发起的采集请求 | `FAN_modbus_master.c` |

## 从站地址段与映射组

从站清单使用四种声明：

- `AMODBUS_MAPS(组名, ...)`：一组非空映射，每组生成一个只读数组。
- `AMODBUS_MAP(项名, 描述...)`：指定地址、权限、aBus 目标及字序。
- `AMODBUS_RANGE(段名, 描述...)`：指定区域、起始地址、跨度和段权限。
- `AMODBUS_MAP_REF(组名)`：在段内引用映射组，自动填写指针和映射数量。

例如为 Counter 配置一段只读保持寄存器：

```c
AMODBUS_MAPS(STATUS,
    AMODBUS_MAP(COUNTER,
        .address = 16U,
        .flags = AMODBUS_ACCESS_READ,
        .target = {IDU_SIG_DEVICE_ID, IDU_SIG_COUNTER, AMODBUS_SIG_WHOLE},
        .word_order = AMODBUS_WORD_HIGH_FIRST
    )
)
AMODBUS_RANGE(STATUS,
    .area = AMODBUS_AREA_HOLDING_REGISTERS,
    .address = 16U,
    .quantity = 2U,
    .flags = AMODBUS_ACCESS_READ,
    AMODBUS_MAP_REF(STATUS)
)
```

映射地址是区域内从零开始的绝对偏移，不是相对段起点的偏移。
映射组内按地址递增且不重叠；段按区域、地址排序，同一区域的段不能重叠。
段跨度 `.quantity` 显式填写，表示寄存器数或位数，不能用映射项数代替。
映射间允许空洞，访问空洞返回非法地址；映射权限必须受所属段权限限制。
从站初始化沿用 aModbus 的排序、范围、类型和权限校验，清单展开不会绕过检查。

一个设备可以声明多个组和多个段，不同区域允许使用相同地址。
段也可以省略 MAP_REF，改用库已有的 read/write/context 直接回调模式；
回调函数及声明由所属 `.c` 提供，不能与 maps 模式混用。
映射组不能为空；没有映射的回调段不声明空组。

## 主站采集

`AMODBUS_POLL(项名, 描述...)` 生成一条 aModbusClientSigRequest_t：
远端站号、区域、地址、目标 SIG/字段、字序和事务超时均显式填写。
增加项只需增加一次声明，poll_count 由生成的数组推导。
清单顺序决定轮询顺序，多个站号可以出现在同一清单中。

当前服务逐项读取，结束后按 `.c` 中的 interval_ms/error_delay_ms 等待；
没有自动合并相邻地址的批量请求，也没有每项独立的周期调度。
服务任务、端口和实例仍由 protocol.c 创建，库不创建任务。

## 标识、内存与生成约定

目标使用生成的 SIG 名称和字段名称，例如 FAN_SIG_MOTOR、FAN_MOTOR_SPEED。
字段调序会同步改变字段枚举与描述，映射仍指向原业务成员；协议地址独立填写。
数据类型、长度、范围仍来自 aBus，映射清单不重复定义。

组名在本编译单元内唯一；字段名称使用设备及 SIG 前缀，避免公共枚举重名。
映射项、段、采集项名称是配置标识，应在各自清单范围内唯一；当前展开不为它们
生成字符串或运行状态，也不额外检查这些名称是否重复。
后续可用同一清单生成诊断名称等辅助信息，默认只保留运行需要的只读数组。
所有展开宏在使用后 undef，不向业务头文件泄漏。

宏只完成编译期展开，不自动排序、生成协议地址或解析 JSON。
未来生成器应检查名称/Key 唯一性、目标引用、地址覆盖和主从配置一致性，
并生成 sig/、mapping/ 中的清单；当前生成器尚未实现。
