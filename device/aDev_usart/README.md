# aDevUsart 数据路径配置

`aDevUsartRead()`、`aDevUsartWrite()` 和超时语义不随底层数据路径变化。应用通过
`aDevUsartConfig_t.mode` 同时组合一个 TX 模式、一个 RX 模式和可选功能：

```c
config.mode = ADEV_USART_TX_INTERRUPT_BUFFERED |
              ADEV_USART_RX_DMA_CIRCULAR |
              ADEV_USART_OPTION_RX_IDLE;
```

TX 和 RX 分别占用一个掩码字段，同一字段只能出现一个有效值；option 使用独立
flag，可以按位叠加。`aDevUsartInit()` 会拒绝未知 bit 和字段中的保留值。

## TX 模式

| 配置 | 数据路径 | 配置要求 |
| --- | --- | --- |
| `ADEV_USART_TX_POLLING` | 轮询 TBE，逐字节写入 USART | 无额外资源 |
| `ADEV_USART_TX_INTERRUPT_BUFFERED` | 先写入 TX 环形缓冲区，再由 TBE 中断发送 | 配置 TX 缓冲区和中断优先级 |
| `ADEV_USART_TX_DMA_BUFFERED` | 数据复制到 TX ring，再由 DMA 分块排空 | TX ring；aDrv 提供当前 USART 的 TX DMA 路由 |

## RX 模式

| 配置 | 数据路径 | 配置要求 |
| --- | --- | --- |
| `ADEV_USART_RX_POLLING` | 轮询 RBNE，逐字节读取 USART | 无额外资源 |
| `ADEV_USART_RX_INTERRUPT_BUFFERED` | RBNE 中断把字节写入 RX 环形缓冲区 | 配置 RX 缓冲区和中断优先级 |
| `ADEV_USART_RX_DMA_CIRCULAR` | 循环 DMA 直接写入 RX 环形缓冲区 | aDrv 提供当前 USART 的 RX DMA 路由，并配置 RX 缓冲区 |

`ADEV_USART_OPTION_RX_IDLE` 独立于 TX 模式，可与中断缓冲 RX 或循环 DMA RX
组合。启用后，USART IDLE 中断会递增 `aDevUsartGetIdleEventCount()` 的计数；
使用循环 DMA RX 时，IDLE 回调还会提交 DMA 当前写入位置，但不会停止或重新
启动 DMA。

IDLE 不允许与轮询 RX 组合。GD32 清除 IDLE 标志需要读取数据寄存器，这可能
消耗尚未被轮询接口读取的末字节；该组合会由 `aDevUsartInit()` 判为无效参数。

## 整体调用流程

1. app 填写 `aDevUsartConfig_t`，提供引脚、波特率、TX/RX 模式和所需缓冲区；
2. `aDevUsartInit()` 调用 aDrv 完成 USART、DMA 路由和硬件 IRQ 初始化；
3. aDev 注册 TXE、TC、RXNE、IDLE 等内部硬件回调，并按模式创建 aOS 等待对象；
4. 任务调用统一的 `aDevUsartRead()` / `aDevUsartWrite()`；轮询模式直接尝试硬件，
   中断模式使用环形缓冲区，DMA RX 模式由 DMA 直接写 RX 环形缓冲区；
5. 缓冲区无数据或无空间时，aDev 通过 aOS 等待对象使调用任务进入 Blocked；
6. 硬件 ISR 进入 aDrv，aDrv 调用 aDev 内部回调；aDev 先更新缓冲区和 DMA 状态，
   再唤醒等待任务，最后报告可选的业务事件；
7. 任务醒来后重新检查条件并继续 read/write，超时始终按本次调用的总预算计算。

```text
app task -> aDev read/write -> aDrv non-blocking hardware operation
                |                         ^
                v                         |
          aOS wait object            USART/DMA ISR
                ^                         |
                +--- aDev internal callback <- aDrv IRQ dispatch
                             |
                             +-> optional app event callback (ISR context)
```

模式是设备实例的运行策略，能力是否编译进固件由
`config/aDrv_config.cmake` 独立控制：

```cmake
para_set(ADRV_MODULE_USART    1)
para_set(ADRV_USART_INTERRUPT 1)
para_set(ADRV_USART_ASYNC     1)
```

`ADRV_USART_ASYNC=1` 会自动启用 `ADRV_MODULE_DMA`。实例选择了未编译或当前芯片
不支持的数据路径时，`aDevUsartInit()` 返回 `A_STATUS_UNSUPPORTED`。

## 中断回调与任务等待

aDrv 的 TXE、RXNE、TC 和 IDLE 回调只由 aDev 注册。回调先维护环形缓冲区、
DMA 位置和设备状态，再通过 aOS 等待对象唤醒阻塞任务；app 不直接接触硬件
中断标志。

中断缓冲 TX 在缓冲区满时阻塞写任务，TXE ISR 每释放一个位置就发出唤醒；等待
物理发送完成时由 TC ISR 唤醒。中断缓冲 RX 在 RXNE ISR 收到字节后唤醒读取
任务。循环 DMA RX 与 IDLE 组合时，由 IDLE ISR 提交 DMA 位置并唤醒读取任务。
等待对象采用合并通知语义，因此任务被唤醒后始终重新检查实际条件。

### FreeRTOS port 实现要求

aDev 只能调用 aOS 等待对象接口，禁止直接包含 FreeRTOS 头文件或调用
`xTaskNotify*()`。当前 FreeRTOS port 使用任务通知实现等待对象，不再为每个 RX/TX
方向创建二值信号量。具体规则如下：

