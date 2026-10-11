# 系统服务与调试

`system_init.c` 负责系统服务启动、状态灯和 Shell 任务，以及私有总线和数据库实例的通用访问。
本目录也管理通用数据总线和数据库实例及其调试命令，详见 [数据库演示](database.md)。
`command_flash.c` 保留手动擦写测试，默认不编译；需要复测时取消本目录 CMakeLists.txt 中该源码的注释。
产品输出适配和存储配置统一由 `app/devices/system` 提供。
日志相关内容按职责分布：

- `app/devices/system/system_device.c` 的日志输出段：产品日志配置及输出适配。
- `system_init.c` 的私有 `logInit()`：使用设备层配置初始化日志并处理失败清理。
- `command_log.c`：注册 `log` 调试命令。

## 存储初始化

`system_device.c` 的存储段持有 Flash 实例、设备操作和区域描述，
`appSystemMemoryConfigInit` 仅填充配置；本目录 `system_init.c` 的私有
`memoryInit()` 调用 aMemoryInit，由 aMemory 执行设备初始化与失败清理。
顶层按 Shell → memory → log → bus → database 的顺序调用，再启动通信服务。
main 的 appInitTask 在 aSystemInit 成功返回后调用 appTestTaskInit，统一创建应用测试任务。
存储、数据总线和数据库不新增独立任务。
存储设备初始化直接使用 ASHELL_PRINT，不依赖 aLog；启动信息先入队，
全部服务就绪后由 Shell 任务输出。需要数据库的日志输出须在数据库就绪后挂接。

## 日志初始化

`aSystemInit` 在 Shell 和存储初始化完成后调用同文件内的私有 `logInit()`，
Shell 任务已在 `shellInit` 中创建，但等待全部服务就绪后才处理命令和发送队列。
日志初始化不由 `shellInit` 承担。
`logInit` 集中完成配置装配、aLog 初始化、失败清理和启动日志；顶层只负责顺序和
返回状态检查。日志没有独立的 service 文件或对外初始化入口。
系统启动后业务直接使用 `ALOG_INFO` 等宏，无需再次初始化。
当前后端直接调用 `app_system_console_stream.write`，不经过 Shell 队列。
没有新增日志任务或日志队列；Shell 与日志共用 console_write 的 TX 锁。
aLog 保留格式化状态锁，Shell 保留输出队列锁，实际串口操作不持 Shell 队列锁。
Shell 任务创建失败或日志初始化失败时，通过 appSystemConsoleDeInit 清理控制台；
串口释放失败保留句柄及 TX 锁并报告错误。其他服务启动失败不回滚全部系统资源，
仍按启动失败停止运行处理，不开放 Shell 命令或重新执行系统初始化。
启动时会看到含等级和启动毫秒数的 `EasyLogger ready` 日志。
日志不等待 Shell 就绪门控，因此可能先于仍在 Shell 队列中的欢迎信息输出。

## 板端测试

```text
log info
log test
log level verbose
log test
log write info flash Flash backend can be added later
log write error database write failed
log info
log level info
```

- 默认 INFO，第一次 `log test` 输出 ERROR、WARN、INFO 三条。
- 设置 VERBOSE 后，再次测试输出五个等级及一行十六进制数据。
- `log info` 显示运行等级、编译等级、成功输出、丢弃和过滤次数。
- `log level` 单独使用时显示当前运行等级。
- `log write <level> <tag> <text...>` 输出任意测试文本，标签不含空格。
- 正文无需引号，命令将剩余参数以单个空格合并；nr 不提供引号解析。

普通日志形式如下，时间以实际启动毫秒数为准：

```text
I/flash           [1234 ms] Flash backend can be added later
```

日志接收成功表示整条已提交串口发送缓冲区，不代表线路发送完成。
发送使用 20 ms 总预算，包含 TX 锁等待；日志锁等待由 aLog 的 lock_timeout 控制。
锁争用、超时和部分发送会计入 dropped，可通过 `log info` 查看；
部分发送可能已输出前缀，不重试整条日志。Shell 队列满不会阻止日志输出。
这些命令只验证当前控制台后端，不擦除或写入 Flash。

## 后续后端

通用接口在 [aLog.h](../../../func/aLog/aLog.h)，应用可在
`app/devices/system/system_device.c` 的日志输出段中将输出回调替换为：

- Flash 队列：复制日志，应用后台任务调用 TSDB。
- Linux 输出：终端、文件或其他日志设施。
- 多后端：应用分发到控制台和 Flash，并明确部分成功策略。

Flash 后端须在回调返回前复制数据，不能保存上游静态缓冲区的指针。

## 数据总线服务

- `system_init.c/h` 的数据总线段：私有 aBus handle、初始化、键解析及通用数据访问。
- `command_data_bus.c`：注册 `sig` 命令，只依赖服务和 aBus 元信息。
- `protocolInit(bus_handle)`：显式借用系统总线，直接通过 aModbus 原有接口创建协议。

产品点表来自 protocol/sig 的 IDU_test.inc 和 FAN_test.inc 清单。
protocol_tables.c 展开为 static const 数组，通过 protocolBusConfigInit 提供配置；
system_init.c 中的私有 busInit 负责创建并挂载。
数据总线不增加独立任务，RAM 绑定在变量所属的 .c 中注册。
命令以 deviceID + sigKey 定位，内部按解析后的 sigIndex 访问：

```text
sig get 1
sig get 1 2001
sig set 1 2001 100
sig get 2 2001
sig set 2 2001 0 200
```

全部类型和范围来自点表，点表增减或调序无需改命令实现。
字段仍按 paramIndex 定位，字段描述调序时须更新字段命令。
两表各 50 个 TEST，分别覆盖六种类型。`sig save [deviceID sigKey]` 显式保存
选定项，`sig load [deviceID sigKey]` 恢复。数据库启动会在协议任务启动前恢复
已有记录。数据库初始化时比较 FlashDB 中的独立布局标记，标记缺失或不符
先作废旧数据再写入标记；失败则不开放实例。正常读写继续使用原始快照。
旧记录长度不符时仍沿用恢复流程逻辑删除，保留默认值并继续；范围或存储错误仍返回
失败。单项 `sig load` 作废旧记录后返回 NOT_FOUND，保留当前 RAM。
详细选择与操作限制见产品协议说明。
点表挂载与设备关系见 [产品协议](../../protocol/README.md)。

## 协议启动

system 通过 `protocol.h` 取得点表配置，初始化总线与数据库后调用
`protocolInit(bus_handle)`，不读取通信配置或判断角色。
`app/protocol/protocol.c` 只管理通信实例、端口装配与任务创建，
板载端口初始化在 `app/devices/rs485`。
成功后 system 才放行 Shell 消费任务；失败时门控保持未就绪，进入启动失败流程。
任务和扩展步骤见 [协议说明](../../protocol/README.md)。
