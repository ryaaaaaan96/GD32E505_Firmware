# aDevUsart 数据路径配置

`aDevUsartRead()`、`aDevUsartWrite()` 和超时语义不随底层数据路径变化。应用通过
`aDevUsartConfig_t.mode` 同时组合一个 TX 模式、一个 RX 模式和可选功能：

```c
config.mode = ADEV_USART_TX_INTERRUPT_BUFFERED |
              ADEV_USART_RX_INTERRUPT_BUFFERED |
              ADEV_USART_OPTION_RX_IDLE;
```

TX 和 RX 分别占用一个掩码字段，同一字段只能出现一个有效值；option 使用独立
flag，可以按位叠加。`aDevUsartInitStatic()` / `aDevUsartCreate()` 会拒绝未知 bit
和字段中的保留值。

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
| `ADEV_USART_RX_DMA_BUFFERED` | 循环 DMA 写入初始化提供的共享 RX ring | 配置 RX 缓冲区、DMA 和中断优先级 |

`ADEV_USART_OPTION_RX_IDLE` 与中断缓冲或 DMA 缓冲 RX 组合，提供空闲通知和事件计数。
IDLE 不改变普通 Read 的返回条件，也不代表完整协议帧。

IDLE 不允许与轮询 RX 组合。GD32 清除 IDLE 标志需要读取数据寄存器，这可能
消耗尚未被轮询接口读取的末字节；该组合会由初始化接口判为无效参数。

## 整体调用流程

1. app 填写 `aDevUsartConfig_t`，提供引脚、波特率、TX/RX 模式和所需缓冲区；
2. `aDevUsartInitStatic()` 或 `aDevUsartCreate()` 调用 aDrv 完成 USART、DMA 路由和硬件 IRQ 初始化；
3. aDev 注册 TXE、TC、RXNE、IDLE 等内部硬件回调，并按模式创建 aOS 等待对象；
4. 任务调用统一的 `aDevUsartRead()` / `aDevUsartWrite()`；轮询模式直接尝试硬件，
   中断模式使用环形缓冲区；Read 收到当前可用数据即返回，不等待凑满；
5. 缓冲区无数据或无空间时，aDev 通过 aOS 等待对象使调用任务进入 Blocked；
6. 硬件 ISR 进入 aDrv，aDrv 调用 aDev 内部回调；aDev 先更新缓冲区和 DMA 状态，
   再唤醒等待任务，并把可选业务事件投递到 aOS deferred-work worker；
7. 任务醒来后重新检查条件并继续 read/write，超时始终按本次调用的总预算计算。

```text
app task -> aDev read/write -> aDrv non-blocking hardware operation
                |                         ^
                v                         |
          aOS wait object            USART/DMA ISR
                ^                         |
                +--- aDev internal callback <- aDrv IRQ dispatch
                             |
                             +-> aOS deferred work -> app event callback (task context)
```

模式是设备实例的运行策略；产品允许的功能由
`config/aclass_config.cmake` 的 aDev 配置区声明，中央 resolver 校验依赖：

```cmake
set(ADEV_USART_REQUESTED ON)
set(ADEV_USART_INTERRUPT_REQUESTED ON)
set(ADEV_USART_DMA_REQUESTED ON)
```

功能组为 INTERRUPT、DMA、ASYNC、RS485，前后缀均为
`ADEV_USART_<功能组>_REQUESTED`。轮询始终可用。
INTERRUPT 包含收发中断和 IDLE 检测；DMA 包含收发 DMA 及 Direct；
ASYNC 控制 WriteAsync / ReadAsync，并依赖 DMA。TX/RX 的具体模式与是否使用 IDLE 在 app 初始化时选择，
不再分别设置 CMake 开关。依赖 OFF 时 CMake 报错，必须显式开启。

关闭的模式在初始化时返回 `A_STATUS_UNSUPPORTED`。关闭 DMA 时 Direct 的声明与实现
均不参与构建，关闭 ASYNC 时异步接口与 TX 队列不参与构建；调用方应按对应 HAS 宏编译。
纯轮询无需 IRQ/DMA 后端。启用功能但硬件不支持时仍返回 UNSUPPORTED。

## 中断回调与任务等待

aDrv 的 TXE、RXNE、TC 和 IDLE 回调只由 aDev 注册。回调先维护环形缓冲区、
DMA 位置和设备状态，再通过 aOS 等待对象唤醒阻塞任务；app 不直接接触硬件
中断标志。

中断缓冲 TX 在缓冲区满时阻塞写任务，TXE ISR 每释放一个位置就发出唤醒；等待
物理发送完成时由 TC ISR 唤醒。中断缓冲 RX 在 RXNE ISR 收到字节后唤醒读取
任务。IDLE 仅为附加事件。
等待对象采用合并通知语义，因此任务被唤醒后始终重新检查实际条件。

### FreeRTOS port 实现要求

aDev 只能调用 aOS 等待对象接口，禁止直接包含 FreeRTOS 头文件或调用
`xTaskNotify*()`。当前 FreeRTOS port 使用任务通知实现等待对象，不再为每个 RX/TX
方向创建二值信号量。具体规则如下：

