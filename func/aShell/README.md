# aShell

`aShell` 使用 nr_micro_shell 2.0.0 作为编辑和命令解析内核，向业务提供单例、
流绑定、命令注册和生命周期接口。它不创建任务，也不直接依赖 USART 或芯片。
旧 Letter Shell 源码、配置和构建依赖已移除。

## 模块结构

- `include/aShell.h`：唯一公共接口，业务无需包含 nr 的头文件。
- `src/aShell.c`：单例生命周期、输入处理和打印格式化。
- `src/aShell_output.c`：有界输出队列、多生产者同步、部分发送及丢弃统计。
- `src/aShell_internal.h`：私有上下文和后端边界。
- `src/aShell_nr.c`：nr 命令桥接、内置命令、后端输出适配。
- `src/aShell_config.c`：启用和禁用构建共用的默认配置初始化。
- `src/aShell_stub.c`：禁用实现。
- `port/nr_micro_shell_port.h`：第三方配置映射。

解析状态集中在私有 `aShellContext_t`，输出队列状态由 output 文件私有持有。
ASHELL_PRINT 只复制文本入队，实际流发送只由 Process 调用。
命令由链接器收集，导出宏生成类型正确的回调包装；不再二次查找或分配命令索引。

所有源码（含 nr 内核）通过 `aclass_project_options` 继承工程参数：
`-Wall -Wextra -Wpedantic -Werror`，以及构建类型对应的优化和调试选项。
模块没有额外的告警屏蔽或优化覆盖。仅 `include` 目录公开给调用者。

## 接入

```c
static int ping_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ASHELL_REPLY("pong\r\n");
    return 0;
}

ASHELL_CMD_EXPORT(ping, ping_command, "Check console");

/* 在设备初始化中：先初始化实际接口，再初始化 Shell。 */
aShellConfig_t config;
aStatus_t status;

aShellConfigStructInit(&config);
config.stream.read = console_read;
config.stream.write = console_write;
status = aShellInit(&config);
```

默认内置 `help`、`clear`、`version`，同样通过宏导出，Init 拒绝重名和重复注册。
`clear` 通过输出队列发送 ANSI 清屏和光标归位序列，随后正常显示提示符；
保留命令历史，不清除终端滚动回溯，需要终端支持 ANSI 转义序列。
宏的首参数为 C 标识符，同时作为命令名；描述为非空静态字符串。
命令表由链接器放入只读存储区，生命周期覆盖整个程序。回调使用 `int argc, char **argv`，返回 0 表示成功。
参数包含命令名，参数指针仅在本次回调中有效；不保证 `argv[argc]` 为 NULL。
回调回复使用 ASHELL_REPLY，不可递归 Process 或调用 Init/DeInit。

应用指定一个任务/主循环调用 Process；每次最多读取 64 字节，命令同步执行。
耗时业务应由命令复制参数后提交给业务任务。销毁前停止所有 API 用户。
Init/DeInit 由调用方串行化，不支持中断上下文调用。
不再持锁执行命令；普通互斥锁只保护队列索引和短时复制，实际 I/O 在锁外执行。
这个锁不保护硬件，也不保护解析器、历史和命令执行；Process 仍必须单任务调用。
当前产品的 Shell 与日志共享 `app_system_console_stream`，发送互斥由
`app/devices/system/system_device.c` 中的 `console_write` 负责。
日志直接写控制台，Shell 队列仍支持其他任务的 ASHELL_PRINT/aShellWrite，
因此保留队列锁。仅仅增加串口锁不能删除多生产者队列的同步。
控制台 TX 锁只覆盖一次 write；分段回复与日志可能交错，不保证整条命令输出独占。
后台 Print/Write 生产者 NO_WAIT 获取队列锁，争用也整条拒绝；
命令 Reply 在需要空间时推进实际发送，长回复无需加大队列。

