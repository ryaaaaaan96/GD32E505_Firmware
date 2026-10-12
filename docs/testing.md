# 验证指南

[返回文档索引](README.md)

以下命令在仓库根目录执行。根据改动范围选择检查；文档修改检查链接和
格式，运行代码修改执行对应回归，构建配置修改再执行相关构建矩阵。

## 基础检查

```sh
git diff --check
python3 tests/architecture/run.py
cmake -P tests/config/test_resolver.cmake
```

架构检查验证自有代码的层间依赖、公共头独立包含，以及只读点表定义与
aBus 运行库配置的隔离；不逐行审计全部第三方源码。

## 主机回归

需要 Python 3 和可用的主机 C 编译器；部分测试使用 pthread 和 sanitizer。
测试脚本在临时目录编译，通常用硬件与 OS 替身驱动真实模块实现。

| 范围 | 命令 |
| --- | --- |
| 可选布局断言与 SIG 参数长度求和（需要 C/C++ 编译器） | `python3 tests/layout/run.py` |
| aLib CRC32（C11/C++11、标准向量及 zlib 对照） | `python3 tests/crc/run.py` |
| aLib GCC 原子封装（并发、内存序及生成代码对照） | `python3 tests/atomic/run.py` |
| OS 任务创建/退出/Join、通知、定时器与工作队列 | `python3 tests/aos/run.py` |
| LED/GPIO 状态、静态/动态生命周期与接口裁剪 | `python3 tests/led/run.py` |
| SPI 配置预检查与软件片选 | `python3 tests/spi/run.py` |
| DMA 通道、IRQ/查询交错与真实 USART DMA 适配 | `python3 tests/dma/run.py` |
| USART、RX 缓冲丢弃、RS485、FIFO 与 IRQ | `python3 tests/usart/run.py` |
| aBus 定义、绑定、锁与分配 | `python3 tests/bus/run.py` |
| SIG 应用与 Shell 类型解析 | `python3 tests/bus/test_app_sig.py`、`python3 tests/bus/test_shell_types.py` |
| aMemory 注册、分区与访问 | `python3 tests/memory/run.py` |
| Flash25Q / 真实 SFUD 核心 | `python3 tests/flash25q/run.py` |
| Flash 测试命令 | `python3 tests/flash25q/test_command.py` |
| 数据库与完整 Flash 适配链 | `python3 tests/database/run.py`、`python3 tests/database/test_flash_chain.py` |
| Modbus 协议、aBus 映射与通用 RTU 多实例/时序 | `python3 tests/modbus/run.py` |
| 协议统一入口、启动顺序与功能关闭 | `python3 tests/protocol_tasks/run.py` |
| Modbus 板级端口、主从、任务调度与生命周期 | `python3 tests/modbus_demo/run.py` |
| Shell EOF/半行清理、日志、共享控制台发送互斥及部分写入 | `python3 tests/shell/run.py`、`python3 tests/log/run.py` |
| 应用设备清理、控制台 RX 溢出恢复、TX 锁超时、启动编排与 Shell 就绪门控 | `python3 tests/app_devices/run.py`、`python3 tests/app_startup/run.py` |
| 调试脚本 | `python3 tests/scripts/test_debug.py` |

`tests/aos/run.py` 验证任务创建直传入口与参数、显式退出和删除语义。
Join 检查仅覆盖 FreeRTOS 预留接口：各类超时参数均返回 UNSUPPORTED，
句柄保持不变；完整等待行为留待 Linux 后端实现后验证。

`tests/app_devices/run.py` 包含真实控制台适配与 Shell 解析器的接收联测，
通过模拟满 RX ring、读取期间溢出、旧换行、连续溢出及读空边界再次溢出，
检查损坏命令不执行、一次丢弃缓冲残留、丢弃失败后的重试，以及正常半行和
重新初始化行为；同时检查零长度和非法参数不清除缓冲及错误，超时参数原样传给 aDev。
`tests/usart/run.py` 使用真实 aDev 验证中断缓冲丢弃、边界处的新字节/错误、
活动接收拒绝丢弃、DMA 查询失败保留状态，以及快照后继续接收和计数回绕；
验证普通 Read 将 IRQ/DMA 溢出统一返回为 EIO，覆盖部分复制和等待结束窗口，
保留零长度、参数错误的优先级以及 ISR 不写任务 errno 的约定；
还执行轮询、仅中断、仅 DMA 三种 RX 裁剪组合的主机编译检查。

USART 优先级回归覆盖真实平台 CMake 对 OS 安全范围的传递，以及 IRQ 注册、
IDLE、关闭后恢复仍使用驱动绑定值。`tests/dma/run.py` 检查单次和循环接收、
共享 DMA 通道换用 USART 后的优先级；`tests/modbus_demo/run.py` 在默认 5..15
及收窄 7..12 的范围下验证真实驱动初始化，非法值在写硬件前拒绝。
主机配置检查只生成构建，不代替完整 ARM 编译。

部分脚本支持通过 `SANITIZE=1` 增加 AddressSanitizer 检查，例如：

```sh
SANITIZE=1 python3 tests/modbus_demo/run.py
```

测试范围及开关以对应脚本为准。主机替身测试不表示已经实现 Linux aOS，
也不验证真实 GPIO、DMA、总线时序或调度负载。

## 固件与构建矩阵

需要 CMake、Ninja 和 ARM GCC；工具链配置见[工程 README](../README.md)。

```sh
python3 scripts/build.py
python3 scripts/build.py release
```

