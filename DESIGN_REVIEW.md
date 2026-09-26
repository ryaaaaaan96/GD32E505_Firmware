# GD32E505_Firmware 架构与接口问题讨论稿


## 应用设备映射：显式初始化

设备映射已移至 app/devices，使用普通 C 配置与 switch，不再使用分散注册。
appUsartInit/appLedInit 按实例初始化并返回私有静态句柄。跨设备资源冲突不在运行时检查，未来可加入构建期告警。
device 层仅保留通用设备初始化及操作。失败不回滚，重复初始化不重试；启动前单线程调用。
详见 [应用设备映射设计](docs/device_registry.md)。

## 2026-09-25：构建体系与模块边界复审

本节描述本轮实际修改与剩余问题。下方保留之前的讨论和用户回复作为历史记录；
其中 aMemory、RX session、独立 aUsartTxQueue 等描述不代表当前结构。

### 本轮结论与修改

| 问题 | 判断与处理 |
|---|---|
| func/aUsartTxQueue 专门调度 USART，device 却公开配套 owner 接口 | 属于设备发送管理，移入 device/aDev_usart；删除独立 library、目录内容及 REQUESTED 开关 |
| 移动后如何使用队列 | 编入 aDevUsart；扩展头 aDev_usart_tx_queue.h，API 改为 aDevUsartTxQueue*；旧接口不保留。FIFO 能力保留，队列对象暂仍显式初始化 |
| 跨模块内部接口公开 | Claim/Release/Queued 提交接口移入 aDev_usart_internal.h，只供同一模块源文件使用 |
| resolver 混入产品绑定 | LED 必选、Shell 使用串口等检查移入 app/devices；Shell 模块本身不绑定 USART |
| 配置边读取边检查，错误可能被前置依赖掩盖 | 两阶段处理：所有输入先校验并归一，再按模块检查依赖；缺失、非法配置显式失败 |
| 子模块依赖 resolver 私有循环变量 | USART CMake 自行声明其消费的能力组，不读取 _ADEV_USART_FEATURES |
| aclass_select 使用 CACHE FORCE | 工程选择改为普通变量，保留必要的编译器路径 cache 和构建身份记录；拒绝已有构建目录切换 MCU/toolchain |
| 后端 include 沿 PUBLIC 链传播 | aOS 的生成配置/port 改 PRIVATE，aOS/aDrv 对 aCore 改 PRIVATE；公共头实际使用的 aLib/aDrv 类型仍保留 PUBLIC |

队列不再是 func 功能模块，但本轮没有强行把所有 Submit 融入 WriteAsync：
现有有界 FIFO、独占 TX、取消等行为继续保留。将队列对象完全收进 USART handle
还需统一容量、超时起点、关闭等待以及回调重入语义，不能只移动结构体就宣布完成。

### 仍需解决的问题（按优先级）

1. **能力裁剪已修复。** 源文件与公共状态机按 INTERRUPT/DMA/ASYNC/RS485 裁剪；
   纯轮询不再要求 IRQ/DMA。关闭能力时 Direct/Async 声明与实现同时移除，无 stub。
2. **队列上下文已统一。** Submit/CancelAll 只登记并投递工作，正常完成、启动失败、
   排队超时和取消均在 aOS worker 回调。回调执行计入 pending，DeInit 会等待工作项退出；
   生命周期仍由一个外部所有者管理，销毁不能与新调用并发。
3. **RX 借用覆盖风险已修复。** 异步节点包含最多 64 字节稳定快照；不向回调出借活跃
   DMA ring。Read/ReadAsync 复制前后检查 DMA 游标，覆盖期间复制的字节不发布。
   消费太慢仍可能丢数据并锁存 overflow；硬件中断不能长时间饿死，不能承诺无限流无损。
4. **旧测试已迁移并通过。** RX 测试改用共享 ring/token，包含取消、超时、快照稳定性、
   复制期间覆盖检测和 Read/Direct 互斥；保留 TX/RS485 回归，补充队列失败路径测试。
5. **aOS 只有 FreeRTOS 实现。** 接口无 FreeRTOS 类型是基础，但 Linux/裸机适配还需要
   定义等待对象、时间回绕、worker 退出和线程安全契约；尚不能宣称已跨 OS 验证。
6. **上游与公共接口尚未物理隔离。** aOS/include 同时放置 aOS.h 和 FreeRTOS 头；
   aCore 同时负责 CMSIS 与 GCC runtime；aDataBase 固定包含 Flash25Q adapter。
   当前可用，但 Linux/其他存储后端接入时应按 backend 拆分，避免新增无实际用途的层。