编辑区/历史区为后端固定静态存储；配置见 `aShell_config.h`：128 字节行缓冲，
最多 16 个参数（含命令名），10 条历史。行最多 127 字节，容量包括末尾 NUL。
历史数量在产品 `config/aclass_config.cmake` 中通过
`set(ASHELL_HISTORY_COUNT 10)` 配置，范围为 1～255。
CMake 校验后作为模块编译宏传入，
无需修改头文件；修改后重新构建即可。独立构建未配置时默认 10 条。
初始化配置仅包含流和读写超时，不再提供 commands/command_count。
命令表无堆分配、无运行时复制；输出队列默认 1024 字节静态存储，
单次格式化区为 256 字节（含 NUL），均在 aShell_config.h 配置。
队列互斥锁由初始化创建；丢弃计数使用 lock-free unsigned 原子操作。
DeInit 丢弃未发送数据，不隐式排空；必须先停止所有生产者和 Process。
关闭 ASHELL 后使用空实现，无后端或 aOS 资源访问。

## 输入与输出约定

- `aShell_config.h` 中的 `ASHELL_WELCOME` 定义启动欢迎信息，设为 `""`
  可关闭；`ASHELL_PROMPT` 定义提示符名称，默认 `AIDC`，内核追加 `: `。
  欢迎信息由 aShell 入队，先于初始提示符输出，不启用第三方固定 Logo。
- 支持可打印 ASCII、退格、方向键、Delete、Tab 补全和历史。
- 参数按空格分隔；本阶段不提供引号分组、反斜杠转义、管道或重定向。
- 接受 CR、LF，CRLF 只提交一次，跨 Process 调用同样有效。
- 参数超限拒绝执行；行溢出后丢弃整行至换行，防止执行截断命令或尾部。
- 输入 I/O 故障会丢弃当前半行至下一次 CR/LF，并提示重试；BUSY/TIMEOUT
  不触发此恢复。使用 nr 官方初始化入口恢复编辑状态，同时清除历史，避免
  把接收缺字节后的残缺文本作为命令执行。
- Init 只入队欢迎信息和初始提示符，不访问流；首次 Process 才发送。
- Process 在读取输入之前、处理之后各排空一次队列快照。一次排空共用
  write_timeout 预算；只移除实际成功提交的字节，部分写入、零进展、超时、
  错误都保留未发送尾部，下一轮继续，不重放已经提交的前缀。
- 前一次排空失败时跳过末尾排空，仍处理输入；命令 Reply 可能尝试推进输出。
  Process 优先返回输出错误，其次是回复错误和输入结果。没有输入仍为 BUSY，
  即使本轮发送过数据。
- read_timeout 必须有限或 NO_WAIT，禁止 FOREVER，防止输入等待阻塞唯一消费者。
  命令执行不受整轮超时预算限制；每次 Reply 自带一个 write_timeout。
  队列短临界区的消费提交仍使用互斥锁，不能把 write_timeout 当成硬实时上限。
- `ASHELL_PRINT` 返回 aStatus_t：OK 表示完整文本已复制入队，不代表发送完成；
  满队列或锁争用返回 BUSY，整条拒绝。超过 255 字节返回 INVALID_PARAM，
  格式化失败返回 ERROR，均不发送截断文本。NULL/未初始化分别返回
  INVALID_PARAM/NOT_READY。禁用宏返回 OK，不求值参数。
- `ASHELL_REPLY` 仅在 Process 调用链内使用，供命令、内置 help 和回显回复。
  与 Print 共用队列，空间不足时发送已有内容再继续，不递归处理输入。
  单次格式化同样限 255 字节，更长原始文本可调用 aShellReplyWrite。
  首个失败锁存到本轮 Process，后续回复停止并计一次丢弃；输出恢复后提示
  `reply interrupted; retry command.`，不会重放可能有副作用的业务命令。
- `aShellGetOutputStats` 返回待发送字节和累计丢弃次数。满队列、锁争用和
  格式化失败/超长、回复中断会计数，未初始化和 NULL 参数不计数。nr 的每次输出片段
  单独计一次消息，统计不等于被丢弃的行数。计数从 Init 开始，无符号溢出回绕。
- `aShellWrite(data, size)` 原始字节整条复制入队，供日志等输出后端使用。
  不格式化、不要求 NUL；受队列容量限制，不受 printf 的 255 字节限制。
  与 ASHELL_PRINT 共用同一队列和锁，满队列或锁争用仍整条拒绝。
- 提示符、回显和业务打印走同一队列。一次入队内部不会交错；nr 的多个输出
  片段之间仍可能插入其他任务的消息。不自动清行或恢复编辑行。
