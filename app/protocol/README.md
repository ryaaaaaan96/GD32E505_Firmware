# 产品协议与点表

当前固件运行在 **IDU 板**，父板卡为 **CSU 板**，子板卡为**风扇控制板**。
风扇控制板统一简称 **FAN**；代码使用大写设备前缀，测点清单集中在 sig/。
system 只包含 `inc/protocol.h` 并调用 `protocolInit()`。

```text
app/protocol/
├── config/               # 后续 JSON 配置输入
├── sig/                  # 参与编译的 X-Macro 测点清单
│   ├── IDU_sig.inc        # IDU 测点清单：名称、固定 Key、描述
│   └── FAN_sig.inc        # FAN 测点清单及字段、范围、默认值
├── inc/
│   ├── protocol.h         # 统一初始化入口
│   ├── IDU_sig_table.h    # IDU 设备号和测点标识
│   └── FAN_sig_table.h    # FAN 设备号、测点标识和共享数据类型
├── protocol.c             # 展开并挂载点表、FAN 副本、协议实例与任务
├── IDU_modbus_slave.c     # 通信类：IDU 对上提供的寄存器映射
└── FAN_modbus_master.c    # 通信类：IDU 访问 FAN 的采集配置
```

点表清单使用 `<设备>_sig.inc`，配套数据头使用 `<设备>_sig_table.h`，
通信配置使用 `<设备>_modbus_<角色>.c`。
主站配置以采集对象 FAN 命名；从站配置以提供接口的 IDU 命名。
master/slave 表示 IDU 在链路中的角色，不代表 FAN 自身作为主站。
物理 USART、引脚、缓冲区和 DE 配置仍在 devices。

[sig](sig/README.md) 存放参与编译的 X-Macro 测点清单。
[config](config/README.md) 留给后续 JSON 配置输入；未来由工具生成 sig/ 下的
清单和通信配置，当前尚未接入生成工具。

## 文件职责

| 文件 | 持有的内容 | 工作 |
| --- | --- | --- |
| `sig/IDU_sig.inc` | IDU 测点名称、Key、描述 | 生成标识和 SIG 定义 |
| `sig/FAN_sig.inc` | FAN 测点及字段、默认值、范围 | 生成标识和 SIG 定义 |
| `IDU_modbus_slave.c` | IDU 从站地址映射和任务参数 | 提供只读配置 |
| `FAN_modbus_master.c` | FAN 站号、采集项和任务参数 | 提供只读配置 |
| `protocol.c` | 私有只读点表、FAN 数据副本、协议与 RTU 实例 | 初始化、任务循环及失败回收 |
| `../devices/rs485/rs485_device.c` | USART 句柄和 TX 缓冲 | 硬件收发、ISR 回调和时基 |
| `../task/system/data_bus_service.c` | 私有 aBus handle | 通用数据访问及内部协议装配 |
| `../../func/aModbus` | 通用协议算法 | 编解码、事务、数据映射和 RTU 分帧 |

实例初始化、处理、回收函数均在 protocol.c 内部，公开初始化入口只有 protocolInit。
主从配置直接使用 aModbus.h 提供的 aModbusServiceConfig_t，
通信配置的 extern 声明集中在 protocol.c，不再定义应用专用的组合类型。
点表在 protocol.c 中展开为 static const 对象，不再跨文件声明。
protocol.h 只提供 protocolInit，每个设备的 sig_table.h 只提供测点标识和
共享数据类型。这些应用头文件不引入 aBus、aModbus 或运行实例配置。

| 字段 | 使用的类型 | 职责 |
| --- | --- | --- |
| `modbus` | `aModbusConfig_t` | 协议角色、传输类型、站号、路由及字节超时 |
| `polls` / `poll_count` | `aModbusClientSigRequest_t` 数组 | 主站采集目标和事务超时 |
| `server` | `aModbusServerProcessRequest_t` | 从站每次处理的超时与可选结果输出 |
| `task` | `aOSTaskConfig_t` | 应用任务名称、栈大小及优先级 |
| `interval_ms` / `error_delay_ms` | `uint32_t` | 应用任务成功或失败后的等待时间 |

角色、站号和路由仍复用 aModbusConfig_t。设备文件填写只读模板，
protocol.c 复制完整 modbus 配置，再装配当前端口与私有总线；从站直接借用
server 请求。task 模板的入口和参数由 protocol.c 填充。映射、采集数组和
可选结果对象须在通信期间持续有效，模板本身不被修改。

当前模板显式配置 20 ms 字节超时，静态初始化不会自动应用 StructInit 的默认值。
板级端口目前只装配 RTU，填写其他传输类型时在初始化资源前返回 UNSUPPORTED；
库的 TCP 能力仍可由其他应用端口使用。组合类型由 aModbus 统一提供，
IDU/FAN 配置实例、具体参数值和任务创建仍由 app 持有。
局部配置可调用 aModbusServiceConfigStructInit 设置默认值；该函数不创建资源。