| 配置范围 | 命令 |
| --- | --- |
| USART 分配、收发及可选能力 | `python3 tests/config/build_matrix.py` |
| Flash25Q 分配与模块裁剪 | `python3 tests/flash25q/build_matrix.py` |
| 数据库存储依赖与分配 | `python3 tests/database/build_matrix.py` |
| Modbus 主从角色与分配 | `python3 tests/modbus/build_matrix.py` |
| 日志等级与功能裁剪 | `python3 tests/log/build_matrix.py` |
| 外部产品接入与非法 OS 配置 | `python3 tests/config/test_platform_build.py` |

矩阵在独立构建目录检查组合、最终链接或符号裁剪，组合数以脚本为准。
ELF 的静态 RAM 统计包含预留区，不等同于运行时堆使用量或任务栈峰值。

## 上板验证

| 路径 | 重点 |
| --- | --- |
| USART / RS485 | 引脚路由、TC 后释放 DE、IRQ 延迟、溢出和 DMA 覆盖边界 |
| Modbus | 主从读写与断线恢复、任务栈余量、ISR 分帧、队列满、最坏中断延迟 |
| SPI Flash | JEDEC 信息、跨页编程、擦除范围、CS 时序和超时恢复 |
| 数据库 | 目标介质上的恢复时间、GC、实际掉电与写入失败 |
| Shell / 日志 | 交互延迟、后台输出并发及队列满时行为 |

板上操作步骤见[应用联调入口](README.md#应用与联调)。Flash 测试默认不编译，
需先在 app/task/system/CMakeLists.txt 中取消 command_flash.c 对应行的注释，
再重新编译和烧录。Flash 测试会改变指定区域，数据库首次格式化也由应用
命令显式触发；按各演示文档选择测试区域。
当前 Modbus Demo 使用 ISR 周期时间戳分帧，硬件边界精度与使用限制见其应用说明。

## TEST 点表与协议挂载回归

- `python3 tests/bus/test_sig_check.py`：构建期 Key 检查。
- `python3 tests/bus/run.py`：锁模式、分配模式、NDEBUG、定义检查，
  包含整组编辑失败不提交和 DISJOINT 范围检查。
- `python3 tests/modbus/run.py`：协议编解码、同 SIG 快照/提交、连续采集合并，
  从站显式长度缺失/不匹配的初始化拒绝、U32/RAW 部分读取和半值写入拒绝，
  20 字节 STRUCT 的全部读取子区间、字段边界写入、范围拒绝、混合字序、
  自定义整组/字段回调、主站整组采集和一次提交，非法布局及容量的拒绝，
  以及平级路由的交叉重叠、空洞预检、跨回调/映射读写和回调失败的处理。
- `python3 tests/modbus_demo/run.py`：实际应用主从/静动态路径、混合类型映射、
  FAN 的 30 个 SIG 整组/标量采集、30 项自动合并及显式断帧、未采集 20 项保持原值，
  主从显式寄存器数量不匹配、整组字段调序时在打开端口前拒绝，
  统一设备超时和整轮周期，以及三个地址段回调 Demo 的真实报文；通过普通 C 数组扩展
  映射与轮询项，检查空洞、权限、无效映射及采集目标在打开端口前拒绝。
- `python3 tests/database/run.py`：FlashDB 与应用 100 SIG 点表，验证选定项保存、
  重开恢复、六种类型和范围错误；未选中项不写入 Flash。覆盖旧记录长度变大/变小
  后的逻辑删除、保留默认值、继续恢复、重开和新格式保存，以及读取/删除失败上报。
  普通 Get 区分参数错误与记录长度不兼容，均不修改 Flash。
  `test_sig_layout.c` 覆盖仅初始化访问的独立布局标记、无标记旧数据、同长度类型/
  偏移/顺序变化、快照字节不变、重复初始化无写入，以及标记读失败和逐次写入故障恢复。
- `python3 tests/protocol_tasks/run.py`：点表展开、配置生命周期与重复查询，
  持久化清单自动计数，以及点表配置不依赖 FlashDB 私有布局的检查。
- `python3 tests/app_startup/run.py`：参数恢复在协议与测试任务启动前完成。

IDU/FAN 各 50 项。主机测试模拟介质与串口，不替代板上的串口时序、
真实掉电恢复和 FreeRTOS 栈余量检查。命令与寄存器见
[产品协议说明](../app/protocol/README.md)。


## aPoll 与预生成采集计划

```bash
python3 tests/poll/run.py
python3 tests/modbus/run.py
python3 tests/modbus_demo/run.py
python3 tests/modbus/build_matrix.py
```

aPoll 验证静动态分配、周期回绕、错误推进、通知先于等待、回调期间重投、
超过 32 个事件、多生产者合并、队列满及三类工作公平性。
Modbus 验证预生成 RTU 帧和 TCP 分组、四种读取区域、CRC/异常/超时、
发布 BUSY 与入口 BUSY 的推进区别，以及计划与即时请求交替执行。
RTU 分帧覆盖即时输入、带采集时间的延迟/分块/多帧批量交付、帧内超时、
采集错误、帧间静默、多实例，以及采集计数器和毫秒时基的 32 位回绕。
Demo 验证主从、六帧采集、事件提前唤醒、临时请求和资源失败清理。
构建通过不代表板上时序已验证；主站可通过 `protocol refresh` 检查事件触发。

字节流回归同时验证 read/write/flush 的实例上下文，RTU 同一组回调服务两个
实例及物理 flush 上下文转交；零长度请求、暂不可用、超时和帧耗尽分别处理。
Shell 覆盖半行等待后续输入、EOF 清理、EOF 后继续输出及重新初始化；Modbus
覆盖主从 EOF、半帧 EOF、写入返回零的违约和 EOF 后拒绝继续事务。
