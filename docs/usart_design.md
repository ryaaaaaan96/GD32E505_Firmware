# aDevUsart 整体设计

> 状态说明：本文描述当前实现的 USART 设计与公开接口。芯片支持范围仍由 aDrv
> DMA 路由决定；未实现的 OS 后端不应视为可用。

## 1. 目标与边界

`aDevUsart` 对 app、func 提供与具体 MCU 无关的串口接口，并在 aDrv 的非阻塞
硬件能力之上统一以下使用方式：

- 带内部缓冲的阻塞 `Read/Write`；
- 用户 buffer 直连 DMA 的阻塞 `ReadDirect/WriteDirect`；
- callback 完成的异步 TX 和持续异步 RX；
- 可选的 `aDevUsartTxQueue` 多 buffer FIFO 发送调度。

硬件实例、引脚、波特率和工作模式仍由 app 显式配置。工程不依赖设备树，也不做
隐藏的自动初始化。

```text
app / func
    |
    +--> aDevUsart Read/Write/Direct/Async
    |            |
    |            +--> aOS mutex/wait/timeout
    |            +--> aDrv USART/IRQ/DMA
    |
    +--> aDevUsartTxQueue --> aDevUsartWriteAsync
```

分层职责：

| 模块 | 职责 |
|---|---|
| aDrv | USART 寄存器、IRQ、DMA 路由和一次非阻塞硬件操作 |
| aDevUsart | 模式校验、共享 RX/TX ring、同步 Direct、异步 RX waiter FIFO、单请求 Async TX |
| aDevUsartTxQueue | 多个零拷贝 TX 请求的 FIFO 调度 |
| aOS | mutex、等待对象、单调时基、deadline timer 和 ISR-safe 同步 |
| app/func | 硬件参数、静态存储、业务 callback 和协议处理 |

## 2. 三层能力判断

公共 API 始终存在，不因配置关闭而从头文件消失。调用结果由三层状态决定：

| 层次 | 作用 | 典型结果 |
|---|---|---|
| 编译能力 | aDrv 是否编译 IRQ/DMA 实现 | 不支持返回 `A_STATUS_UNSUPPORTED` |
| 实例配置 | 当前 handle 是否准备相应模式和资源 | 未配置返回 `A_STATUS_NOT_READY` |
| 运行状态 | 同方向是否已有操作占用 | 冲突返回 `A_STATUS_BUSY` |

非法 mode bit、缺少必需缓冲区或无效指针返回 `A_STATUS_INVALID_PARAM`。

## 3. 配置模式

TX、RX 各使用一个互斥字段，option 使用独立 bit：

```c
config.mode = ADEV_USART_TX_DMA_BUFFERED |
              ADEV_USART_RX_INTERRUPT_BUFFERED |
              ADEV_USART_OPTION_RX_IDLE;
```

目标模式定义：

| 配置 | 普通接口的默认数据路径 |
|---|---|
| `ADEV_USART_TX_POLLING` | `Write()` 内部轮询提交 |
| `ADEV_USART_TX_INTERRUPT_BUFFERED` | TX ring 由 TXE IRQ 排空 |
| `ADEV_USART_TX_DMA_BUFFERED` | TX ring 由 DMA 分块排空 |
| `ADEV_USART_RX_POLLING` | `Read()` 内部轮询读取 |
| `ADEV_USART_RX_INTERRUPT_BUFFERED` | RXNE IRQ 写初始化提供的 RX ring |
| `ADEV_USART_RX_DMA_BUFFERED` | 循环 DMA 写初始化提供的共享 RX ring；Read/ReadAsync 消费同一游标 |
| `ADEV_USART_OPTION_RX_IDLE` | 使用 IDLE 辅助提交和唤醒 RX |

普通 DMA TX 使用 `ADEV_USART_TX_DMA_BUFFERED`。Direct 是公共操作语义，不作为
普通 `Write()` 的默认 mode 名称。

mode 只决定普通 `Read/Write` 的默认实现和初始化资源。显式 Direct/Async 接口仍需
检查芯片能力和当前运行状态。

## 4. 对外接口总表

