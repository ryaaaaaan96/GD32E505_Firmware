# 文档索引

本文档入口对应当前仓库。平台已实现 GD32E505 / FreeRTOS，Linux 后端仍待实现。
构建和调试命令见[工程 README](../README.md)。

## 阅读顺序

1. [整体架构](architecture.md)：各层职责、依赖方向与应用启动顺序。
2. [公共接口规范](interface_contract.md)：类型、所有权、超时、回调和实例布局。
3. [构建与配置](cmake_design.md)：功能开关、依赖校验、目标边界和外部产品接入。
4. 根据功能阅读模块 README；修改后按[验证指南](testing.md)选择检查入口。

## 专题设计

| 文档 | 范围 |
| --- | --- |
| [aDrv 与 aDevice 设计](driver_device_design.md) | 外设分类、配置与运行状态、通用 DMA 和移植边界 |
| [USART、RS485 与中断](usart_design.md) | 收发路径、ISR 回调、DE 控制和应用适配 |
| [Flash25Q / SPI 存储链路](spi_flash_design.md) | SFUD、总线与锁、超时，以及未实现的 QSPI 路径 |
| [验证指南](testing.md) | 主机测试、构建矩阵与上板验证的边界 |

## 模块说明

| 层级 | 入口 |
| --- | --- |
| 基础类型与运行时 | [aLib](../platform/aLib/README.md)、[aCore](../platform/aCore/README.md) |
| 硬件与 OS | [aDrv](../platform/aDrv/README.md)、[aOS](../platform/aOS/README.md) |
| 设备 | [device](../device/README.md)、[USART](../device/aDev_usart/README.md)、[Flash25Q](../device/aDev_Flash25q/README.md) |
| 数据与存储 | [aBus](../func/aBus/README.md)、[aMemory](../func/aMemory/README.md)、[aDataBase](../func/aDataBase/README.md) |
| 协议与交互 | [aModbus](../func/aModbus/README.md)、[aShell](../func/aShell/README.md)、[aLog](../func/aLog/README.md) |

公共头文件维护完整签名和参数契约；模块 README 维护接入示例、功能限制和
第三方适配说明。这里集中说明跨模块关系，避免复制多份接口声明。

## 应用与联调

| 需求 | 入口 |
| --- | --- |
| 查看应用目录、任务和初始化 | [app](../app/README.md) |
| SIG 与 Shell 读写 | [通用 SIG 命令](../app/README.md#通用-sig-命令) |
| RS485 主从切换与点表关系 | [产品协议](../app/protocol/README.md) |
| 后续 CSV/JSON 点表与协议配置 | [配置目录](../app/protocol/config/README.md) |
| Flash 探测和手动擦写 | [Flash 测试](../app/task/system/flash_test.md) |
| KV 参数与 TSDB 记录 | [数据库演示](../app/task/system/database.md) |
| 日志等级、输出与测试命令 | [系统任务与日志调试](../app/task/system/README.md) |

## 评审与历史

| 记录 | 用途 |
| --- | --- |
| [2026-10-07 架构与性能复查](reviews/2026-10-07_architecture_review.md) | 该轮修改、验证结果和当时的剩余问题；时间点早于 Modbus 板级 Demo |
| [历史设计讨论](../DESIGN_REVIEW.md) | 保留讨论过程和用户决定；其中旧接口与状态说明不代表当前实现 |

## 维护约定

- 当前设计放在 `docs`，带日期的评审快照放在 `docs/reviews`。
- 模块用法和板级参数更新对应模块或应用 README，索引通过相对链接引用。
- 规划功能明确标注“未实现”，测试结果说明主机、构建或硬件验证范围。
- 修改接口或目录时同步头文件契约、正文和相对链接。
- 评审中的代码体积、测试数量只表示当时结果，不用作当前构建指标。
- 第三方文档保留在上游目录，自有文档只记录适配差异和使用约束。