- `configUSE_TASK_NOTIFICATIONS` 必须为 1；
- `configTASK_NOTIFICATION_ARRAY_ENTRIES` 必须至少为 3；
- notification index 0 保留给 FreeRTOS 默认机制及 stream/message buffer；
- aOS 等待对象独占 notification index 1；aOS deferred-work worker 独占 index 2，
  app 和其他模块不得直接使用这两个 index；
- 每个等待对象同一时刻只允许一个等待任务，第二个等待者返回
  `A_STATUS_BUSY`；
- 等待对象必须保存 `pending` 锁存状态，ISR 在任务登记前到达时也不能丢失事件；
- 任务通知只负责唤醒，不表示一个字节或一次完整传输；任务醒来后必须重新检查
  RX/TX 缓冲区和硬件状态；
- aDev 反初始化前必须保证没有任务仍在等待，并先关闭硬件中断，再销毁等待对象。

FreeRTOS port 的等待对象只动态分配很小的 `waiting_task + pending` 元数据，不创建
Queue/Semaphore 内核对象。事件回调使用 aOS 的全局 deferred-work worker，在任务上下文
调用；每个 USART handle 仅内嵌一个 work item 和事件位图，不创建专属 worker。未来 Linux port 可在相同 aOS 接口下使用 condition
variable/eventfd，裸机 port 可使用事件标志；不得改变 aDev 的调用方式。

纯 polling TX/RX 没有对应的完成通知，仍使用 `aOSYield()` 配合截止时间轮询。
DMA buffered TX 由 TC ISR 回收已传完的 ring 分块并唤醒写任务。

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
| TX interrupt buffered | `TX_SPACE`、`TX_COMPLETE` |
| TX DMA buffered | `TX_SPACE`、`TX_COMPLETE` |
| polling | 当前不产生业务回调 |

事件产生于 ISR 或数据路径，但通过 aOS deferred-work 队列在任务上下文调用；它不是
Linux softirq 的 API 仿制，而是跨 OS 的“延迟到线程上下文执行”抽象。回调不接管用户
缓冲区，也不替代同步 `aDevUsartRead()`/`aDevUsartWrite()`。事件位会合并，因此回调
应将其视为状态变化提示，随后查询/读取实际状态。若将来确有必须在 ISR 内执行的通知，
应单独定义名称明确带 `FromISR`/`ISR` 的接口和 ISR-safe 契约，不复用当前业务回调。

## 单请求异步 TX 与共享 ring 异步 RX

`aDevUsartWriteAsync()` 将用户 buffer 直接交给 aDrv DMA；同一 USART 同时只接受一笔
异步请求。buffer 必须持续有效到完成、超时或取消事件的 callback 返回。callback 始终
在 aOS 工作任务中运行。有限 timeout 从 DMA 启动成功后计时；取消通过 callback 返回
`A_STATUS_CANCELLED` 和实际已传输字节数。当前 GD32 一笔 DMA 长度上限为 65535 字节。

### DMA buffered RX 与 ReadAsync

`aDevUsartReadAsync(handle, &request, &token)` 注册一次数据等待。多个任务可以并发
提交；DMA 将数据写入初始化时提供的共享 RX ring，`Read` 与异步请求竞争同一消费
游标。完成回调使用节点内最多 64 字节的稳定快照，不接收独立 DMA 目标缓冲区。使用
`aDevUsartReadAsyncCancel(handle, token)` 取消指定的等待项。

DMA buffered 模式下，循环 DMA 将接收字节写入初始化提供的共享 RX ring；普通
`Read()` 从 ring 拷贝，`ReadAsync()` 将 ring 中一段数据复制到节点快照后交给 callback。二者共用
消费游标，按 RX mutex 竞争字节，不会重复交付。异步请求组成 FIFO 链表，每个请求
只完成一次，继续接收时重新提交。取消按 token 指定；超时/取消均通过 callback 事件报告。

DMA/USART ISR 只更新进度并投递工作项，callback 在 aOS 工作任务上下文执行。
`event.buffer` 指向快照，offset 为 0；仅在 callback 返回前有效，DMA 写 ring 不会改变快照。
复制前后校验生产游标，发现复制期间覆盖即报告 ERROR，普通 Read 也执行该检查。
callback 不能阻塞工作任务。DMA buffered RX 持续占用接收
DMA，与 `ReadDirect()` 互斥；需要同步零拷贝时初始化选择 polling 或 interrupt RX。
未启用 IDLE 时，Read/ReadAsync 的 DMA 进度通知发生在半满或满中断，数据可见延迟
由 ring 大小与波特率决定；设置 `ADEV_USART_OPTION_RX_IDLE` 可在帧间空闲时提前唤醒。

`aDevUsartTxQueue` 是 `aDevUsart` 内的 FIFO 扩展，用调用者提供的 request 数组保存多个请求，
内部仍由 aDev 单次 DMA 发送。成功提交后每个 buffer 都保持有效到该请求的 callback
返回。队列满时 Submit 返回 `A_STATUS_BUSY`；CancelAll 会对活动与等待中的每个请求
分别产生一次取消终结 callback。业务 callback 不在 ISR 中执行。