| 分类 | 接口 | 阻塞 | 数据复制 | 完成方式 |
|---|---|---:|---:|---|
| 生命周期 | `aDevUsartInit/DeInit` | 是 | — | 函数返回 |
| 普通 RX | `aDevUsartRead` | 是 | RX ring 到用户 buffer | 返回长度/errno |
| 普通 TX | `aDevUsartWrite` | 可能等待空间 | 用户 buffer 到 TX ring | 返回已接受长度/errno |
| 线路排空 | `aDevUsartWaitTransmitComplete` | 是 | — | USART TC |
| Direct RX | `aDevUsartReadDirect` | 是 | 零拷贝 | 返回实际长度/errno |
| Direct TX | `aDevUsartWriteDirect` | 是 | 零拷贝 | 返回 DMA 已消费长度/errno |
| Async TX | `aDevUsartWriteAsync` | 否 | 零拷贝 | callback |
| Async TX 取消 | `aDevUsartWriteAsyncCancel` | 否 | — | 状态 + callback |
| Async RX | `aDevUsartReadAsync` | 否 | 最多 64 字节的稳定快照 | 一次 callback |
| Async RX 取消 | `aDevUsartReadAsyncCancel` | 否 | — | token 指定并回调 |
| TX FIFO | `aDevUsartTxQueueSubmit` | 否 | 零拷贝 | queue callback |

aDev 不对外提供 `PollIn/PollOut`。轮询是 aDrv 的实现能力，应用统一使用
`Read/Write`。

## 5. 生命周期接口

```c
void aDevUsartConfigStructInit(aDevUsartConfig_t *config);
aStatus_t aDevUsartInitStatic(
    const aDevUsartConfig_t *config,
    aDevUsartStorage_t *storage,
    aDevUsartHandle_t **handle);

aStatus_t aDevUsartCreate(
    const aDevUsartConfig_t *config,
    aDevUsartHandle_t **handle);

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);
aStatus_t aDevUsartDestroy(aDevUsartHandle_t *handle);
```

句柄实现保持不透明。`InitStatic()` 使用应用提供的静态存储区；`Create()` 由 aOS
分配私有句柄，必须配对 `Destroy()`。这两种方式都仍会创建 aOS mutex/wait object，
当前 FreeRTOS 后端的这些对象本身使用 RTOS heap；静态句柄不等同于整个设备零堆分配。

app 负责填写：

- aDrv 逻辑 USART 实例和 TX/RX 引脚；
- 波特率、校验和停止位；
- 默认 TX/RX mode；
- 普通流接口需要的静态 RX/TX ring；
- IRQ 优先级。

初始化准备普通收发模式；Direct/Async DMA 在提交请求时启动。

## 6. 普通阻塞流接口

```c
aSSize_t aDevUsartRead(
    aDevUsartHandle_t *handle,
    void *buffer,
    size_t size,
    aTimeout_t timeout);

aSSize_t aDevUsartWrite(
    aDevUsartHandle_t *handle,
    const void *buffer,
    size_t size,
    aTimeout_t timeout);

aStatus_t aDevUsartWaitTransmitComplete(
    aDevUsartHandle_t *handle,
    aTimeout_t timeout);
```

### 6.1 Read

`Read()` 等待首个字节，随后读取当前可用数据立即返回，不等待凑满；无数据且超时
返回 `-1` 并设置 errno。ReadDirect 等待 DMA 收满或超时，部分数据优先返回长度。

```text
polling:            USART -> CPU -> user buffer
interrupt buffered: USART -> RXNE ISR -> RX ring -> copy -> user buffer
```

无数据时，中断数据路径通过 aOS 等待对象进入 Blocked；ISR 更新状态后唤醒。纯轮询
路径使用 aOS deadline 和 yield。

### 6.2 Write

`Write()` 是带内部所有权的流式发送接口。返回表示数据已被设备内部路径接受，调用者
可以立即复用原 buffer，不保证最后一个停止位已经发出。

```text
polling:            user buffer -> CPU -> USART
interrupt buffered: user buffer -> copy -> TX ring -> TXE ISR -> USART
DMA buffered:       user buffer -> copy -> TX ring -> DMA chunks -> USART
```

TX ring 空间不足时可以阻塞，等待时间计入本次调用总预算。需要确认物理线路排空时
调用 `aDevUsartWaitTransmitComplete()`。

## 7. Direct 阻塞接口

```c
aSSize_t aDevUsartReadDirect(
    aDevUsartHandle_t *handle,
    void *buffer,
    size_t size,
    aTimeout_t timeout);

aSSize_t aDevUsartWriteDirect(
    aDevUsartHandle_t *handle,
    const void *buffer,
    size_t size,
    aTimeout_t timeout);
```

