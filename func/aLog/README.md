# aLog

aLog 封装 EasyLogger，提供单例日志、等级过滤、十六进制输出和统计。
只使用官方核心和工具源码，不修改 `EasyLogger/`，不编译官方移植模板、
异步队列、缓冲输出、pthread、文件或 EasyFlash 插件。
本次接入的官方提交为 `806328e131836662fa83dd43364a53f699fd76ac`。

## 输出边界

```text
业务任务 → aLog → 应用输出回调 → console_write（TX 锁）→ 串口
                          └─ 后续可接 Flash 队列 / 文件 / 多后端分发
```

aLog 不依赖 Shell、Flash25q 或 aDataBase，也不创建业务任务。
应用通过 `aLogConfig_t.output` 注入后端，`context` 可以关联后端状态。
当前输出适配与配置在 `app/devices/system/system_device.c` 的日志输出段，
这里直接调用 `app_system_console_stream.write`，与 Shell 共用控制台 TX 锁。
不经过 Shell 队列，不受 Shell 长命令或队列满影响；调用者可能等待串口。
产品初始化由 `app/task/system/system_init.c` 的私有 `logInit()` 完成，
同目录 `log_command.c` 提供调试命令。
Flash 日志后端尚未实现；后续可由应用回调复制入队，再在任务中写 TSDB。

回调在日志调用者任务中持日志锁执行，不能重入 aLog。
`data` 仅在回调期间有效，不保证以 NUL 结尾，必须按 `size` 消费或复制。
返回 OK 表示完整接收；当前串口后端表示整条提交到发送缓冲区，不表示线路完成。
当前后端每次提交使用 20 ms 总预算，包含 TX 锁等待与串口写入。
部分提交返回 ERROR，可能已经输出前缀，不读取旧 errno、不重试整条日志。
无进展失败按 errno 返回 BUSY、TIMEOUT、NOT_READY 等错误。
后端失败直接返回，不重试；多个后端需要应用自行定义部分成功的处理方式。
耗时后端若直接写 Flash，仍会阻塞日志调用者，应按业务需求提供后台队列。

## 初始化与调用

```c
#include "system_device.h"

aLogConfig_t config;
/* 产品控制台已由 appSystemConsoleInit 初始化。 */
appSystemLogConfigInit(&config);
config.level = ALOG_LEVEL_INFO;
aStatus_t status = aLogInit(&config);
```

```c
ALOG_ERROR("flash", "read failed: %d", status);
ALOG_WARN("database", "retry required");
ALOG_INFO("system", "clock: %lu Hz", (unsigned long)clock);
ALOG_DEBUG("protocol", "received %u bytes", count);
ALOG_VERBOSE("sample", "raw value: %u", value);
ALOG_HEXDUMP("protocol", data, size);
```

配置结构体复制保存，`context` 指向对象借用至 DeInit。
默认等级 INFO、颜色关闭、锁等待 NO_WAIT，不产生初始化隐式日志。
Init/DeInit 要求外部串行化；销毁前停止所有调用者。
所有运行接口只允许任务上下文，不能在 ISR 中格式化或使用任务锁。

## 同步、长度与返回值

aOS 外层互斥锁覆盖等级过滤、正文格式化、上游处理和输出回调。
保护的对象包括共享正文缓冲区、EasyLogger 单例、时间文本、输出结果和统计。
它不能替代设备锁：其他模块并不持有日志锁；设备锁也不能保护发送前的格式化。
官方内部锁关闭，相关钩子为空实现，避免同一互斥锁重复获取。
上游头文件只在模块内部可见；业务不得绕过 aLog 直接调用 `elog_*`。
默认锁忙返回 BUSY，可以配置有限等待或 FOREVER。
`lock_timeout` 仅控制日志锁等待，不控制输出回调的等待。
固定获取顺序为日志锁 → 控制台 TX 锁；控制台适配和底层驱动不得反向调用 aLog。
Shell 发送前已释放自己的队列锁，因此命令中调用日志不形成反向锁依赖。
Init/DeInit 不由这些锁自动保护，仍须停止使用者后串行执行。

正文先由 `vsnprintf` 格式化；官方没有 `va_list` 输出入口，再通过 `%s`
交给 EasyLogger 增加头部。正文不需要自行追加换行。
普通日志包含等级、标签、启动毫秒数及 CRLF；启动毫秒数随 32 位时基回绕，
不代表 RTC 时间。颜色可在初始化时开启，存入 Flash 时建议关闭。

| 项目 | 约定 |
|---|---|
| 标签 | 非空、最多 30 字节，不包含空白或控制字符 |
| 完整输出容量 | `ALOG_LINE_BUFFER_SIZE` |
| 正文长度 | 最多 `ALOG_LINE_BUFFER_SIZE - 81` 字节 |
| 超长 / 内含 NUL | 整条拒绝，不截断、不调用后端 |
| 运行等级过滤 | 返回 OK，不格式化，不交付后端 |
| 后端失败 | 返回后端状态，不重试，不回滚已交付记录 |
| HEX | DEBUG 等级，每行 16 字节，使用官方 HEX 格式，无普通时间头 |
| HEX 最大输入 | 65520 字节，避免官方 16 位行游标回绕 |

HEX 多行可能部分成功；第一次后端错误后不再交付剩余行。
上游仍会完成当前 HEX 调用的遍历，因此采集任务不宜进行大块 HEX 输出。

`aLogGetStats` 等待日志锁取得快照：完整接收记录数、丢弃次数、运行过滤次数。
HEX 每行算一条记录；锁失败、正文超长/NUL 和格式失败计丢弃，
非法等级、标签或空格式指针不计丢弃。
计数为 unsigned，溢出回绕，初始化时清零。
编译裁剪的宏不求值参数，因此不会增加运行过滤计数。

## 产品配置

在 `config/aclass_config.cmake` 中设置：

```cmake
set(ALOG_ENABLE ON)
set(ALOG_OUTPUT_LEVEL 5)
set(ALOG_LINE_BUFFER_SIZE 256)
```

静态最高等级：1 ERROR、2 WARN、3 INFO、4 DEBUG、5 VERBOSE。
运行阈值不能恢复已编译裁剪的等级，实际输出受两个阈值共同限制。
`ALOG_LINE_BUFFER_SIZE` 范围为 256..4096，容量含头部、颜色和 CRLF。
当前后端按串口发送能力和 20 ms 预算提交；增大日志行容量可能增加部分发送失败，
需要同时评估波特率、发送缓冲区及后端超时，不受 Shell 队列容量限制。
关闭日志后保留 target 和空实现，打印宏不求值参数，不编译上游和 OS 锁。
模块及官方核心继承项目告警和优化参数，没有局部告警豁免。

自有 `config/elog_cfg.h` 通过私有 include 路径优先加载，不修改官方配置。

## 验证

```sh
SANITIZE=1 python3 tests/log/run.py
python3 tests/log/build_matrix.py
cmake --build build/Debug --parallel 4
```

主机测试编译真实 EasyLogger，并验证生命周期、配置复制、边界、过滤、
后端错误、四线程并发、关闭宏、真实控制台适配与 Shell 命令处理。
共享控制台测试以串口替身检查并发发送互斥、Shell 队列满不阻塞日志、部分写入
不误报成功，以及 256 / 512 字节行容量。Release 矩阵验证关闭、独立后端、
等级裁剪和行容量；主机测试不代表板端串口时序验证。
板端操作见 [系统任务与日志调试](../../app/task/system/README.md)。
