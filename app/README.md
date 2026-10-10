# 应用层组织

## 文件命名

应用文件的功能和职责部分使用小写下划线，设备缩写保留大写，如 IDU、CSU。
设备业务文件使用“设备名 + 类别 + 职责”，省略重复的 `app_` 前缀；
配套头文件与源文件同名。目录表达所属模块，文件名表达业务归属和职责。

| 职责 | 文件名示例 |
| --- | --- |
| 业务测点清单 | `sig/IDU_sig.inc`、`sig/FAN_sig.inc` |
| 系统服务配置与生命周期 | `database_service.c`、`log_service.c`、`data_bus_service.c` |
| 协议映射及采集清单 | `mapping/FAN_modbus_master.inc`、`mapping/IDU_modbus_slave.inc` |
| 协议角色和服务参数 | `FAN_modbus_master.c`、`IDU_modbus_slave.c` |
| 任务创建与运行循环 | `protocol.c`、`sig_task.c` |
| 板级资源配置与注册 | `rs485_device.c`、`memory_config.c`、`log_config.c` |
| 设备实例与访问入口 | `system_device.c`、`flash_device.c` |
| Shell 命令 | `data_bus_command.c`、`database_command.c`、`log_command.c` |
| 测点标识与共享数据类型 | `IDU_sig_table.h`、`FAN_sig_table.h` |
| 数据总线与协议的装配接口 | `data_bus_modbus.h` |
| 系统启动编排 | `system_init.c` |

`main.c` 和 `flash_test.c` 保留已有的职责名称。应用总配置使用
`app_config.h`，与 `main.c` 同级；这是集中配置的固定名称。
文件名不要求与接口前缀一致，头文件保护宏仍使用 `APP_` 命名空间。
protocol 按设备归属与职责命名：点表类使用 `IDU_sig_*`，
通信类使用 `IDU_modbus_slave.c` 与 `FAN_modbus_master.c`，分别表示
IDU 对上提供服务与向风扇控制板采集；FAN 为风扇控制板的统一简称。
当前固件运行在 IDU 板，上级为 CSU 板，下级为风扇控制板。
通用数据接口使用 `dataBus...`，协议绑定入口使用 `dataBusModbus...`；
IDU_sig.inc、FAN_sig.inc 提供测点的唯一清单，
头文件由清单生成索引及固定 Key，protocol.c 生成只读数组并统一挂载。
Modbus 主从文件仅提供只读业务配置，由 `app_config.h` 选择角色。
protocol.c 统一持有协议实例并创建任务，protocol 对 system 只提供 protocolInit。
通信配置对象的 extern 声明集中在 protocol.c，点表对象为本文件私有。
公共头文件只提供
初始化入口、设备标识和共享数据类型，不引入 aBus 或 aModbus 的配置定义。

## 目录职责

应用只保留三个一级目录，入口和调试配置仍与目录同级：

```text
app/
├── devices/           # 板级设备、存储和输出配置
├── task/              # 系统启动、运行任务和系统调试入口
│   └── system/        # 日志、数据库、数据总线服务和可选 Flash 测试
├── protocol/          # 点表、协议装配与通信任务创建
│   ├── inc/           # protocol.h 与产品数据头文件
│   ├── config/        # 后续 JSON 配置输入
│   ├── sig/           # IDU_sig.inc、FAN_sig.inc 测点清单
│   ├── mapping/       # 从站映射组/地址段与主站采集清单
│   ├── protocol.c         # 唯一入口、协议实例与通信任务
│   ├── FAN_modbus_master.c
│   └── IDU_modbus_slave.c
├── main.c
└── app_config.h
```

`protocol` 集中管理围绕 aBus 的应用逻辑：由 IDU_sig.inc 与 FAN_sig.inc
生成本板、风扇板的只读点表，RAM 绑定放在变量所属的 .c 中，
主从配置将协议寄存器与点表连接，完成采集和转发。通用协议实现仍在 func 层。