Direct 表示 aDev 直接使用调用者 buffer，payload 不经过内部 ring 或 `memcpy()`。
GD32 port 明确使用 DMA 实现；实例没有对应 DMA 路由时接口保留并返回
`A_STATUS_UNSUPPORTED`。公共接口不使用 `Dma` 后缀，避免上层依赖具体 DMA 类型、
通道和控制器。

| 接口 | Buffer 所有权 |
|---|---|
| `ReadDirect()` 调用期间 | DMA 写入，应用不得访问 |
| `WriteDirect()` 调用期间 | DMA 读取，应用不得修改或释放 |
| Direct 返回之后 | 所有权归还应用 |

同方向已有普通流、Direct 或 Async 操作时返回 `BUSY`。Direct TX 返回只保证 DMA
不再访问 buffer；若要确认线路停止位已发出，继续调用 WaitTransmitComplete。

`aDevUsartIsSupported()` 可以在调用前查询 `ADEV_USART_CAP_TX_DIRECT` 和
`ADEV_USART_CAP_RX_DIRECT`。能力查询只说明硬件是否具备 DMA 路由；如果 stream ring
尚未清空、同方向操作正在执行，Direct 仍返回
`BUSY`。

## 8. 单请求异步 TX

```c
typedef struct {
    const void *buffer;
    size_t requested;
    size_t transferred;
    aStatus_t status;
} aDevUsartTxEvent_t;

typedef void (*aDevUsartTxCallback_t)(
    aDevUsartHandle_t *handle,
    const aDevUsartTxEvent_t *event,
    void *argument);

aStatus_t aDevUsartWriteAsync(
    aDevUsartHandle_t *handle,
    const aDevUsartWriteRequest_t *request);

aStatus_t aDevUsartWriteAsyncCancel(
    aDevUsartHandle_t *handle);
```

aDev 同一时刻只执行一个异步 TX。成功返回后直到 complete/abort callback，buffer
归 aDev 所有。第二次直接调用 `WriteAsync()` 返回 `A_STATUS_BUSY`。

TX callback 使用 per-operation 绑定，而不是占用整个 USART 唯一事件 callback，
这样 RX 事件和上层 `aDevUsartTxQueue` 不会互相覆盖。

当前同步 `WriteDirect()` 直接启动 aDrv DMA，并使用统一 deadline 协作式查询 DMA
remaining；整个过程中 payload 不经过 CPU 复制。同步 Direct 和异步 TX 的所有权
互斥由 aDev 的 TX/RX 状态及 mutex 管理。

## 9. 共享 DMA RX ring 与异步读取

选择 `ADEV_USART_RX_DMA_BUFFERED` 后，初始化配置的 `rx_buffer/rx_buffer_size`
是循环 DMA 的唯一 RX ring。普通 `Read()` 从 ring 拷贝到调用者 buffer；
`ReadAsync()` 复制最多 64 字节到请求节点的快照，再通过回调交付。两者由 RX mutex 保护同一消费
游标，因此同一字节只交给一个消费者。DMA 写入由 aDrv 按半满/满中断更新进度；
可选 IDLE 中断用于低延迟唤醒；未启用时 DMA 半满/满中断才通知数据进度，延迟
取决于 ring 容量和输入速率。

`ReadAsync(handle, request, &token)` 在链表尾部提交一次等待项。多个任务可以并发
提交，FIFO 顺序决定异步请求之间的数据分配；每个请求只调用一次 callback，后续
读取需重新提交。callback 返回 `void`，不决定请求是否续期；按 token 调用
`ReadAsyncCancel()` 可取消仍在等待的请求。超时从提交时开始计时，NO_WAIT 在无
可读数据时通过 deferred callback 报告 TIMEOUT。等待节点由 aOS 分配，分配失败时
提交返回 `A_STATUS_NO_MEMORY`。