## 一份清单生成点表

每个测点只在对应设备的 `.inc` 中写一次：

```c
ABUS_SIG(COUNTER, 42U,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .size = sizeof(uint32_t)
)
```

头文件将同一清单展开两次，生成连续的 `IDU_SIG_COUNTER` 等索引、
`IDU_SIG_COUNT` 和固定的 `IDU_SIG_COUNTER_KEY`。protocol.c 再展开清单，
生成按相同顺序排列的 aBusSig_t 数组。新增已有设备的测点无需修改枚举或数量。
索引随顺序变化，固定 Key 不随顺序变化；持久化及 Shell 继续使用 Key。

STRUCT 使用 `ABUS_PARAMS(...)` 描述一个或多个字段，由宏推导 param_count。
默认值、范围、字段数组使用文件作用域的 const 复合字面量，具有静态存储期，
在当前固件中放入 Flash。没有字段时省略 ABUS_PARAMS，没有范围或默认值时
省略对应指定初始化项；默认值为空时仍由 aBus 初始化为零。

清单不设置头文件保护，也不单独参与 CMake 编译；它是多次包含的数据清单。
ABUS_SIG、ABUS_PARAMS 仅在展开期间定义，随后 undef，不作为运行时 API。
应用公共头生成枚举时丢弃描述参数，不引入 aBus、aOS 或 aModbus 头文件。
各编译单元必须使用一致的条件配置；清单中的描述不应再次指定 sigKey。
名称和同表 Key 须唯一，当前没有新增重复 Key 的构建期校验工具。

自动生成只读定义不会额外分配业务数据：Counter 仍绑定任务中的变量，
FAN 副本在 protocol.c 中绑定。新增测点可使用已有动态分配补足机制，
纯静态模式则必须为新增测点提供 RAM 绑定。协议映射仍由通信配置明确指定，
不会因新增测点自动开放寄存器或添加轮询项。

新增设备时需要提供清单和数据头，并在 protocol.c 中增加该设备的数组展开、
表描述及静态状态容量。aBus 核心始终接收普通 aBusTable_t，不依赖产品清单。

## 配置对象的 extern 约定

通信配置采用 `extern const` 跨文件共享，声明集中在
[protocol.c](protocol.c)，只由它装配；公共头文件不声明这些对象。
通信配置类型由 aModbus.h 提供，对象分别定义在 FAN_modbus_master.c、
IDU_modbus_slave.c；protocol.c 在 APP_MODBUS_ENABLE 内按主从角色声明：

```c
#if APP_MODBUS_MASTER_ENABLE
extern const aModbusServiceConfig_t FAN_modbus_master_config;
#else
extern const aModbusServiceConfig_t IDU_modbus_slave_config;
#endif
```

定义方和使用方均使用库提供的通用类型，但不再共享对象声明。
修改对象名称、类型或 const 限定时，必须同步检查定义和 protocol.c 的声明；
普通链接器不能保证发现跨文件的 C 类型不一致。
protocol.c 根据主从宏引用所选对象，再复制配置并装配运行资源。
声明本身不分配对象存储，也不复制配置；每个参与构建的配置对象只有一份定义。
当前配置和引用的映射、采集数组均具有静态存储期，不随函数返回失效。
多个任务仅仅读取这些只读对象，不需要为配置读取额外加锁。

点表不使用 extern：protocol.c 在 ABUS_ENABLE 内展开两份清单，直接定义
连续的 static const aBusTable_t 数组，再通过 dataBusInit 借用。
初始化不再复制表描述，不需要逐设备的初始化函数。

system 只包含 protocol.h 并调用 protocolInit，业务模块通过设备头文件取得
设备号、测点标识和数据类型，不会间接取得配置对象声明或协议库依赖。
通信配置对象仍具有外部链接；将声明放入 .c 不等于编译器强制的私有访问控制。
约定只有 protocol.c 引用这些对象，其他业务通过数据总线服务访问运行数据。
当前不为这两个通信配置对象新增内部头文件，也不在多个使用方重复维护声明。

| 风险 | 使用要求 |
| --- | --- |
| 声明与定义类型不一致 | 双方使用库提供的统一类型；变更时同步核对对象名称、类型和限定符，普通链接器不能完整检查 C 类型 |
| 条件宏或对齐选项导致结构布局不同 | 保持相关编译单元的配置宏、结构体布局和 ABI 选项一致 |
| 缺少定义、重复定义或引用了被裁剪的角色 | 每个对象仅在一个 .c 中定义；引用与主从编译条件一致，并检查最终链接 |
| 将只读配置当作运行状态修改 | 保持 const，不强制去除限定；运行状态由独立实例持有 |
| 指针目标失效或存在并发修改 | const 不递归保护指针目标；result、context 等可写目标单独约定生命周期及同步 |
| 设备配置依赖扩散 | 引用集中在 protocol.c；公共协议库不引用具体设备对象 |