- 系统 console 实例随 ASHELL_ENABLE 启用或裁剪，不再单独设置应用 USART 开关。
- devices/system/system_device.h：系统设备的 LED 句柄、console 流专用初始化接口。
- devices/system/system_device.c：统一管理系统设备的 LED 与 USART 配置结构、缓冲区和私有句柄；参数直接在本文件中配置。
- task/system/system_init.c：统一管理 LED 状态任务和 Shell 任务，内部逐项初始化设备、服务并创建任务；对外仅提供 aSystemInit。
- task/system/system_init.h：声明系统服务启动入口 aSystemInit。
- devices/system/log_config.c：日志输出适配与配置，当前接 Shell 字节队列。
- task/system/log_service.c：日志服务初始化；log_command.c 提供调试命令。
- devices/system/database_config.c：数据库分区选择、超时和记录容量配置。
- task/system/database_service.c：数据库实例管理和读写；database_command.c 提供调试命令。
- app_config.h：aBus 绑定实例号、调试角色和站号配置，默认 Modbus 从站 1。
- devices/rs485/：提供板载串口配置与端口生命周期、Stream 和时序适配。
- protocol/sig/IDU_sig.inc、FAN_sig.inc：测点标识、类型、默认值和规则；新增测点由清单同步生成枚举及数组。
- task/system/data_bus_*：应用私有 aBus 实例、通用读写和 Shell 命令。
- protocol/mapping/：定义从站映射组、地址段与主站采集项，引用生成的 SIG/字段标识。
- protocol/FAN_modbus_master.c、IDU_modbus_slave.c：展开清单、配置主从服务参数，默认从站 1；
  RTU 分帧由 func/aModbus 提供，串口与时基在 devices/rs485/rs485_device.c；
  protocol.c 生成并挂载只读点表，持有 FAN Motor 副本、协议与 RTU 并创建任务。
- task/system/flash_test.c：手动擦写测试，默认不加入 CMake 源码列表。
- main.c：初始化驱动和 OS，创建 appInitTask 后启动调度器；初始化任务完成系统功能初始化后通过 aOSTaskExit 自退出。

初始化仅在启动阶段单线程执行。运行阶段使用已取得的私有句柄，
不通过重复 Init 获取句柄。protocolInit 成功或失败后再次调用均返回 BUSY，
防止重新装配正在使用的数据实例或重复创建任务；不支持运行期重启。
端口采用准备 RTU、创建协议、最后打开 USART 的顺序，避免 ISR 访问未就绪状态。
设备参数在 devices 下对应 .c 文件中配置；通信任务参数由 protocol 内部配置。
平台功能裁剪由 config 下的 CMake 配置控制。

详见 [设备按实例初始化](../docs/architecture.md) 和 [接口规范](../docs/interface_contract.md)。

当前不做运行时跨设备资源冲突检查。后续可使用 Python/CMake 增加构建期告警，尚未实现；设备参数、能力和初始化错误检查继续保留。

应用任务按所属功能创建：系统任务在 app/task，通信任务由 protocolInit 统一创建。
func 不创建/删除任务，不配置任务优先级和栈大小。
协议配置、注册和扩展步骤见 [protocol 说明](protocol/README.md)。

日志由 aSystemInit 在 Shell 初始化完成后单独调用 appLogInit，
Shell 任务在全部服务就绪后启动，不创建单独日志任务。
Flash 探测、aMemory 分区注册、数据库和 protocolInit 由 aSystemInit 显式编排；
全部成功后才启动 Shell 任务。appSystemFlashInit 不再启动存储或数据库服务，
appDatabaseInit 负责 aDataBaseInit 及打开应用数据库。启动失败不开放命令输入。
数据库配置和 `db` 命令用法见 [数据库演示](task/system/database.md)。
`log test`、`log level verbose` 和 `log info` 的用法见
[系统任务与日志调试](task/system/README.md)。
func 提供 Init/Process/DeInit 或事件处理入口；app 决定调用线程、周期和停止顺序。

SIG 就绪后启动 Modbus 演示，使用 USART2 PC10/PC11、PA15 手动 DE、
115200 8N1。主从切换与点表关系见 [产品协议](protocol/README.md)，
后续 JSON 生成输入放在 [protocol/config](protocol/config/README.md)。
销毁 func 实例前必须停止其调用者并等待在途操作结束，禁止强行删除持有模块锁的任务。
system 设备公开 appSystemStatusLedInit 与 appSystemConsoleInit，保留按实例初始化，不做统一重初始化。

控制台通过 `appSystemConsoleInit(void)` 初始化 USART、绑定 Stream 并初始化 aShell 单例，
USART 类型、私有控制台句柄指针和适配回调只在 app/devices 内部使用；
流接口不携带 context，回调直接访问已绑定的控制台句柄。aStream_t 定义在
platform/aLib/include/aStream.h；Shell 配置在设备初始化函数内构造，任务层创建并运行 Shell 任务。
flush 非空时表示显式提交缓冲输出；NULL 表示 write 已直接提交，无需刷新。
flush 不等待线路完成、不丢弃输入；Shell 不自动调用。

当前控制台使用 aDevUsartCreate 动态分配对象，Shell 初始化失败时调用
aDevUsartDestroy 回收；产品必须开启 ADEV_USART_DYNAMIC_ENABLE。

## SIG 数据与测试任务

IDU_sig.inc 和 FAN_sig.inc 配置两张点表，`app/task/system/data_bus_service.c` 私有持有
aBus handle。aSystemInit 在 Shell 初始化后调用 protocolInit，
由它装配 IDU/FAN 两张表并统一挂载，再启动计数测试和通信任务。关闭 Shell 时数据与测试任务仍初始化；
关闭 aBus 时一并裁剪。

