# aShell

`aShell` 使用 nr_micro_shell 2.0.0 作为编辑和命令解析内核，向业务提供单例、
流绑定、命令注册和生命周期接口。它不创建任务，也不直接依赖 USART 或芯片。
旧 Letter Shell 源码、配置和构建依赖已移除。

## 模块结构

- `include/aShell.h`：唯一公共接口，业务无需包含 nr 的头文件。
- `src/aShell.c`：单例生命周期、输入处理、输出与锁。
- `src/aShell_internal.h`：私有上下文和后端边界。
- `src/aShell_nr.c`：nr 命令桥接、内置命令、后端输出适配。
- `src/aShell_config.c`：启用和禁用构建共用的默认配置初始化。
- `src/aShell_stub.c`：禁用实现。
- `port/nr_micro_shell_port.h`：第三方配置映射。

状态集中在私有 `aShellContext_t`。Init/Process 各自拥有输出错误结果，
同次处理保留第一次错误；独立 Print 不保留跨调用错误。
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
    ASHELL_PRINT("pong\r\n");
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

默认内置 `help`、`version`，同样通过宏导出，Init 拒绝重名和重复注册。
宏的首参数为 C 标识符，同时作为命令名；描述为非空静态字符串。
命令表由链接器放入只读存储区，生命周期覆盖整个程序。回调使用 `int argc, char **argv`，返回 0 表示成功。
参数包含命令名，参数指针仅在本次回调中有效；不保证 `argv[argc]` 为 NULL。
回调可 Print，不可递归 Process 或调用 Init/DeInit。

应用指定一个任务/主循环调用 Process；每次最多读取 64 字节，命令同步执行。
耗时业务应由命令复制参数后提交给业务任务。销毁前停止所有 API 用户。
Init/DeInit 由调用方串行化。编辑和输出使用递归锁，不支持中断上下文调用。

编辑区/历史区为后端固定静态存储；配置见 `aShell_config.h`：128 字节行缓冲，
最多 16 个参数（含命令名），5 条历史。行最多 127 字节，容量包括末尾 NUL。
初始化配置仅包含流和读写超时，不再提供 commands/command_count。
命令表无堆分配、无运行时复制；aOS 锁仍由初始化创建。
关闭 ASHELL 后使用空实现，无后端或 aOS 资源访问。

## 输入与输出约定

- 支持可打印 ASCII、退格、方向键、Delete、Tab 补全和历史。
- 参数按空格分隔；本阶段不提供引号分组、反斜杠转义、管道或重定向。
- 接受 CR、LF，CRLF 只提交一次，跨 Process 调用同样有效。
- 参数超限拒绝执行；行溢出后丢弃整行至换行，防止执行截断命令或尾部。
- write 支持部分写入，单片段共用超时预算；零进展立即停止，NO_WAIT 只尝试一次。
- Init 提示符写入失败回滚；Process 返回输入/输出错误，已消费输入不重放。
  命令可能已产生副作用；不能因输出错误重试命令。OK 不代表命令业务成功。
- 用户使用 `ASHELL_PRINT`，启用时调用 aShellPrintf；最多 255 字节，超长截断，
  保留 void 语义。禁用时不求值参数。底层函数开启 GCC printf 格式检查。
- flush 可为 NULL，不自动调用；write 应自行提交数据或由应用安排刷新。
- 并发 Print 与编辑串行化，但不会为异步日志自动清行、重绘当前输入。

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
