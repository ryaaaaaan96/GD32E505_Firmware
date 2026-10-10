# 系统服务与调试

`system_init.c` 负责系统服务的启动编排，以及状态灯和 Shell 任务。
本目录也管理通用数据总线和数据库实例及其调试命令，详见 [数据库演示](database.md)。
`flash_test.c` 保留手动擦写测试，默认不编译；启用步骤见
[Flash 手动测试](flash_test.md)。
产品输出适配和存储配置统一由 `app/devices/system` 提供。
日志相关内容按职责分布：

- `app/devices/system/log_config.c`：产品日志配置及输出适配。
- `log_service.c`：使用设备层配置初始化日志服务。
- `log_command.c`：注册 `log` 调试命令。

## 日志初始化

`aSystemInit` 在 Shell 初始化完成后单独调用 `appLogInit`，
Shell 任务已在 `shellInit` 中创建，但等待全部服务就绪后才处理命令和发送队列。
日志初始化不由 `shellInit` 承担。
系统启动后业务直接使用 `ALOG_INFO` 等宏，无需再次初始化。
当前后端使用 `aShellWrite` 将完整日志复制进 Shell 队列，不直接操作 USART。
没有新增日志任务或日志队列；Shell 任务统一负责发送，串口不再创建任务互斥锁。
Shell 任务创建失败或日志初始化失败时，通过 appSystemConsoleDeInit 清理控制台；
串口释放失败保留句柄并报告错误。其他服务启动失败不回滚全部系统资源，
仍按启动失败停止运行处理，不开放 Shell 命令或重新执行系统初始化。
启动时会看到含等级和启动毫秒数的 `EasyLogger ready` 日志。

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

日志接收成功表示进入 Shell 队列，不代表串口已经发送完成。
队列满或锁争用时整条丢弃，可通过 `log info` 查看累计次数。
这些命令只验证当前 Shell 后端，不擦除或写入 Flash。

## 后续后端

通用接口在 [aLog.h](../../../func/aLog/aLog.h)，应用可在
`app/devices/system/log_config.c` 中将输出回调替换为：

- Flash 队列：复制日志，应用后台任务调用 TSDB。
- Linux 输出：终端、文件或其他日志设施。
- 多后端：应用分发到 Shell 和 Flash，并明确部分成功策略。

Flash 后端须在回调返回前复制数据，不能保存上游静态缓冲区的指针。

## 数据总线服务

- `data_bus_service.c/h`：私有 aBus handle、初始化、键解析及通用数据访问。
- `data_bus_command.c`：注册 `sig` 命令，只依赖服务和 aBus 元信息。
- `data_bus_modbus.h`：应用协议装配入口，供主从示例借用私有总线实例。

产品点表来自 protocol/sig 的 IDU_sig.inc 和 FAN_sig.inc 清单。
protocol.c 展开为 static const 数组，再用 dataBusConfig_t 统一挂载。
数据总线不增加独立任务，RAM 绑定在变量所属的 .c 中注册。
命令以 deviceID + sigKey 定位，内部按解析后的 sigIndex 访问：

```text
sig get 1
sig get 1 42
sig set 1 42 100
sig get 2 1001
sig set 2 1001 0 200
```

全部类型和范围来自点表，点表增减或调序无需改命令实现。
字段仍按 paramIndex 定位，字段描述调序时须更新字段命令。
点表挂载与设备关系见 [产品协议](../../protocol/README.md)。

## 协议启动

system 只包含 `protocol.h` 并调用 `protocolInit()`，不读取协议配置或判断角色。
点表注册和各功能初始化由 `app/protocol/protocol.c` 编排，通信实例与任务也由该文件管理，
板载端口初始化在 `app/devices/rs485`。
成功后 system 才放行 Shell 消费任务；失败时门控保持未就绪，进入启动失败流程。
任务和扩展步骤见 [协议说明](../../protocol/README.md)。
