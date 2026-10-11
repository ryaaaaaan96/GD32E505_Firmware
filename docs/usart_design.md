# USART、RS485 与中断职责

[返回文档索引](README.md)

本文说明层间职责。模式支持、初始化与收发示例统一维护在
[aDevUsart README](../device/aDev_usart/README.md)，完整参数和错误约定见
[公共头文件](../device/aDev_usart/include/aDev_usart.h)。

## 分层和收发路径

| 层级 | 职责 |
| --- | --- |
| aDrv | 配置硬件、IRQ/DMA 分发、非阻塞传输与完成查询 |
| aDevUsart | 收发缓冲、ISR/DMA 状态保护、等待、异步请求和 DE 方向控制 |
| app | 引脚、模式、缓冲区、协议任务、访问串行化及业务排队 |

mode 的 TX/RX 字段分别决定收发后端。普通、Direct、Async 共用该配置，
编译开关决定可用能力；不提供设备内 TX 请求队列。
Write 返回表示数据已提交，DMA 搬运完成也不代表线路完成；需要排空时
使用 `aDevUsartWaitTransmitComplete()`，以 TC 为准。

USART 不创建任务互斥锁。当前控制台由 Shell 独占接收，Shell 与日志共享发送；
app/devices 的 console_write 创建统一的 TX 互斥边界，锁等待计入写入总预算。
日志直接提交串口，不经过 Shell 队列。Modbus 仍独占 RS485，不额外创建锁。
共享端口时，由 app/devices 协调同方向调用、发送完成等待和生命周期。
忙状态检查不保证多任务入口竞争安全，ISR 临界区也不能替代任务互斥。

DMA 中断入口、标志消费和循环计数集中在通用 aDrvDma；USART DMA 文件只负责
路由、请求开关和通知适配。查询先消费事件时仍由 DMA ISR 派发通知。
设备配置和 TX/RX 运行状态的组织见 [aDrv 与 aDevice 设计](driver_device_design.md)。

应用通过 `drv_config.interrupt_priority` 配置 USART/DMA 的共同优先级。
aDrv 初始化时完成硬件范围和 OS 安全范围校验，并把值保存到驱动句柄；
后续 USART 回调、IDLE 和 RX DMA 使用该绑定值。aDev 不保存、读取、传递或
校验优先级。OS 范围由平台构建传给 aDrv，不引入驱动对 OS 运行时的依赖。

aDev 与 aDrv 的交互使用事件、回调和派发命名，硬件 IRQ 命名和操作保留在
GD32 驱动内部。`aDrvUsartEvent_t` 包含 TX_READY、RX_READY、TX_COMPLETE、
RX_IDLE、RX_ERROR 和 DISPATCH，均带 `ADRV_USART_EVENT_` 前缀。
回调配置为 `aDrvUsartCallbackConfig_t`，通过 `event` 字段选择事件。

| 接口 | 职责 |
| --- | --- |
| `aDevUsartRegisterDriverCallback` | aDev 内部登记驱动回调，不是应用回调接口 |
| `aDrvUsartRegisterCallback` / `UnregisterCallback` | 登记、注销指定事件回调 |
| `aDrvUsartSetEventEnabled` | 启用、禁用指定事件 |
| `aDrvUsartEventIsSupported` | 查询当前构建的 USART 事件派发能力 |
| `aDrvUsartRequestDispatch` | 请求 DISPATCH 回调；允许合并，GD32 通过挂起 IRQ 实现 |
| `aDrvUsartPauseDispatch` / `ResumeDispatch` | 短暂暂停、恢复 USART 事件派发 |

Pause/Resume 只影响通过 RegisterCallback 注册的 USART 事件，不包含 DMA
回调，不等待在途回调退出，不支持嵌套或任务互斥；区间内不注册新回调。
这些调整仅改变命名，当前 `FromISR` 回调上下文、同步方式和编译开关保持
原有语义。Linux 的回调与线程同步适配仍需另行实现。

## 回调上下文和数据稳定性

aDrv 事件回调由 aDev 注册，应用不覆盖设备正在使用的回调。
缓冲业务回调通过 ReadAsync / WriteAsync 请求设置，不另外注册 CallbackSet。
需要字节到达时刻的协议可选独占 RX 字节回调模式，在初始化时提供钩子。

- Async 回调统一在 USART/DMA ISR 执行；任务取消和定时服务超时通过
  软件挂起 USART IRQ 派发，不能理解成“谁触发就在哪个上下文回调”。
- IRQ RX 借用 ring 区间，回调返回前保持占用；DMA RX 复制到调用者
  快照区并检查覆盖。两种路径均只保证本次回调期间的数据稳定性。
- 回调必须非阻塞、耗时有界，不得重入 USART API 或销毁句柄。
- Cancel 成功表示已受理，资源必须保留到终态回调退出。

复杂处理由应用通过 ISR 安全通知交给任务。USART 不依赖 workqueue；
需要延后执行时由应用选择通知或可选工作队列。工作队列回调同样不得阻塞，
详细约束见 [aOS](../platform/aOS/README.md)。RX session 是模块 README 中
明确标注的未来设计，目前未实现。

## RS485 与应用实例

GPIO_DE 由 aDev 在发送前置为有效电平，TC 后释放。芯片自动 UART_DE
当前不支持。DE 控制不负责协议帧间隔、回显过滤或总线仲裁。

| 应用 | 端口 | 说明 |
| --- | --- | --- |
| Shell | USART0，PA9/PA10，115200 8N1 | 中断缓冲、IDLE 通知，通过应用回调适配 aStream |
| Modbus Demo | USART2，PC10/PC11，PA15 DE，115200 8N1 | ISR 字节回调收帧，TX 中断缓冲，当前默认从站 1 |

USART2 的 PC10/PC11 重映射与 PA15 的 JTAG 引脚释放由 aDrv 完成，保留 SWD。
Modbus 应用装配见[产品协议](../app/protocol/README.md)，
RTU 分帧与时序接口见 [aModbus](../func/aModbus/README.md)。
USART IDLE 只表示硬件空闲事件，不等同于协议帧结束。

相关主机和构建检查见[验证指南](testing.md)，不替代真实 DMA、TC、DE
波形和最坏 ISR 延迟验证。