业务统一通过 `dataBusSet(request)`、`dataBusGet(request)` 整组读写。
请求使用 aBusSetIndexRequest_t / aBusGetIndexRequest_t，通过对应
StructInit 初始化后填写 sigIndex、缓冲区、长度和锁等待时间。
sigIndex 是对应 IDU_sig_table.h 或 FAN_sig_table.h 的枚举下标，不是稳定 sigKey。
未初始化返回 NOT_READY，未知下标返回 NOT_FOUND，长度错误返回
INVALID_PARAM。请求与缓冲区只在调用期间借用。

FAN Motor 初始值为转速 100、温度 25，转速允许 0..6000。
Counter 初值为 0，在 app/task/sig/sig_task.c 中定义为静态变量，
通过旁边的 ABUS_RAM_BIND_EXPORT 分散注册绑定。任务每次延时
1000 ms 后直接 counter++，UINT32_MAX 后回绕到零。
实际周期包含调度时间，不作为精确计时器。

IDU_sig.inc 与 FAN_sig.inc 分别定义测点。protocol.c 展开为 static const
描述及表数组，通过 dataBusConfig_t 一次性挂载；handle 由系统数据总线私有持有。
sigIndex 按清单顺序自增，sigKey 按清单固定取值，数量自动统计。
默认值、范围及字段数组同样具有静态存储期，初始化不复制顶层表描述到 RAM。
注册直接使用 ABUS_RAM_BIND_EXPORT，实例号取自 app_config.h 中的
PROTOCOL_BUS_INSTANCE_ID，设备号和下标取自对应 sig_table.h。
aBus 按 instanceID + deviceID + sigIndex 匹配，
只使用 .abus_bindings 收集；描述和收集指针均为 const，存放 Flash。
包含绑定的同一 instanceID 只允许一个活动实例，由应用保证。
绑定只关联存储，不额外提供并发保护。直接访问绑定变量的同步策略
由应用自行决定；当前自增测试不做同步，Shell 设置值可能被自增覆盖。
通用读写仍遵循 aBus 锁配置，timeout 仅用于 aBus 锁等待，不能保护
任务绕过接口直接访问绑定变量。
动态创建启用时分配实例元数据，否则使用静态实例；两个测点的数据
均静态绑定。aBus 自身保持多实例能力。

### Shell 命令

命令在 app/task/system/data_bus_command.c 注册，按 deviceID + sigKey 访问：

- `sig get 1 42`：读取 Counter（IDU_SIG_COUNTER）。
- `sig set 1 42 100`：设置 Counter 为 100，任务随后继续自增。

当前 Shell 支持标量、RAW 和 STRUCT，类型和参数描述均来自点表。
ID 和整数值采用十进制，RAW 使用十六进制；非法输入不修改数据。
命令默认不等待锁，忙时返回错误，用户可重试。
通用测点命令归 task/system 管理；protocol 内部完成通信服务与测点的装配。

当前挂载 IDU、FAN 两张表，deviceID 分别为 1、2。
IDU Counter 使用键 42；FAN Motor 使用键 1001，原 Motor 命令的设备号需改成 2。
同一 handle 按 deviceID 路由，SIG 下标分别从零开始。Shell 显式接收 deviceID 和 sigKey，先调用 dataBusResolveKey 转成下标，
再通过 dataBusGetInfo 查询类型和长度，
不维护测点类型表，也不引用具体测点枚举。

### 通用 SIG 命令

```text
sig get 2 1001           # Motor：显示全部已登记字段
sig get 2 1001 0         # Motor.speed
sig set 2 1001 0 200     # speed 范围为 0..6000
sig set 2 1001 1 -10     # temperature：S32，无业务上下限
sig get 1 42             # Counter
sig set 1 42 100         # Counter：U32，无业务上下限
```

命令从 SIG 定义获取类型和字段描述。RAW SIG/字段也支持按完整长度进行连续
十六进制读写；Motor 为 STRUCT，必须指定参数才能文本赋值。
SIG 和 Param 都通过可选 range 指针指定上下限，NULL 表示不限制业务范围。
字段操作由 aBus 在原 SIG 锁内完成；任务直接修改绑定变量的同步仍由应用负责。

Shell 的原 sigIndex 参数已改为 sigKey，旧命令需调整。SIG 顺序变化不影响键的定位；
STRUCT 的 paramIndex 仍随字段描述顺序变化；C 代码使用生成的字段名称，
如 FAN_MOTOR_SPEED，避免直接填写数字下标。点表挂载与设备关系见
[产品协议](protocol/README.md)。
