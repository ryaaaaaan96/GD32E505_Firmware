# GD32E505_Firmware 架构与接口问题讨论稿

本文根据当前工作区整理，供架构 review 和后续修改排期使用。只讨论分层、模块边界、公开接口及文档一致性，不是本轮代码修改清单；列出的问题不代表都必须按某一种方案实现。

## 总体判断

当前分层已经有清晰雏形：`aDrv` 负责硬件操作，`device` 组合设备语义，`func` 提供功能模块，`app` 持有产品资源并显式初始化。接下来主要需要审视的是接口边界是否足够稳定：公开结构暴露多少内部状态、上层如何绑定底层设备、异步事件的上下文如何定义，以及设计文档能否区分目标与已实现能力。

建议本轮先对以下问题逐项讨论并决定是否修改，不要把所有条目默认视为必须重构。

## 讨论问题

### 1. aDevUsart 公共句柄暴露内部实现状态

现状：[`aDev_usart.h`](device/aDev_usart/aDev_usart.h) 的 `aDevUsartHandle_t` 包含 RX/TX ring 索引和计数、DMA 接收累计值、互斥锁、等待对象、回调指针及内部错误状态等字段。

影响：调用方需要分配完整句柄，因此内部字段虽然意图私有，仍然出现在公共头文件中。后续修改缓冲管理或并发实现时，可能影响调用方编译、结构体大小和接口稳定性；也容易让应用直接依赖实现细节。

待讨论：

- 是否继续采用“调用方静态分配完整 handle，并约定不得访问内部字段”的简化方式？
- 是否需要隐藏内部状态，同时仍保持无动态分配？例如公开固定存储区/不透明存储类型，或由应用提供对齐存储。
- 如果隐藏，怎样避免固定存储区大小成为新的 ABI 负担？

我觉得可以公开声明，但是实现放进.c里面吗？
是否有动态分配的优劣在哪？


### 2. aMemory 通过全局索引查找 Flash 设备

现状：[`fal_flash25q_port.c`](func/aMemory/src/fal_flash25q_port.c) 使用 `aDevFlash25qGetDevice(AMEMORY_FLASH_DEVICE_INDEX)` 获取设备；设备注册表和设备数量定义在 [`aDev_flash25q.h`](device/aDev_Flash25q/aDev_flash25q.h)。分区策略则来自 `config/aMemory_flash_layout.h`。

影响：aMemory 与 Flash25Q 的关联不是通过初始化参数显式传入，而是依赖设备先注册到全局索引。设备实例、初始化顺序和存储适配器之间存在隐式约定，也使多个存储实例或测试替身不易注入。

待讨论：

- 保留全局设备注册表，还是改成 app 显式绑定 `aDevFlash25qHandle_t` 与分区布局？
- aMemory 是否需要面向通用存储接口，还是明确作为当前 Flash25Q/FAL 的产品适配层即可？
- 如果暂时只支持一个设备，是否仍保留 index/get-device 机制？

我的答复：aMemory 当前唯一用途是为 FlashDB 提供 FAL 适配。若产品只使用 FlashDB，
不需要单独的 aMemory library；应将 FAL/Flash25Q adapter 收入 aDataBase，并由 app 显式
绑定 Flash25Q handle。本轮按此方向调整，保留 FAL，因为这是当前 FlashDB FAL 模式所需
的存储接口，不再把它作为独立 aMemory 层。


### 3. USART 业务事件回调固定运行在 ISR 上下文

现状：[`aDev_usart.h`](device/aDev_usart/aDev_usart.h) 说明事件回调在 ISR 上下文执行；回调从 USART 或 DMA 中断路径触发。调用方必须只执行 ISR-safe 操作，通常还需要自行唤醒业务任务。

影响：这是明确的 MCU/RTOS 执行上下文契约。它方便低延迟通知，但也把中断限制传递给应用；Linux 等线程模型不一定能提供同样的回调上下文和语义，因而“硬件/OS 无关”的 API 目标需要进一步定义。

待讨论：

- 把当前接口明确命名/定位为 ISR 通知接口，接受应用承担 ISR-safe 约束？
- 还是由 aDev/aOS 将事件投递到任务/线程上下文，提供更统一的业务回调语义？
- 若两种场景都需要，是否拆成 ISR-safe 轻量通知和普通上下文事件回调，而不是让一个 API 承担两种语义？