只读配置不需要为消除 extern 再增加 getter 或运行时注册。未来 JSON
生成器应统一维护设备 .c 的配置定义和 protocol.c 的配套声明，并校验
名称、类型、符号唯一性及主从裁剪关系。当前尚未实现生成工具，声明一致性
依靠源码同步维护；构建与回归测试验证链接和实际功能，不替代完整类型检查。

开源项目也使用相同的跨文件声明机制，具体对象的访问约束仍需分别遵守：

- [Zephyr device.h](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/device.h)
  通过宏为设备树节点生成设备对象的 extern 声明，非可变设备带 const。
- [FreeRTOS heap_4.c](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/portable/MemMang/heap_4.c)
  在 configAPPLICATION_ALLOCATED_HEAP 启用时，通过 extern 引用应用定义的 ucHeap。
- [Linux jiffies.h](https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h)
  声明 jiffies 与 jiffies_64；可写计时状态仍有独立的读取和同步要求。
- 仓库中的 [nr_micro_shell.h](../../func/aShell/nr_micro_shell/inc/nr_micro_shell.h)
  通过 extern 引用 cmd_table 和 cmd_table_size。

## 双点表与数据流

当前同一个 aBus 实例挂载两张表：

| 表 | aBus deviceID | SIG | 键 |
| --- | --- | --- | --- |
| IDU | 1 | Counter，每秒递增 | 42 |
| FAN | 2 | Motor，包含 speed 和 temperature | 1001 |

两表共用 [app_config.h](../app_config.h) 中的
`PROTOCOL_BUS_INSTANCE_ID = 1`，挂载和 RAM 绑定使用同一个实例号。
每张表各自从下标 0 编号；deviceID 与 Modbus 站号属于不同命名空间。

主站读到远端值后直接写入 FAN 表，从站地址映射也直接读取 FAN 表。
不在 IDU 表再复制一份 Motor。Counter 仍由原测试任务持有并直接递增。
所有业务值均静态绑定，静态 aBus 的状态容量是两表 SIG 数量之和。

清单由 protocol.c 展开为 static const 定义，表描述、字段、范围和默认值
保持只读，不保留 RAM 表描述副本。业务值存于绑定的 RAM 对象中，
aBus handle 由数据总线服务私有持有。

RAM 绑定在变量所属的 .c 中直接使用 ABUS_RAM_BIND_EXPORT，不增加设备包装宏。
Counter 在 task/sig/sig_task.c 注册，Motor 在 protocol.c 注册；
这两个文件直接包含 aBus.h 和 app_config.h，点表头文件不承担注册职责。
绑定只关联存储，直接访问绑定变量的并发同步由应用自行决定。

## 初始化顺序

protocolInit 在启动阶段单线程依次完成：

1. 统一调用 dataBusInit，挂载编译时生成的 IDU 与 FAN 只读表。
2. 创建 Counter 静态绑定测试任务。
3. 根据调试角色选择只读通信配置，准备硬件时基和物理输出流。
4. 创建 RTU、创建 aModbus 并校验映射，然后打开 USART 接收回调。
5. 创建通信任务；所有实例已就绪，允许任务立即运行。

system 在成功返回后启动 Shell 消费任务。关闭 aBus 时不装配点表；
关闭 Modbus 时仍可初始化点表并使用 Shell。重复调用 protocolInit 返回 BUSY，
不会重写正在使用的表描述或重复创建任务。

通信初始化或任务创建失败时，依次回收协议、USART、RTU。USART 关闭 BUSY
时保留 RTU，防止接收中断访问已释放状态。已启动的其他任务不强制删除，
统一入口失败后由系统启动失败流程处理。

## 当前演示边界

仍使用一条 USART2 / RS485 端口，`app_config.h` 的主从宏二选一：

`APP_MODBUS_MASTER_ENABLE` 为 0 时运行从站，为 1 时运行主站。
修改后重新编译并烧录。

- 从站模式：IDU 对上提供地址 0～5；0/1 为 IDU Counter，2～5 为 FAN 数据。
- 主站模式：IDU 从远端站号 1、地址 2 读取转速，写入本地 FAN 表。

FAN 的转速、温度、范围与寄存器地址沿用已有演示，尚未接入真实子板点表。
父子设备同时通信需要分别配置链路、端口及运行实例，当前没有同时启动两条链路。

主站每次处理一个采集项，结束后延时 1000 ms；失败也推进到下一项，BUSY
重入不推进。从站正常处理后继续等待，失败后延时 5 ms。主从配置只决定
业务和调度参数，协议算法由库实现；aStream 的无 context 约定保持不变。

Shell 改为分别查看两张表：

```text
sig get 1
sig get 1 42
sig get 2
sig get 2 1001
sig set 2 1001 0 1500
```

旧的 `sig get 1 1001` 需改为 `sig get 2 1001`，IDU Counter 命令不变。
通用命令用法见 [应用 Shell 调试](../README.md#通用-sig-命令)。