- `configUSE_TASK_NOTIFICATIONS` 必须为 1；
- `configTASK_NOTIFICATION_ARRAY_ENTRIES` 必须至少为 2；
- notification index 0 保留给 FreeRTOS 默认机制及 stream/message buffer；
- aOS 独占 notification index 1，app 和其他模块不得直接使用该 index；
- 每个等待对象同一时刻只允许一个等待任务，第二个等待者返回
  `A_STATUS_BUSY`；
- 等待对象必须保存 `pending` 锁存状态，ISR 在任务登记前到达时也不能丢失事件；
- 任务通知只负责唤醒，不表示一个字节或一次完整传输；任务醒来后必须重新检查
  RX/TX 缓冲区和硬件状态；
- aDev 反初始化前必须保证没有任务仍在等待，并先关闭硬件中断，再销毁等待对象。

FreeRTOS port 的等待对象只动态分配很小的 `waiting_task + pending` 元数据，不创建
Queue/Semaphore 内核对象。未来 Linux port 可在相同 aOS 接口下使用 condition
variable/eventfd，裸机 port 可使用事件标志；不得改变 aDev 的调用方式。

纯 polling TX/RX 没有对应的完成通知，仍使用 `aOSYield()` 配合截止时间轮询。
循环 DMA RX 未启用 IDLE 时也会保持主动查询 DMA 位置；需要真正阻塞等待时应组合
`ADEV_USART_OPTION_RX_IDLE`。DMA buffered TX 由 TC ISR 回收已经传完的 ring 分块，
释放空间并唤醒写任务。

每个 handle 内部具有独立 TX mutex 和 RX mutex。mutex 覆盖一次完整的
`aDevUsartWrite()` 或 `aDevUsartRead()` 调用，因此多个写任务的数据不会按字节交错，
多个读任务也不会拆分同一个读取请求。获取 mutex 的等待时间属于调用者传入的同一
timeout 总预算；mutex 只在任务上下文使用，ISR 仍只更新环形索引和发出通知。

业务若需要异步事件通知，应在初始化成功后调用
`aDevUsartRegisterEventCallback()`，不再需要时调用
`aDevUsartUnregisterEventCallback()`。再次注册会替换同一 handle 上原有的业务
回调。aDev 只报告以下与硬件无关的事件：

| 事件 | 含义 |
| --- | --- |
| `ADEV_USART_EVENT_RX_READY` | RX 缓冲区出现可读数据 |
| `ADEV_USART_EVENT_RX_IDLE` | 接收线出现 IDLE 边界 |
| `ADEV_USART_EVENT_TX_SPACE` | TX 环形缓冲区释放空间 |
| `ADEV_USART_EVENT_TX_COMPLETE` | 中断缓冲 TX 已物理发送完成 |

不同模式产生的业务事件如下：

| 数据路径 | 可能产生的事件 |
| --- | --- |
| RX interrupt buffered | `RX_READY`；组合 IDLE 时还有 `RX_IDLE` |
| RX DMA circular + IDLE | `RX_READY`、`RX_IDLE` |
| TX interrupt buffered | `TX_SPACE`、`TX_COMPLETE` |
| TX DMA buffered | `TX_SPACE`、`TX_COMPLETE` |
| polling、RX DMA circular without IDLE | 当前不产生业务回调 |

事件回调运行在 ISR 上下文，只能调用 ISR-safe 接口或向任务投递通知，不能执行
阻塞 read/write 或复杂业务。这个回调接口只负责通知，不接管用户缓冲区，也不
替代同步 `aDevUsartRead()`/`aDevUsartWrite()`；业务通常在回调中唤醒自己的任务，
再由任务调用 read/write。

```c
static void usartEvent(aDevUsartEvent_t event, void *argument)
{
    AppContext_t *context = argument;

    /* ISR context: only use the application's ISR-safe notification here. */
    appNotifyFromISR(context, event);
}

status = aDevUsartInit(&config, &handle);
if (status == A_STATUS_OK) {
    status = aDevUsartRegisterEventCallback(
        &handle, usartEvent, &app_context);
}
```

## 当前 Shell 组合

Shell 使用中断缓冲 TX、循环 DMA RX 和 IDLE 检测。发送数据先写入 TX 环形
缓冲区，再由 TBE 中断逐字节送出；接收数据由 DMA 直接写入 RX 环形缓冲区，
中断中没有二次复制。

`aDevUsartRead()` 每次读取前都会主动同步 DMA 当前位置；缓冲区为空时任务进入
Blocked，并由下一次 IDLE 中断唤醒。DMA 每次回绕都会由 aDrv 的完成中断更新
累计接收量。生产速度追上消费速度时，循环 DMA 会覆盖最旧的未读数据；aDev
保留最新一圈数据、移动读取位置，并锁存 RX overflow 标志供应用查询。

中断缓冲 TX 和 DMA buffered TX 的 `aDevUsartWrite()` 返回都表示数据已经复制进
软件队列，不代表最后一个停止位已经发出，因此调用者可以立即复用源内存。需要确认
物理发送完成时，应调用 `aDevUsartWaitTransmitComplete()`。

当前板级组合位于 `app/system/aclass_system_config.h`。aDev 不包含 DMA handle、
通道或寄存器地址；USART 到 TX/RX DMA 的芯片专用映射和状态由 aDrv 管理。