ISR 不调用业务 callback，只提交 aOS work。DATA_READY 的 buffer 指向节点快照，offset
固定为 0；快照在 callback 期间稳定，返回后失效。Read 与 ReadAsync 都在复制前后检查
DMA 生产游标，复制期间源区间被覆盖则报 ERROR，不交付混杂字节。快照增加一次复制，
ReadDirect 仍直接 DMA 到用户 buffer。连续流可能在消费者过慢时溢出，不承诺无损；
DMA IRQ 必须至少每个 ring 周期获得处理，否则硬件的单个满标志无法记录多圈。
普通 Read 和 ReadAsync 是同一数据流的竞争消费者，谁先取得 RX mutex 谁消费字节；
系统不会广播或复制同一段字节给多个线程。DMA ring 溢出时保留最新容量的数据，
丢失状态通过 `aDevUsartHasRxOverflowed()` / `aDevUsartGetRxError()` 查询。

DMA buffered RX 持续占用接收 DMA，不能同时调用 `ReadDirect()`。需要同步零拷贝接收时，
初始化选择 polling 或 interrupt buffered RX。

## 10. aDevUsartTxQueue

`aDevUsartTxQueue` 是 aDevUsart 内的发送队列扩展，用于在 aDev 单请求 Async TX 之上支持多个
outstanding 零拷贝 buffer。

```text
Submit(A), Submit(B), Submit(C)
              |
              v
FIFO:      [A] -> [B] -> [C]
active:     A
              |
              v
      aDevUsartWriteAsync(A)
              |
        DMA complete ISR
              |
      aOS deferred-work worker
              |
      complete A, start B
```

### 10.1 接口

```c
aStatus_t aDevUsartTxQueueInit(
    const aDevUsartTxQueueConfig_t *config,
    aDevUsartTxQueueHandle_t *handle);

aStatus_t aDevUsartTxQueueSubmit(
    aDevUsartTxQueueHandle_t *handle,
    const aDevUsartTxQueueRequest_t *request,
    uint32_t *request_id);

aStatus_t aDevUsartTxQueueCancelAll(
    aDevUsartTxQueueHandle_t *handle);

aStatus_t aDevUsartTxQueueWaitDrained(
    aDevUsartTxQueueHandle_t *handle,
    aTimeout_t timeout);

size_t aDevUsartTxQueueGetPendingCount(
    aDevUsartTxQueueHandle_t *handle);

aBool_t aDevUsartTxQueueIsIdle(
    aDevUsartTxQueueHandle_t *handle);

aStatus_t aDevUsartTxQueueDeInit(
    aDevUsartTxQueueHandle_t *handle);
```

第一阶段不提供指定 request ID 的中间删除；先实现 active abort 和 `CancelAll()`。
完整实现验证后再增加 `aDevUsartTxQueueCancel(id)`，不提前保留空壳接口。

### 10.2 静态存储

应用提供固定描述符数组：

```c
static aDevUsartTxRequest_t s_tx_requests[4];

config.usart = &s_usart;
config.request_storage = s_tx_requests;
config.request_capacity = 4U;
config.callback = txQueueCallback;
```

队列只复制描述符，不复制用户数据。成功 Submit 后直到该请求的 complete、abort、
cancel 或 timeout 事件，buffer 归队列所有。队列满时返回 `A_STATUS_BUSY`，buffer
仍属于应用。

### 10.3 DMA 推进

队列不创建专用 TX 任务：

- Submit 只登记请求并提交 aOS 工作项，由 worker 启动 DMA；
- 后续 Submit 只进入 FIFO；
- DMA 完成后继续等待 USART TC，由 aOS deferred work 完成请求并启动下一项；
- 正常完成、启动失败、排队超时和取消回调均在 aOS worker 执行。

队列源文件属于 aDevUsart target。公开扩展头为 `aDev_usart_tx_queue.h`，
TX 占用和带 owner 的提交接口仅声明于内部头文件。不提供 ISR 链式提交 API。
FIFO 元素存储复用 aLib/aFifo.h；锁、超时、取消和 DMA 推进属于 device。

同一个 USART TX 被 aDevUsartTxQueue 接管后，直到 QueueDeInit 都禁止直接调用该
handle 的 Write、WriteDirect 或 WriteAsync。RX 方向保持独立，可以并行运行。

### 10.4 完成语义

| 状态 | 含义 |
|---|---|
| request complete | DMA 不再访问这个 buffer，可以归还应用 |
| queue drained | FIFO 为空且 USART TC，最后停止位已经发出 |

DMA 完成后可立即启动下一个 buffer，不在每个请求之间等待 USART TC，避免产生发送
间隙。RS485 方向由 aDevUsart 内部在最终 TC 后自动切换；等待接口仅用于确认
线路排空，不由应用手动切换方向。当前实现与配置见 [RS485 统一设计](usart_rs485.md)。