7. **构建仍有项目级硬编码。** GCC 规则、GD32 vendor 路径、FreeRTOS port 与 MCU
   profile 绑定，debug.py 固定 ELF 名；全局告警选项也作用于上游。后续应提取后端
   选择和固件产物信息，按目标区分自有代码与上游策略。
8. **配置验证已扩充。** 六种 USART 配置分别构建并检查库符号，覆盖纯轮询、中断、
   DMA、异步、RS485、全功能，矩阵使用 Shell OFF。数据库 ON 不属于本轮修改范围。

### 通用 FIFO 的最终归属

`platform/aLib/include/aFifo.h` 提供调用者存储、有界 FIFO、按值复制元素的通用容器，
不包含 USART、DMA、aOS、锁或动态分配。USART 队列复用它保存请求描述符，
payload 不复制；回调、取消、deadline、TX 独占与工作项仍属于 device。

### 边界原则

- aLib：通用类型、状态、时间计算，不依赖 OS 或硬件。
- aCore/aDrv：架构适配和硬件操作，不管理业务请求排队。
- aOS：等待、互斥、时基、内存及 deferred work 的平台适配。
- device：设备实例、缓冲、同步/异步请求、TX 排队、RS485 收发方向。
- func：协议、Shell、数据库；通过设备接口或注入的传输接口使用资源。
- app：板级资源选择、初始化顺序和业务任务。

### 本轮验证

- Debug 固件构建通过；配置解析正反例通过。
- 主机 USART/RS485/队列和通用 FIFO 测试通过；六种能力配置构建/符号检查通过。未进行硬件验证。

---

## 历史讨论（保留用户回复）

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

现状：USART/DMA ISR 更新设备状态后，通过 aOS work item 将业务事件投递到任务上下文；回调事件位可能合并。当前实际后端是 FreeRTOS worker，其他 OS 后端尚未实现。

影响：回调不需要承担 ISR-safe 限制，但 deferred-work 生命周期、资源开销和 OS 端口契约必须明确。若未来增加 ISR 回调，应使用显式 ISR API，不应改变当前回调语义。

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

推荐保留当前嵌套：`aDrvUsartConfig_t` 表达由应用选择的硬件实例、引脚和线路参数，
`aDevUsartConfig_t` 管理设备级 TX/RX 模式、缓冲区和 RS485 行为。aDrv 配置是本仓库的
跨芯片契约，不是 GD32 类型；重复定义并逐字段转换会形成两份需要同步演进的结构。
只有当 aDev 承诺不暴露实例/引脚等硬件资源，或要支持非 USART 后端时，再抽离独立配置。

### 5. USART 设计文档混合了规划接口与当前接口

现状：USART 的同步、异步和队列接口都已落入源码；设计边界与剩余硬件验证项见
[`docs/usart_design.md`](docs/usart_design.md) 和 [`docs/usart_api_design.md`](docs/usart_api_design.md)。

影响：读者可能把设计目标误认为现有可调用 API，进而按不存在的接口编写应用，或者误判实现完成度。

待讨论：是否把文档拆成“当前实现/API”和“目标设计/待实现”，并让每个 API 表只描述一个状态？

我的答复：按设计文档落地单请求异步 TX、RX session 和 aUsartTxQueue；实现必须遵守
buffer 所有权、队列 FIFO、取消和错误语义。当前已实现，详细接口状态与实现边界见
[`docs/usart_design.md`](docs/usart_design.md)。回调采用任务/线程上下文；需要 ISR
回调时另设显式 API。

### 6. aModbus 模块名与当前功能范围

现状：[`func/aModbus/aModbus.c`](func/aModbus/aModbus.c) 当前提供 CRC16 计算和 RTU 帧 CRC 校验，没有实现 RTU 收发、主从状态机或完整协议栈。

影响：模块名容易被理解为完整 Modbus 功能，但实际是基础帧工具。若当前范围有意保持精简，这不一定是代码缺陷，主要是模块定位与命名/说明需要一致。

待讨论：保留 `aModbus` 并明确它目前只是公共基础工具，还是将 CRC/帧校验收敛到更窄的模块名，待完整协议能力落地后再扩展？

aModbus后面再后见，先不做

### 7. aDrv 配置文档与实际 CMake 写法不一致

现状：产品与各层功能请求集中在 [`config/aclass_config.cmake`](config/aclass_config.cmake)，跨层依赖由 [`cmake/aclass_resolve.cmake`](cmake/aclass_resolve.cmake) 统一解析；USART 配置示例以 resolver 的有效配置为准。

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
- Async/队列 API 是已选定的目标，但需按可验证阶段实现；Linux/裸机 aOS 后端仍不在本轮。
- 所有结论基于当前工作区文件；如果后续接口或文档有改动，应在讨论决策后同步更新本文。