### 4. aDevUsart 配置结构直接包含 aDrv 配置

现状：[`aDev_usart.h`](device/aDev_usart/aDev_usart.h) 中 `aDevUsartConfig_t` 嵌入 `aDrvUsartConfig_t`，应用通过该字段配置逻辑实例、引脚和串口参数。

影响：device 接口直接使用 aDrv 配置类型，减少了一层字段映射，也让 app 能显式控制硬件资源；另一方面，device 公共接口与 aDrv 配置结构演进绑定，device 层没有完全形成独立的串口设备配置语义。

待讨论：

- 目前这种复用通用 aDrv 配置是否已满足跨芯片目标？
- 是否应由 aDevUsart 定义自己的公共配置，再在内部转换到 aDrv？
- 若拆分，哪些是通用设备属性（波特率、数据位等），哪些应继续由 app 作为硬件资源映射提供（实例、引脚）？

推荐方案是什么样呢，

### 5. USART 设计文档混合了规划接口与当前接口

现状：[`docs/usart_design.md`](docs/usart_design.md) 的接口总表列有 `aDevUsartWriteAsync()`、RX session 和 `aUsartTxQueue` 等目标接口；文档后续章节又说明这些接口尚未实现。当前接口状态另见 [`docs/usart_interface_review.md`](docs/usart_interface_review.md)。

影响：读者可能把设计目标误认为现有可调用 API，进而按不存在的接口编写应用，或者误判实现完成度。

待讨论：是否把文档拆成“当前实现/API”和“目标设计/待实现”，并让每个 API 表只描述一个状态？

我的答复：按本设计文档将待实现 API 全部落地，包括单请求异步 TX、RX session 和
aUsartTxQueue；实现必须遵守文档中的 buffer 所有权、队列 FIFO、取消和错误语义。

### 6. aModbus 模块名与当前功能范围

现状：[`func/aModbus/aModbus.c`](func/aModbus/aModbus.c) 当前提供 CRC16 计算和 RTU 帧 CRC 校验，没有实现 RTU 收发、主从状态机或完整协议栈。

影响：模块名容易被理解为完整 Modbus 功能，但实际是基础帧工具。若当前范围有意保持精简，这不一定是代码缺陷，主要是模块定位与命名/说明需要一致。

待讨论：保留 `aModbus` 并明确它目前只是公共基础工具，还是将 CRC/帧校验收敛到更窄的模块名，待完整协议能力落地后再扩展？

aModbus后面再后见，先不做

### 7. aDrv 配置文档与实际 CMake 写法不一致

现状：[`config/aDrv_config.cmake`](config/aDrv_config.cmake) 采用普通 `set()` 配置；[`docs/usart_design.md`](docs/usart_design.md) 的配置示例仍展示 `para_set()`。

影响：同一配置入口出现不同写法，读者不易判断项目是否支持/要求 `para_set()`，也可能复制出已不符合当前构建方案的配置。

待讨论：统一文档示例为当前普通变量写法，并检查其他 CMake 文档是否仍残留旧配置方式。

我的答复：使用普通 `set()`。这些是当前工程的源代码配置，不需要写入
`CMakeCache.txt`；`para_set()` 不是当前配置方案的一部分。设备 README 的旧示例已改为
`set()`。

## 建议的讨论顺序

1. 先确定公共 handle 的稳定性策略，以及设备资源是显式注入还是全局注册。
2. 再确定业务事件的执行上下文和跨 OS 契约。
3. 确定 device 配置是否需要独立于 aDrv 的数据类型。
4. 最后统一模块范围、命名与设计文档状态。

这些问题之间有依赖关系：例如若保留显式静态 handle，隐藏内部字段需要同步确定存储分配方式；若事件回调改为任务上下文，还要确定 aOS 的事件派发能力和资源开销。因此建议逐项决定接口目标，再安排实现，不要仅为形式上的分层增加包装层。

## 本文边界

- 本文聚焦架构和对外接口，不重复列出 DMA 计数、ISR 优先级、Flash 事务锁等实现级审查项。
- 本文没有要求新增 Async/队列 API，也没有要求立即支持 Linux 或裸机后端。
- 所有结论基于当前工作区文件；如果后续接口或文档有改动，应在讨论决策后同步更新本文。