## 11. TX/RX 状态和冲突

TX、RX 分别维护状态，允许全双工并行：

```c
typedef enum {
    ADEV_USART_TX_IDLE,
    ADEV_USART_TX_STREAM,
    ADEV_USART_TX_DIRECT,
    ADEV_USART_TX_ASYNC,
    ADEV_USART_TX_QUEUE
} aDevUsartTxState_t;

typedef enum {
    ADEV_USART_RX_IDLE,
    ADEV_USART_RX_STREAM,
    ADEV_USART_RX_DIRECT,
    ADEV_USART_RX_ASYNC
} aDevUsartRxState_t;
```

| 当前方向状态 | 同类型后续操作 | 其他同方向操作 |
|---|---|---|
| TX stream | `Write()` 可继续串行调用 | Direct/Async/Queue 为 `BUSY` |
| TX direct | 不接受第二个操作 | `BUSY` |
| TX async | 不接受第二个 aDev async | `BUSY` |
| TX queue | QueueSubmit 可继续入队 | 绕过 Queue 的 TX 操作为 `BUSY` |
| RX stream | `Read()` 由 mutex 串行 | Direct 为 `BUSY`；DMA ring 的 ReadAsync 可排队 |
| RX direct | 不接受第二个操作 | `BUSY` |
| RX async waiter | token 可取消指定等待项 | 同一 RX ring 由 Read/异步请求竞争消费 |

## 12. 锁和 ISR 并发

| 保护对象 | 机制 | 规则 |
|---|---|---|
| 多任务完整 Read 调用 | RX mutex | 覆盖一次完整 Read，防止多消费者拆分字节流 |
| 多任务完整 Write 调用 | TX mutex | 覆盖一次完整 Write，防止消息按字节交错 |
| Direct 调用 | 对应方向 mutex | 从状态检查持有到 Direct 返回 |
| Async Start/Stop/Submit | 对应方向 mutex | 只保护短控制操作，不跨异步生命周期持有 |
| task/ISR 共享索引和状态 | IRQ-safe 短临界区 | 只更新指针、计数和状态 |
| 阻塞等待 | aOS wait object | 负责睡眠/唤醒，不代替 mutex |

ISR 不能获取任务 mutex，也不能替任务释放 mutex。异步函数返回前释放 mutex，后续
生命周期由状态机表示。aDev 和 aDevUsartTxQueue 不能直接包含 FreeRTOS 头文件。

## 13. Timeout

同步接口 timeout 覆盖：

```text
等待 mutex + 等待队列/数据 + 硬件操作
```

所有阶段共享同一个绝对 deadline，不能在获得锁或启动 DMA 后重新计算完整预算。

异步 TX Queue 的 timeout 从成功 Submit 开始，覆盖排队和 DMA 传输。Submit 本身
不等待队列空间，队列满立即返回 `BUSY`。`A_TIMEOUT_NO_WAIT` 不能表达有意义的异步
完成期限，异步接口应拒绝它，调用者使用有限 timeout 或 `A_TIMEOUT_FOREVER`。

硬件 IDLE 只表示一个字符时间的线路空闲，不等价于任意毫秒 timeout。任意 deadline
需要 aOS timer 抽象；FreeRTOS port 可以使用系统 timer service，不创建 USART 专用
数据搬运任务。

## 14. Callback 规则

- callback 事件必须携带 buffer、请求长度、实际长度和状态；
- callback 只通知完成或数据可用，不执行阻塞 Read/Write；
- aDev 的业务事件 callback 统一由 aOS deferred-work 队列投递，在任务/线程上下文
  执行；硬件 ISR 仅更新状态并提交 work item，不直接调用业务 callback；
- callback 必须短小，不应阻塞或从自身调用 DeInit；复杂业务应通知业务任务处理；
- 如果未来需要真正的 ISR callback，必须提供名字和契约明确的 ISR 专用 API，不与
  当前线程上下文 callback 混用。当前不提供该 API；
- 每个成功提交的零拷贝 buffer 必须且只能收到一次最终归还事件。

异步 TX/RX completion callback 也使用同一个 aOS deferred-work 上下文；这避免把
FreeRTOS task/thread 语义泄漏到 aDev，同时也不模仿 Linux softirq。Linux port 可用
工作线程/工作队列实现相同契约，裸机 port 可由主循环轮询 deferred work。