- 命令使用 Reply 输出大段结果；不要循环重试 ASHELL_PRINT 等待自己消费。
- flush 可为 NULL，不自动调用；write 应自行提交数据或由应用安排刷新。

```c
aStatus_t status = ASHELL_PRINT("value: %u\r\n", value);
/* BUSY 表示本条消息未入队，由业务决定是否允许丢弃。 */
```

Linux 接入沿用相同接口，由应用绑定终端/串口 read/write，处理终端原始模式、
超时、EOF 和恢复设置；RTOS 由应用任务驱动。此次提供主机解析器测试，未增加
完整 Linux aOS 后端或交互终端程序。

## GCC 链接段注册

`include/detail/aShell_export_gcc.h` 提供工具链适配，其他工具链尚未实现。
启用 Shell 的 CMake 构建仅接受 GNU 编译器；GCC 兼容头文件语法也允许 clangd
解析，方便编辑器检查，不代表已支持 Clang 固件构建。

每条命令导出一个只读描述和一个单字节计数标记。链接脚本按名称排序描述，
用 KEEP 保留描述及标记；标记段长度就是数量，不硬编码 32/64 位结构体大小。
通过 SHORT 写入真正的 uint16_t 数量对象，并在链接时检查数量不超过 65535。
编译期检查描述与 nr 的字段偏移、大小和对齐；Init 检查总长度和重名。
命令回调必须使用 int argc，宏包装成 nr 要求的 uint8_t argc，不转换函数指针。

- `GD32E505_flash.ld`：产品脚本中的 .ashell_* 段位于 .text 之后、
  RAM 数据的 Flash 加载镜像之前，使用 FLASH 区域和 text PHDR。
  移植新产品时需要复制对应链接段规则，不能只复制导出宏。
- `port/gcc/aShell_sections_host.ld`：主机测试适配，插入默认 .rodata 之后。
  当前仅验证非 PIE 主机程序，不承诺 PIE/共享库或完整 Linux 产品支持。
- 使用 GNU ld 的 READONLY 输出段属性；工具链需支持该语法，不兼容时应升级
  或单独适配，不能通过屏蔽告警绕过。

应用命令建议通过 `target_sources(app_target PRIVATE command.c)` 加入最终目标，
或以 CMake OBJECT 库加入。KEEP 不会让普通静态库中未引用的对象自动参与链接；
若命令存放在静态库中，需要对该命令库显式 whole-archive。不要全局开启。
禁用时宏不生成注册对象；回调仅在不求值表达式中引用以避免 unused 告警，
最终通过段回收裁剪未使用的函数。

## 上游与本地补丁

来源：https://github.com/Nrusher/nr_micro_shell

基线：`de9942125a43469d9f5e584c1bc338b64d959394`，保留上游 MIT LICENSE。
下载时的嵌套 Git 元数据备份于主仓库 `.git/vendor-backups/nr_micro_shell`，
源文件直接由本仓库维护，避免意外提交为无法取得的 gitlink。
仅编译 `src/nr_micro_shell_core.c`，不编译上游示例命令中的裸内存读写等功能。
上游 examples/test 保留作为参考，其默认命令表不用于本工程。

本地维护的差异：

- 恢复官方 `extern struct cmd cmd_table[]` 和
  `extern const uint16_t cmd_table_size` 声明，由链接脚本提供对象。
  名称/描述字段仍为 const；应用不包含后端头文件。
- 格式化输出支持块写入，处理 vsnprintf 负返回值，避免逐字符发送格式化文本。
- 修正历史环形索引回绕，限制历史复制长度。
- 修正补全多候选空指针访问、移动方向、尾部保留和游标显示。
- 修正最后一个参数超限漏检、删除后 NUL、空行未初始化返回值。
- 行溢出后整行丢弃；编译期限制 uint8_t 游标和历史索引容量。
- 修复编译警告，后端也开启 Wall/Wextra/Wpedantic/Werror。

更新上游时需要重新审核这些补丁。验证：

```sh
SANITIZE=1 python3 tests/shell/run.py
cmake --build build/Debug
python3 tests/shell/check_firmware.py \
    build/Debug/bin/gd32e505vet7_debug.elf
```
