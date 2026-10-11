# 应用层组织

## 文件命名

应用文件的功能和职责部分使用小写下划线，设备缩写保留大写，如 IDU、CSU。
设备业务文件使用“设备名 + 类别 + 职责”，省略重复的 `app_` 前缀；
配套头文件与源文件同名。目录表达所属模块，文件名表达业务归属和职责。

| 职责 | 文件名示例 |
| --- | --- |
| 业务测点清单 | `sig/IDU_test.inc`、`sig/FAN_test.inc` |
| 系统服务配置与生命周期 | `system_init.c/h` |
| 协议映射及采集数组 | `FAN_modbus_master.c`、`IDU_modbus_slave.c` |
| 协议角色和服务参数 | `FAN_modbus_master.c`、`IDU_modbus_slave.c` |
| 任务创建与运行循环 | `protocol.c`、`test_task.c` |
| 板级资源配置与注册 | `rs485_device.c`、`system_device.c` |
| 设备实例与访问入口 | `system_device.c` |
| Shell 命令 | `command_data_bus.c`、`command_database.c`、`command_log.c`、`command_flash.c` |
| 测点标识与共享数据类型 | `IDU_sig_table.h`、`FAN_sig_table.h` |
| 数据总线与协议的装配接口 | `protocol.h` 的显式总线参数 |
| 系统启动编排 | `system_init.c` |

`main.c` 保留入口名称，Shell 命令文件统一使用 `command_功能.c`。应用总配置使用
`app_config.h`，与 `main.c` 同级；这是集中配置的固定名称。
文件名不要求与接口前缀一致，头文件保护宏仍使用 `APP_` 命名空间。
protocol 按设备归属与职责命名：点表类使用 `IDU_sig_*`，
通信类使用 `IDU_modbus_slave.c` 与 `FAN_modbus_master.c`，分别表示
IDU 对上提供服务与向风扇控制板采集；FAN 为风扇控制板的统一简称。
当前固件运行在 IDU 板，上级为 CSU 板，下级为风扇控制板。
通用数据接口使用 `dataBus...`，协议通过 `protocolInit(bus)` 借用系统总线；
IDU_test.inc、FAN_test.inc 提供测点的唯一清单，
头文件由清单生成索引及固定 Key，protocol_tables.c 生成只读数组，system 统一挂载。
Modbus 主从文件提供只读业务配置及从站地址段回调，由 `app_config.h` 选择角色。
protocol.c 统一持有协议实例并创建任务；protocol.h 提供点表配置填充和协议初始化接口。
通信配置对象的 extern 声明集中在 protocol.c，点表对象由 protocol_tables.c 私有持有。
protocol.h 使用 aBus 类型表达装配依赖，不暴露通信配置对象；
设备数据头文件仅提供设备标识和共享数据类型。

## 目录职责

应用只保留三个一级目录，入口和调试配置仍与目录同级：

```text
app/
├── devices/           # 板级设备、存储和输出配置
├── task/              # 系统启动、运行任务和系统调试入口
│   ├── system/        # 日志、数据库、数据总线服务和可选 Flash 测试
│   └── test/          # 应用测试任务，当前为 TEST_001 静态绑定测试
├── protocol/          # 点表、协议装配与通信任务创建
│   ├── inc/           # protocol.h 与产品数据头文件
│   ├── config/        # 后续 JSON 配置输入
│   ├── sig/           # IDU_test.inc、FAN_test.inc 测点清单
│   ├── persistence/   # 需要持久化的测点清单
│   ├── protocol.c         # 协议实例与通信任务
│   ├── protocol_tables.c  # 点表生成、静态绑定及配置提供
│   ├── FAN_modbus_master.c
│   └── IDU_modbus_slave.c
├── main.c
└── app_config.h
```

`protocol` 集中管理围绕 aBus 的应用逻辑：由 IDU_test.inc 与 FAN_test.inc
生成本板、风扇板的只读点表，RAM 绑定放在变量所属的 .c 中，
主从配置将协议寄存器与点表连接，完成采集和转发。通用协议实现仍在 func 层。