```c
static void usartEvent(aDevUsartEvent_t event, void *argument)
{
    AppContext_t *context = argument;

    /* aOS task context: notify/queue application work; keep this callback short. */
    appNotify(context, event);
}

static aDevUsartStorage_t usart_storage;
aDevUsartHandle_t *handle = NULL;
status = aDevUsartInitStatic(&config, &usart_storage, &handle);
if (status == A_STATUS_OK) {
    status = aDevUsartRegisterEventCallback(
        handle, usartEvent, &app_context);
}
```

## 当前 Shell 组合

Shell 使用中断缓冲 TX/RX 和 IDLE 通知。RXNE ISR 将字节存入 RX ring 并唤醒读取任务；
Read 取出当前可用数据后返回。ring 满时丢弃新字节并锁存 overflow 标志。

中断缓冲 TX 和 DMA buffered TX 的 `aDevUsartWrite()` 返回都表示数据已经复制进
软件队列，不代表最后一个停止位已经发出，因此调用者可以立即复用源内存。需要确认
物理发送完成时，应调用 `aDevUsartWaitTransmitComplete()`。

当前板级组合位于 `app/task/system/aclass_system_config.h`。aDev 不包含 DMA handle、
通道或寄存器地址；USART 到 TX/RX DMA 的芯片专用映射和状态由 aDrv 管理。

## Direct 零拷贝接口

`aDevUsartWriteDirect()` 和 `aDevUsartReadDirect()` 将调用者 buffer 直接交给 GD32
DMA，不经过 aDev 的 TX/RX ring，也不复制 payload。Direct 调用持有对应方向 mutex，
函数返回前无论成功、超时还是错误都必须停止 DMA 对 buffer 的访问。

`WriteDirect()` 返回表示 DMA 已消费 buffer，不表示 USART TC；需要等待最后停止位时
继续调用 `aDevUsartWaitTransmitComplete()`。没有对应 DMA 路由的实例返回
`-1/A_ENOTSUP`，可以通过 `aDevUsartIsSupported()` 提前查询 TX/RX Direct 能力。

Direct 不隐式打乱 stream 数据：TX ring 或 DMA buffered 分块尚未清空时返回
`-1/A_EAGAIN`；RX ring 仍有未读数据或其他 RX 请求占用接收方向时同样返回
`-1/A_EAGAIN`。Direct 与另一方向可以并行，例如 Direct TX 不阻止普通 RX。

## 异步请求参数约定

ReadAsync 的请求结构只含等待超时、callback 和用户参数；DMA 使用初始化配置的
`rx_buffer/rx_buffer_size`，不从 ReadAsync 请求取得额外 buffer。ReadAsync 返回
`aDevUsartReadToken_t`，取消时传入对应 token。WriteAsync 与 TX queue 请求仍由调用者
提供 TX payload，且在完成 callback 前保持有效。

```c
static void rx_ready(aDevUsartHandle_t *handle,
                     const aDevUsartRxEvent_t *event, void *argument)
{
    if (event->type == ADEV_USART_RX_EVENT_DATA_READY) {
        const uint8_t *data = (const uint8_t *)event->buffer + event->offset;
        consume_or_copy(data, event->length, argument);
    }
}

aDevUsartReadRequest_t request = {
    .timeout = A_TIMEOUT_FOREVER,
    .callback = rx_ready,
    .argument = &app_context,
};
aDevUsartReadToken_t token;
aStatus_t status = aDevUsartReadAsync(usart, &request, &token);
```

每个异步请求只接收一次通知；需要继续读取时重新提交。DATA_READY span 只在 callback
执行期间借用的是稳定快照，不是 DMA ring。普通 Read 与异步等待项共用同一个 ring 消费游标，字节只会交付一次。

## Read 与 ReadDirect

Read 支持轮询、中断 ring 和 DMA ring：无数据时等待首字节，随后返回当前可读数据，
不等凑满。DMA ring 由设备初始化配置提供，并与 ReadAsync 共用。
ReadDirect 只走 DMA：等待收满或总超时，IDLE 不提前完成；超时有数据返回部分长度，
无数据返回 -1/errno。DMA buffered RX 持续占用 DMA，因此 ReadDirect 与该模式互斥。
其他模式下 ReadDirect 返回前停止 DMA；已有 ring 数据时返回 BUSY。
NO_WAIT 仅作立即检查；零长度返回 0。超过 65535 字节会分段，分段存在接收间隙。

## 应用设备映射

device 层不管理产品编号或全局设备目录。应用在 app/devices/usart/app_usart.c
持有私有配置、缓冲区及静态存储，调用 aDevUsartInitStatic 初始化。
业务使用 appUsartInit(id, &handle) 初始化指定实例并获取共享借用句柄。
不使用分散注册或设备链接段。借用方不得 DeInit/Destroy。

完整生命周期与错误处理见 [device_registry.md](../../docs/device_registry.md)。
