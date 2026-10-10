# 测点清单

本目录存放 IDU、FAN 的 X-Macro 测点清单，作为当前点表的唯一描述来源：

- `IDU_sig.inc`：IDU 本板 Counter，固定 Key 42。
- `FAN_sig.inc`：FAN 的 Motor 副本，固定 Key 1001，包含转速和温度字段。

每项使用 `ABUS_SIG(名称, 固定Key, 描述...)`；名称在设备头文件中生成
连续索引、数量及 Key 常量，在 protocol.c 中生成只读 aBusSig_t 数组。
描述使用 C 指定初始化，STRUCT 的字段由命名组集中声明：

```c
ABUS_PARAMS(FAN_MOTOR,
    ABUS_PARAM(FAN_MOTOR_SPEED,
        .offset = offsetof(FANMotor_t, speed),
        .type = ALIB_DATA_U32,
        .size = sizeof(uint32_t)
    )
    ABUS_PARAM(FAN_MOTOR_TEMPERATURE,
        .offset = offsetof(FANMotor_t, temperature),
        .type = ALIB_DATA_S32,
        .size = sizeof(int32_t)
    )
)
```

该示例省略范围；实际范围仍在 FAN_sig.inc 中定义。头文件由组生成
FAN_MOTOR_SPEED、FAN_MOTOR_TEMPERATURE 和 FAN_MOTOR_PARAM_COUNT，
protocol.c 生成字段数组。SIG 内填写 `ABUS_PARAM_REF(FAN_MOTOR)`，
自动引用数组并计算字段数量。每组至少一个字段，没有字段则省略组及引用。
字段名包含设备/SIG 前缀，所有公开名称必须唯一；组名在装配编译单元内唯一。
协议引用生成的字段名称，调整字段清单顺序无需手工修改映射的 paramIndex。
Shell 输入的数字 paramIndex 仍随字段顺序变化。

清单没有头文件保护，不单独编译，展开宏由包含者提供并在使用后清理。
规则和默认值只在文件作用域生成，保持静态存储期。

新增测点通常只需增加清单项。纯静态模式还须注册对应 RAM；动态模式会
为未绑定测点分配数据。重复 Key 校验没有新增自动工具，协议地址及轮询项
由 [mapping](../mapping/README.md) 的独立清单明确指定。
完整约定见 [一份清单生成点表](../README.md#一份清单生成点表)。

后续 JSON 输入放在 [config](../config/README.md)，由工具生成本目录的 `.inc`
及 mapping/ 中的协议清单。当前尚未定义 JSON 格式或实现生成器，清单暂由手工维护。

生成的通信配置直接使用 aModbusServiceConfig_t；任务栈、优先级和轮询周期
由应用填写，任务由 protocol.c 创建。
后续生成器应统一维护设备 .c 的 const 配置定义和 protocol.c 的 extern 声明，
保持名称、类型、限定符及裁剪条件一致，具体约定见
[配置对象的 extern 约定](../README.md#配置对象的-extern-约定)。
设备的 sig_table.h 只提供设备号、生成的测点标识和共享数据类型。
点表在 protocol.c 中定义为 static const 并统一挂载，没有跨文件对象声明。
RAM 绑定在变量所属的 .c 中直接注册，不由清单重复创建。

仓库根目录的 `config/aclass_config.cmake` 继续负责模块和构建选项。