- 系统 console 实例随 ASHELL_ENABLE 启用或裁剪，不再单独设置应用 USART 开关。
- devices/system/system_device.h：系统设备的 LED 句柄、console 流专用初始化接口。
- devices/system/system_device.c：统一管理 LED、控制台、日志输出、存储注册及数据库配置，按功能分段组织。
- task/system/system_init.c：统一初始化各服务，私有持有 aBus 和 KV/TSDB 实例，提供通用数据访问。
- task/system/system_init.h：声明 aSystemInit、dataBus 通用访问及数据库操作接口。
- task/system/command_log.c：日志调试命令，服务初始化由 system_init.c 的私有 logInit 完成。
- task/system/command_database.c：数据库调试命令，通过 system_init.h 使用私有实例。
- app_config.h：aBus 绑定实例号、调试角色和站号配置，默认 Modbus 从站 1。
- devices/rs485/：提供板载串口配置与端口生命周期、Stream 和时序适配。
- protocol/sig/IDU_test.inc、FAN_test.inc：测点标识、类型、默认值和规则；新增测点由清单同步生成枚举及数组。
- task/system/command_data_bus.c：通用 SIG Shell 命令，类型和范围从点表查询。
- protocol/FAN_modbus_master.c、IDU_modbus_slave.c：直接定义采集、映射和地址段数组，
  回调段与离散映射平级，引用 SIG 清单生成的标识并配置主从服务参数，默认从站 1；
  RTU 分帧由 func/aModbus 提供，串口与时基在 devices/rs485/rs485_device.c；
  protocol_tables.c 生成只读点表并持有 FAN TEST_001 数据；protocol.c 持有协议与 RTU 并创建任务。
- task/system/command_flash.c：手动擦写测试，默认不加入 CMake 源码列表。
- main.c：初始化驱动和 OS，创建 appInitTask 后启动调度器；任务先调用 aSystemInit，
  成功后调用 appTestTaskInit 统一创建应用测试任务，最后通过 aOSTaskExit 自退出。

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

日志由 aSystemInit 在 Shell 和存储初始化完成后调用同文件内的私有 logInit，
该函数负责装配配置、初始化、失败清理和启动日志，顶层只检查返回状态。
Shell 任务在 shellInit 中创建，等待全部服务就绪后才处理命令及输出，
不创建单独日志任务。
aSystemInit 在 shellInit 后依次调用 memoryInit、logInit、busInit、databaseInit，
再调用 protocolInit(bus_handle)；
全部系统服务成功后放行 Shell 任务，随后 main 创建应用测试任务。system_device.c 的存储段提供私有 Flash 实例、
设备回调和区域表，由 appSystemMemoryConfigInit 填充配置；
system_init.c 的私有 memoryInit 调用 aMemoryInit，初始化设备并发布区域。
存储探测信息直接通过 ASHELL_PRINT 入队，不依赖日志模块初始化。
业务只访问 aMemory 区域；system_device.c 将区域句柄关联到 KV/TSDB，
system_init.c 的 databaseInit 负责数据库初始化和打开实例。启动失败不开放命令输入。
数据库配置和 `db` 命令用法见 [数据库演示](task/system/database.md)。
`log test`、`log level verbose` 和 `log info` 的用法见
[系统任务与日志调试](task/system/README.md)。
func 提供 Init/Process/DeInit 或事件处理入口；app 决定调用线程、周期和停止顺序。

SIG 就绪后启动 Modbus 演示，使用 USART2 PC10/PC11、PA15 手动 DE、
115200 8N1。主从切换与点表关系见 [产品协议](protocol/README.md)，
后续 JSON 生成输入放在 [protocol/config](protocol/config/README.md)。
销毁 func 实例前必须停止其调用者并等待在途操作结束，禁止强行删除持有模块锁的任务。
system 设备公开 appSystemStatusLedInit 与 appSystemConsoleInit，保留按实例初始化；
控制台失败清理由 appSystemConsoleDeInit 统一处理，不提供系统运行期重启。

控制台通过 `appSystemConsoleInit(void)` 初始化 USART、绑定 Stream 并初始化 aShell 单例，
USART 类型、私有控制台句柄指针和适配回调只在 app/devices 内部使用；
公开只读 `app_system_console_stream`，Shell 和日志统一通过它访问串口；
流接口不携带 context，回调直接访问已绑定的控制台句柄。aStream_t 定义在
platform/aLib/include/aStream.h；Shell 配置在设备初始化函数内构造，任务层创建并运行 Shell 任务。
flush 非空时表示显式提交缓冲输出；NULL 表示 write 已直接提交，无需刷新。
flush 不等待线路完成、不丢弃输入；Shell 不自动调用。

