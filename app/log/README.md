# 日志演示

系统初始化层在 Shell 初始化完成后、Shell 任务启动前调用 `appLogInit`。
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

通用接口在 [aLog.h](../../func/aLog/aLog.h)，应用可将输出回调替换为：

- Flash 队列：复制日志，应用后台任务调用 TSDB。
- Linux 输出：终端、文件或其他日志设施。
- 多后端：应用分发到 Shell 和 Flash，并明确部分成功策略。

Flash 后端须在回调返回前复制数据，不能保存上游静态缓冲区的指针。
