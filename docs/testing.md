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
| OS 通知、定时器与工作队列 | `python3 tests/aos/run.py` |
| SPI 配置预检查与软件片选 | `python3 tests/spi/run.py` |
| USART、RS485、FIFO 与 IRQ | `python3 tests/usart/run.py` |
| aBus 定义、绑定、锁与分配 | `python3 tests/bus/run.py` |
| SIG 应用与 Shell 类型解析 | `python3 tests/bus/test_app_sig.py`、`python3 tests/bus/test_shell_types.py` |
| aMemory 注册、分区与访问 | `python3 tests/memory/run.py` |
| Flash25Q / 真实 SFUD 核心 | `python3 tests/flash25q/run.py` |
| Flash 测试命令 | `python3 tests/flash25q/test_command.py` |
| 数据库与完整 Flash 适配链 | `python3 tests/database/run.py`、`python3 tests/database/test_flash_chain.py` |
| Modbus 协议与 aBus 映射 | `python3 tests/modbus/run.py` |
| Modbus 板级端口、主从与生命周期 | `python3 tests/modbus_demo/run.py` |
| Shell / 日志 | `python3 tests/shell/run.py`、`python3 tests/log/run.py` |
| 应用设备与启动编排 | `python3 tests/app_devices/run.py`、`python3 tests/app_startup/run.py` |
| 调试脚本 | `python3 tests/scripts/test_debug.py` |

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

板上操作步骤见[应用联调入口](README.md#应用与联调)。Flash 测试会改变指定
区域，数据库首次格式化也由应用命令显式触发；按各演示文档选择测试区域。
当前 Modbus Demo 使用 ISR 周期时间戳分帧，硬件边界精度与使用限制见其应用说明。