## 15. 错误语义

| 情况 | 状态 |
|---|---|
| 参数、mode、buffer 或 timeout 无效 | `A_STATUS_INVALID_PARAM` |
| 芯片/固件没有对应能力 | `A_STATUS_UNSUPPORTED` |
| handle 未初始化或没有准备对应模式 | `A_STATUS_NOT_READY` |
| 同方向冲突或异步队列已满 | `A_STATUS_BUSY` |
| 有限等待到期 | `A_STATUS_TIMEOUT` |
| DMA/USART 硬件故障 | `A_STATUS_ERROR` 或后续细分状态 |

流式 Read/Write 返回 `aSSize_t`，失败时用 aOS errno；配置、控制和异步提交接口返回
`aStatus_t`。发生部分传输时，事件或返回值必须报告实际长度。

## 16. Buffer 所有权汇总

| 操作 | 成功调用后 | 所有权归还时刻 |
|---|---|---|
| `Read()` | 用户始终拥有目标 buffer | 函数返回 |
| `Write()` | 数据已复制后用户可复用源 buffer | Write 返回 |
| `ReadDirect()` | aDev/DMA 写用户 buffer | 函数返回 |
| `WriteDirect()` | aDev/DMA 读用户 buffer | 函数返回 |
| `WriteAsync()` | aDev/DMA 持有用户 buffer | complete/abort callback |
| `ReadAsync()` | 节点快照在 callback 期间借用 | callback 返回 |
| `TxQueueSubmit()` | TX queue 持有用户 buffer | complete/abort/cancel/timeout event |

## 17. 当前实现状态

当前仓库实现范围：

| 能力 | 源码状态 | 当前边界 |
|---|---|---|
| 普通 `Read/Write` | 已实现 | polling、interrupt buffered、DMA buffered 由 TX/RX flag 选择 |
| DMA buffered RX | 已实现 | 初始化提供共享 ring；Read 与 ReadAsync 使用同一消费游标 |
| `ReadDirect/WriteDirect` | 已实现 | 当前 GD32 通过 DMA 能力实现；无对应路由返回 unsupported |
| Async RX | 已实现 | DMA ring 上 FIFO 等待项；回调只在 aOS task context |
| `aDevUsartTxQueue` | 已实现 | 调用者提供 FIFO 存储；请求按提交顺序串行发送 |

异步 TX 的 DMA/USART ISR 只更新状态并提交 aOS work item；物理发送完成后，callback
在 aOS 工作任务执行。队列因此在任务上下文完成当前请求并启动下一请求，而不是从
ISR 直接调用上层 callback。RX DMA 半满/满或 IDLE 后，aDev 刷新共享 ring 游标并在
aOS 工作任务中派发一个 FIFO waiter；DMA 持续运行，不为每次 callback 重装用户 buffer。

异步 TX buffer 由调用方分配和保持。TX queue 成功 Submit 后，源 buffer 到该请求的
终结 callback 返回前不得修改或释放。DMA RX ring 在初始化时由应用提供，并持续
有效到 DeInit；Async callback 临时借用最多 64 字节的节点快照，不单独持有 DMA 目标缓冲区。

流式 RX 错误通过 `ADEV_USART_EVENT_RX_ERROR` 通知，并可由
`aDevUsartGetRxError()` 查询。ReadAsync 的 DMA backend 错误通过该请求的 ERROR event 报告。

## 异步请求参数约定

ReadAsync 使用 `aDevUsartReadRequest_t`（timeout、callback、argument），token 单独作为输出参数；
WriteAsync 使用 `aDevUsartWriteRequest_t`（buffer、size、timeout、callback、argument）。
TxQueueSubmit 使用 `aDevUsartTxQueueRequest_t`（buffer、size、timeout），request_id 单独作为输出参数；
队列回调仍在队列初始化配置中设置。内部 WriteAsyncQueued 复用 WriteRequest。

所有请求结构体在提交期间复制所需字段，不保存其地址，可以是局部变量；提交返回后可修改或销毁
结构体本身。TX buffer 及 argument 必须保持有效直到完成回调；TX payload 不复制，RX Async 使用节点快照。
NULL 请求返回 INVALID_PARAM。同步 Read/Write/Direct 保留原有简短签名。
