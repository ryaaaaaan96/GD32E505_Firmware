# 测点清单

本目录存放 IDU、FAN 的 X-Macro 测点清单，作为当前点表的唯一描述来源：

- `IDU_sig.inc`：IDU 本板 Counter，固定 Key 42。
- `FAN_sig.inc`：FAN 的 Motor 副本，固定 Key 1001，包含转速和温度字段。

每项使用 `ABUS_SIG(名称, 固定Key, 描述...)`；名称在设备头文件中生成
连续索引、数量及 Key 常量，在 protocol.c 中生成只读 aBusSig_t 数组。
描述使用 C 指定初始化，STRUCT 的 ABUS_PARAMS 自动统计字段数量。
清单没有头文件保护，不单独编译，展开宏由包含者提供并在使用后清理。
规则和默认值只在文件作用域生成，保持静态存储期。

新增测点通常只需增加清单项。纯静态模式还须注册对应 RAM；动态模式会
为未绑定测点分配数据。重复 Key 校验、寄存器映射和轮询项不由宏自动生成。
完整约定见 [一份清单生成点表](../README.md#一份清单生成点表)。

后续 JSON 输入放在 [config](../config/README.md)，由工具生成本目录的 `.inc`
和协议目录下的通信配置。当前尚未定义 JSON 格式或实现生成器，清单暂由手工维护。

生成的通信配置直接使用 aModbusServiceConfig_t；任务栈、优先级和轮询周期
由应用填写，任务由 protocol.c 创建。
后续生成器应统一维护设备 .c 的 const 配置定义和 protocol.c 的 extern 声明，
保持名称、类型、限定符及裁剪条件一致，具体约定见
[配置对象的 extern 约定](../README.md#配置对象的-extern-约定)。
设备的 sig_table.h 只提供设备号、生成的测点标识和共享数据类型。
点表在 protocol.c 中定义为 static const 并统一挂载，没有跨文件对象声明。
RAM 绑定在变量所属的 .c 中直接注册，不由清单重复创建。

仓库根目录的 `config/aclass_config.cmake` 继续负责模块和构建选项。
