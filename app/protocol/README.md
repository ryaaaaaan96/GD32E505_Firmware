# 产品协议与点表

当前固件运行在 **IDU 板**，父板卡为 **CSU 板**，子板卡为**风扇控制板**。
风扇控制板统一简称 **FAN**；代码使用大写设备前缀，保持文件平铺。
system 只包含 `inc/protocol.h` 并调用 `protocolInit()`。

```text
app/protocol/
├── config/                # 后续 CSV/JSON 配置及代码生成的输入
├── inc/
│   ├── protocol.h          # 统一入口、RAM 绑定实例号及设备配置声明
│   ├── IDU_sig_table.h    # IDU 点表装配与静态绑定宏
│   ├── IDU_sig_ids.h      # IDU 设备号、测点下标和键
│   ├── FAN_sig_table.h    # FAN 数据类型、点表装配与绑定宏
│   └── FAN_sig_ids.h      # FAN 设备号、测点下标和键
├── protocol.c             # 挂载点表、协议实例、端口装配和任务
├── IDU_sig_table.c        # 点表类：IDU 本板 Counter
├── FAN_sig_table.c        # 点表类：FAN 转速、温度的本地副本
├── IDU_modbus_slave.c     # 通信类：IDU 对上提供的寄存器映射
└── FAN_modbus_master.c    # 通信类：IDU 访问 FAN 的采集配置
```

点表文件使用 `<设备>_sig_*`，通信配置使用 `<设备>_modbus_<角色>.c`。
主站配置以采集对象 FAN 命名；从站配置以提供接口的 IDU 命名。
master/slave 表示 IDU 在链路中的角色，不代表 FAN 自身作为主站。
物理 USART、引脚、缓冲区和 DE 配置仍在 devices。

[config](config/README.md) 预留为点表及协议配置的生成输入目录。
后续由 CSV 或 JSON 生成设备点表和通信配置；当前尚未接入生成工具，
仍使用现有 C 配置，不再单独维护 IDU 的点表和 Modbus 说明文件。

## 文件职责

| 文件 | 持有的内容 | 工作 |
| --- | --- | --- |
| `IDU_sig_table.c` | IDU 只读 SIG 定义 | 填充 IDU 表描述 |
| `FAN_sig_table.c` | FAN 只读定义、默认值和静态数据 | 填充 FAN 表描述 |
| `IDU_modbus_slave.c` | IDU 从站地址映射和任务参数 | 提供只读配置 |
| `FAN_modbus_master.c` | FAN 站号、采集项和任务参数 | 提供只读配置 |
| `protocol.c` | 持久表描述数组、协议与 RTU 实例 | 初始化、任务循环及失败回收 |
| `../devices/rs485/rs485_device.c` | USART 句柄和 TX 缓冲 | 硬件收发、ISR 回调和时基 |
| `../task/system/data_bus_service.c` | 私有 aBus handle | 通用数据访问及内部协议装配 |
| `../../func/aModbus` | 通用协议算法 | 编解码、事务、数据映射和 RTU 分帧 |

实例初始化、处理、回收函数均在 protocol.c 内部，公开初始化入口只有 protocolInit。
主从配置直接使用 aModbus.h 提供的 aModbusServiceConfig_t，
设备配置声明集中在 inc/protocol.h，不再定义应用专用的组合类型。

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

## 配置对象的 extern 约定

设备配置采用 `extern const` 跨文件共享。通用类型由 aModbus.h 定义，
具体对象分别定义在 FAN_modbus_master.c、IDU_modbus_slave.c，声明集中在
[inc/protocol.h](inc/protocol.h)：

```c
extern const aModbusServiceConfig_t FAN_modbus_master_config;
extern const aModbusServiceConfig_t IDU_modbus_slave_config;
```

定义文件和使用方均包含这份声明，让编译器检查声明与定义的类型一致性。
protocol.c 根据主从宏引用所选对象，再复制配置并装配运行资源。
声明本身不分配对象存储，也不复制配置；每个参与构建的配置对象只有一份定义。
当前配置和引用的映射、采集数组均具有静态存储期，不随函数返回失效。
多个任务仅仅读取这些只读对象，不需要为配置读取额外加锁。

目前声明在 protocol.h 中可见，约定仅供协议装配使用；system 仍只调用
protocolInit，无关业务模块不直接依赖 IDU/FAN 配置对象。这是模块使用约定，
不是编译器强制的私有访问控制。设备增多后可将声明迁入内部头文件，或由
生成器统一维护；不在不同 .c 中手写多份 extern 声明。

| 风险 | 使用要求 |
| --- | --- |
| 声明与定义类型不一致 | 定义方、使用方包含同一份声明和类型头；普通链接器不能完整检查 C 类型 |
| 条件宏或对齐选项导致结构布局不同 | 保持相关编译单元的配置宏、结构体布局和 ABI 选项一致 |
| 缺少定义、重复定义或引用了被裁剪的角色 | 每个对象仅在一个 .c 中定义；引用与主从编译条件一致，并检查最终链接 |
| 将只读配置当作运行状态修改 | 保持 const，不强制去除限定；运行状态由独立实例持有 |
| 指针目标失效或存在并发修改 | const 不递归保护指针目标；result、context 等可写目标单独约定生命周期及同步 |
| 设备配置依赖扩散 | 引用集中在 protocol.c；公共协议库不引用具体设备对象 |

只读配置不需要为消除 extern 再增加 getter 或运行时注册。未来 CSV/JSON
生成器应统一输出配置定义和配套声明，并校验符号唯一性及主从裁剪关系。
当前尚未实现生成工具，以上要求由现有源码和构建检查落实。

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

两表共用 `PROTOCOL_BUS_INSTANCE_ID = 1` 的 RAM 绑定命名空间。
每张表各自从下标 0 编号；deviceID 与 Modbus 站号属于不同命名空间。

主站读到远端值后直接写入 FAN 表，从站地址映射也直接读取 FAN 表。
不在 IDU 表再复制一份 Motor。Counter 仍由原测试任务持有并直接递增。
所有业务值均静态绑定，静态 aBus 的状态容量是两表 SIG 数量之和。

表模块通过函数填充描述，由 protocol.c 的静态数组持有到整个运行期结束。
字段、范围和默认值仍为各模块内的 const 定义；只有两个顶层表描述在 RAM。
既不导出全局点表变量，也不暴露业务数据地址或 aBus handle。

## 初始化顺序

protocolInit 在启动阶段单线程依次完成：

1. 装配 IDU 与 FAN 表描述，统一调用 dataBusInit 挂载。
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