当前控制台使用 aDevUsartCreate 动态分配对象，Shell 初始化失败时调用
aDevUsartDestroy 回收；产品必须开启 ADEV_USART_DYNAMIC_ENABLE。
创建 Shell 任务或初始化日志失败时，统一释放控制台串口、TX 锁及 Shell 队列；
串口清理失败保留句柄和 TX 锁，允许在独占条件下重试清理。
日志就绪后的服务初始化失败仍进入 appFatal，Shell 门控不放行，
不尝试回滚可能已有使用者的全部服务，也不支持重试整个系统初始化。

控制台接收由 Shell 独占，不加 RX 锁；发送由 Shell 和日志共享，
`console_write` 使用一把 TX 锁覆盖单次写入，锁等待与发送共用超时预算。
aLog 锁保护共享格式化和统计，Shell 锁保护输出队列；两者都不代替 TX 锁。
日志绕过 Shell 队列，不保证与排队的启动文字、命令回显按调用顺序显示。
RS485 只由选定的 Modbus 协议任务调用，不额外创建串口互斥锁。
若新增直接访问者，应用设备入口须协调同方向 API 及完整 RS485 事务，
关闭设备前须先停止所有使用者。设备层的 BUSY 检查不能替代应用互斥。

## SIG 数据与测试任务

IDU、FAN 点表各 50 个 SIG，统一命名 TEST_001～050，deviceID 分别为 1、2，
sigKey 均为 2001～2050。两表分别覆盖 U8、U16、U32、S32、RAW 和 STRUCT。
不保留旧 Counter/Motor 及旧 Key。唯一清单位于 protocol/sig 的
IDU_test.inc、FAN_test.inc；枚举、只读定义和静态存储均由同一清单展开。

system_init.c 私有持有 aBus handle，业务通过 dataBusSet/dataBusGet 及
Param 接口访问，协议通过 protocolInit(bus) 借用总线。sigIndex 是枚举下标，
Shell 使用稳定 sigKey。查询配置不会重置活动实例。

IDU TEST_001 的 RAM 属于 task/test/test_task.c，通过 ABUS_RAM_BIND_EXPORT
绑定，每秒直接加 1，UINT32_MAX 后回绕到零。直接访问绑定变量的同步仍由应用
决定；此测试不与 Shell 设置互斥。aBus 接口内部继续遵循配置的锁策略。
main 在系统服务与通信初始化后调用 appTestTaskInit；关闭 aBus 时不创建空任务。
FAN TEST_001 同样显式绑定；其余数据在动态模式一次分配，在静态模式按 C 类型
生成 RAM。总线本身仍支持多实例。

数据库选定 12 个 SIG 持久化：IDU TEST_001～009、FAN TEST_002～004。
启动时在通信与测试任务之前恢复记录，缺失记录保留默认值；范围或长度错误
停止启动并返回错误。数据更新不自动写 Flash，通过 sig save 显式保存。

### 通用 SIG 命令

```text
sig get 1                    # 显示 IDU 全表及类型、flags、范围
sig get 2                    # 显示 FAN 全表
sig get 1 2001               # 静态绑定的自增测试值
sig set 1 2002 123           # U8
sig set 1 2003 12345         # U16
sig set 1 2004 1234          # U32
sig set 1 2005 -123          # S32
sig set 1 2006 1122334455667788  # RAW[8]
sig get 1 2007               # 组合类型的所有字段
sig set 1 2007 0 200         # STRUCT.U32
sig set 1 2007 1 -10         # STRUCT.S32
sig set 1 2007 2 3           # STRUCT.U8
sig set 1 2007 3 112233      # STRUCT.RAW[3]

db init                      # 空白介质首次显式初始化
sig save                     # 保存全部选定项
sig save 2 2003              # 保存单项
sig load 2 2003              # 恢复单项
sig load                     # 恢复所有已有记录
```

命令位于 task/system/command_data_bus.c，通过元信息处理类型和字段，
不引用具体点表枚举。ID、整数采用十进制，RAW 为完整长度的连续十六进制。
sig get/set 默认不等待锁；忙时可重试。STRUCT 写入须指定 paramIndex，
其数字随字段顺序变化；C 代码应使用生成的字段名称。

协议寄存器映射、各字段范围和持久化限制见 [产品协议](protocol/README.md)。
