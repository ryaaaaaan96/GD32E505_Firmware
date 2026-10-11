# TEST 测点清单

每个设备只有一份清单：`IDU_test.inc`（50 个 SIG）、`FAN_test.inc`（50 个 SIG）。
不保留旧业务测点，也不单独维护 test_list 或 params 文件。

- `ABUS_SIG(名称, 固定Key, C存储类型, 描述...)`：普通测点。
- `ABUS_BOUND_SIG(名称, 固定Key, C存储类型, 描述...)`：已有 RAM 绑定的测点。
- `ABUS_PARAMS`、`ABUS_PARAM`：组合类型的字段描述。
- `ABUS_PARAM_REF`：SIG 引用字段数组及其数量。

两种 SIG 宏在索引、Key 和定义展开时等价。纯静态模式按清单的 C 类型为普通项生成存储，
跳过已有绑定项；动态模式由 aBus 为未绑定项分配一个连续数据池。
当前 TEST_001 的外部绑定由 test_task.c、protocol_tables.c 分别提供。
清单无头文件保护、不单独编译，展开者负责定义和清理宏。

同一份清单生成连续索引、固定 Key 和只读描述；字段名称同样由清单生成。
修改顺序会改变 sigIndex/paramIndex，固定 Key 不跟随顺序改变。
规则、范围与默认值在文件作用域展开，生命周期覆盖实例。

当前组合 SIG 使用 Modbus 整组映射，字段清单必须按 offset 连续覆盖结构体，
每项占偶数字节。C 类型头使用 aLib 断言固定大小和偏移；protocolBusConfigInit
主动调用 aBusSigParamSizeCheck 检查长度总和，失败返回错误且不发布配置。
协议层另外检查连续性及 quantity。调换字段描述顺序会被整组检查拒绝；
修改 C 成员布局则需要同步核对寄存器地址和已保存的二进制数据。

构建时 `scripts/check_sig.py` 检查每个设备的名称及 Key 唯一性。
Key 必须是无前导零的十进制或十六进制整数，范围 0～65535。
不同设备允许使用同一个 Key。检查器支持引用其他清单，但当前无需拆文件。
条件裁剪及 C 表达式不由检查器求值，产品清单保持无条件声明。

协议映射和采集数组直接写在 `IDU_modbus_slave.c`、`FAN_modbus_master.c`，
引用此处生成的 SIG/字段枚举，不使用映射展开宏。持久化选择仍放在
`persistence/sig_persist.inc`。JSON 输入预留在 config/，当前尚未提供生成器。
完整测点及调试方式见 [产品协议说明](../README.md)。
