# 系统任务与日志调试

`system_init.c` 负责系统服务的启动编排，以及状态灯和 Shell 任务。
本目录也管理数据库实例及其调试命令，详见 [数据库演示](database.md)。
`flash_test.c` 保留手动擦写测试，默认不编译；启用步骤见
[Flash 手动测试](flash_test.md)。
产品输出适配和存储配置统一由 `app/devices/system` 提供。
日志相关内容按职责分布：

- `app/devices/system/log_config.c`：产品日志配置及输出适配。
- `log_service.c`：使用设备层配置初始化日志服务。
- `log_command.c`：注册 `log` 调试命令。

## 日志初始化

`aSystemInit` 在 Shell 初始化完成后单独调用 `appLogInit`，
全部服务就绪后才启动 Shell 任务。日志初始化不由 `shellInit` 承担。
系统启动后业务直接使用 `ALOG_INFO` 等宏，无需再次初始化。
当前后端使用 `aShellWrite` 将完整日志复制进 Shell 队列，不直接操作 USART。
没有新增日志任务或日志队列；Shell 任务统一负责发送。
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
