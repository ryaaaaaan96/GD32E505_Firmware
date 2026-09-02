# aDevUsart 整体设计

## 1. 目标与边界

`aDevUsart` 对 app、func 提供与具体 MCU 无关的串口接口，并在 aDrv 的非阻塞
硬件能力之上统一以下使用方式：

- 带内部缓冲的阻塞 `Read/Write`；
- 用户 buffer 直连 DMA 的阻塞 `ReadDirect/WriteDirect`；
- callback 完成的异步 TX 和持续异步 RX；
- 可选的 `aUsartTxQueue` 多 buffer FIFO 发送调度。

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
    +--> aUsartTxQueue --> aDevUsartWriteAsync
```

分层职责：

| 模块 | 职责 |
|---|---|
| aDrv | USART 寄存器、IRQ、DMA 路由和一次非阻塞硬件操作 |
| aDevUsart | 模式校验、流式缓冲、单次 Direct/Async 操作、状态与超时 |
| aUsartTxQueue | 多个零拷贝 TX 请求的严格 FIFO 调度 |
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
              ADEV_USART_RX_DMA_CIRCULAR |
              ADEV_USART_OPTION_RX_IDLE;
```

目标模式定义：

| 配置 | 普通接口的默认数据路径 |
|---|---|
| `ADEV_USART_TX_POLLING` | `Write()` 内部轮询提交 |
| `ADEV_USART_TX_INTERRUPT_BUFFERED` | TX ring 由 TXE IRQ 排空 |
| `ADEV_USART_TX_DMA_BUFFERED` | TX ring 由 DMA 分块排空 |
| `ADEV_USART_RX_POLLING` | `Read()` 内部轮询读取 |
| `ADEV_USART_RX_INTERRUPT_BUFFERED` | RXNE IRQ 写内部 RX ring |
| `ADEV_USART_RX_DMA_CIRCULAR` | DMA 持续写内部 RX ring |
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
| Async RX 启动 | `aDevUsartRxStart` | 否 | 零拷贝 | RX callback |
| Async RX 供 buffer | `aDevUsartRxBufferQueue` | 否 | 零拷贝 | buffer 入队 |
| Async RX 停止 | `aDevUsartRxStop` | 否 | — | RX callback |
| TX FIFO | `aUsartTxQueueSubmit` | 否 | 零拷贝 | queue callback |

aDev 不对外提供 `PollIn/PollOut`。轮询是 aDrv 的实现能力，应用统一使用
`Read/Write`。

## 5. 生命周期接口

```c
void aDevUsartConfigStructInit(aDevUsartConfig_t *config);
void aDevUsartHandleStructInit(aDevUsartHandle_t *handle);

aStatus_t aDevUsartInit(
    const aDevUsartConfig_t *config,
    aDevUsartHandle_t *handle);

aStatus_t aDevUsartDeInit(aDevUsartHandle_t *handle);
```

app 负责填写：

- aDrv 逻辑 USART 实例和 TX/RX 引脚；
- 波特率、校验和停止位；
- 默认 TX/RX mode；
- 普通流接口需要的静态 RX/TX ring；
- IRQ 优先级。

初始化只准备配置选择的能力，不启动一次性 Direct/Async TX。循环 DMA RX mode 可以
在初始化时启动持续接收。

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

`Read()` 尝试取得请求长度；到期前已有数据时返回部分长度，没有数据时返回
`-1` 并设置 errno。

```text
polling:            USART -> CPU -> user buffer
interrupt buffered: USART -> RXNE ISR -> RX ring -> copy -> user buffer
DMA circular:       USART -> DMA -> RX ring -> copy -> user buffer
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

Direct 表示 aDev 直接使用调用者 buffer；GD32 port 使用 DMA，未来 Linux port 可以
映射到其他直接 I/O 机制。公共接口不使用 `Dma` 后缀，避免暴露硬件实现。

| 接口 | Buffer 所有权 |
|---|---|
| `ReadDirect()` 调用期间 | DMA 写入，应用不得访问 |
| `WriteDirect()` 调用期间 | DMA 读取，应用不得修改或释放 |
| Direct 返回之后 | 所有权归还应用 |

同方向已有普通流、Direct 或 Async 操作时返回 `BUSY`。Direct TX 返回只保证 DMA
不再访问 buffer；若要确认线路停止位已发出，继续调用 WaitTransmitComplete。

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
    const void *buffer,
    size_t size,
    aTimeout_t timeout,
    aDevUsartTxCallback_t callback,
    void *argument);

aStatus_t aDevUsartWriteAsyncCancel(
    aDevUsartHandle_t *handle);
```

aDev 同一时刻只执行一个异步 TX。成功返回后直到 complete/abort callback，buffer
归 aDev 所有。第二次直接调用 `WriteAsync()` 返回 `A_STATUS_BUSY`。

TX callback 使用 per-operation 绑定，而不是占用整个 USART 唯一事件 callback，
这样 RX 事件和上层 `aUsartTxQueue` 不会互相覆盖。

`WriteDirect()` 可以复用同一异步 TX 引擎：内部启动异步操作，再通过 aOS 等待对象
等待完成，因此 aDev 只维护一套 direct DMA 状态机。

## 9. 持续异步 RX

RX 不采用重复 `ReadAsync()` 请求。外部字节随时到达，因此使用一个 RX session 和
多个预先提供的空 buffer：

```c
aStatus_t aDevUsartRxStart(
    aDevUsartHandle_t *handle,
    void *buffer,
    size_t size,
    aTimeout_t idle_timeout,
    aDevUsartRxCallback_t callback,
    void *argument);

aStatus_t aDevUsartRxBufferQueue(
    aDevUsartHandle_t *handle,
    void *buffer,
    size_t size);

aStatus_t aDevUsartRxStop(aDevUsartHandle_t *handle);
```

调用规则：

| 操作 | 规则 |
|---|---|
| `RxStart()` | 一个 session 只调用一次；重复调用返回 `BUSY` |
| `RxBufferQueue()` | session 期间可以重复提供空 buffer |
| `RxStop()` | 停止 DMA 并逐个归还仍被持有的 buffer |
| `Read/ReadDirect()` | async RX session 期间返回 `BUSY` |

典型双 buffer 流程：

```text
DMA active: A
empty queue: B

A full -> DMA switches to B -> callback releases A
app processes A -> Queue(A)

DMA active: B
empty queue: A
```

RX callback 事件至少包含：

| 事件 | 含义 |
|---|---|
| `RX_READY(buffer, offset, length)` | buffer 中新增一段有效数据 |
| `RX_BUFFER_REQUEST` | 驱动需要下一个空 buffer |
| `RX_BUFFER_RELEASED` | DMA 不再访问 buffer，可以复用 |
| `RX_STOPPED` | session 已停止 |
| `RX_ERROR` | USART/DMA 接收错误 |

一个 buffer 可以因为多次 IDLE 产生多个 `RX_READY`，只有 `RX_BUFFER_RELEASED` 才
归还所有权。至少准备两个 buffer 才能降低 DMA 切换期间丢数风险。

## 10. aUsartTxQueue

`aUsartTxQueue` 是可选 func 模块，用于在 aDev 单请求 Async TX 之上支持多个
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
      complete A, start B
```

### 10.1 接口

```c
typedef uint32_t aUsartTxRequestId_t;

aStatus_t aUsartTxQueueInit(
    const aUsartTxQueueConfig_t *config,
    aUsartTxQueueHandle_t *handle);

aStatus_t aUsartTxQueueSubmit(
    aUsartTxQueueHandle_t *handle,
    const void *buffer,
    size_t length,
    aTimeout_t timeout,
    aUsartTxRequestId_t *request_id);

aStatus_t aUsartTxQueueCancelAll(
    aUsartTxQueueHandle_t *handle);

aStatus_t aUsartTxQueueWaitDrained(
    aUsartTxQueueHandle_t *handle,
    aTimeout_t timeout);

size_t aUsartTxQueueGetPendingCount(
    const aUsartTxQueueHandle_t *handle);

aBool_t aUsartTxQueueIsIdle(
    const aUsartTxQueueHandle_t *handle);

aStatus_t aUsartTxQueueDeInit(
    aUsartTxQueueHandle_t *handle);
```

第一阶段不提供指定 request ID 的中间删除；先实现 active abort 和 `CancelAll()`。
完整实现验证后再增加 `aUsartTxQueueCancel(id)`，不提前保留空壳接口。

### 10.2 静态存储

应用提供固定描述符数组：

```c
static aUsartTxRequest_t s_tx_requests[4];

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

- 第一个 Submit 在任务上下文启动 DMA；
- 后续 Submit 只进入 FIFO；
- DMA 完成 ISR 结束队首并直接启动下一项；
- app callback 只做轻量通知，协议业务回到任务执行。

aDev 必须提供 ISR-safe 的内部链式启动契约，aUsartTxQueue 不得绕过 aDev 调用 aDrv。
具体实现可以是受限的 `aDevUsartWriteAsyncFromISR()`，或者由 aDev completion hook
返回 next buffer。

同一个 USART TX 被 aUsartTxQueue 接管后，直到 QueueDeInit 都禁止直接调用该
handle 的 Write、WriteDirect 或 WriteAsync。RX 方向保持独立，可以并行运行。

### 10.4 完成语义

| 状态 | 含义 |
|---|---|
| request complete | DMA 不再访问这个 buffer，可以归还应用 |
| queue drained | FIFO 为空且 USART TC，最后停止位已经发出 |

DMA 完成后可立即启动下一个 buffer，不在每个请求之间等待 USART TC，避免产生发送
间隙。RS485 方向切换使用 `aUsartTxQueueWaitDrained()`。

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
| RX stream | `Read()` 由 mutex 串行 | Direct/Async 为 `BUSY` |
| RX direct | 不接受第二个操作 | `BUSY` |
| RX async | RxBufferQueue 可继续供 buffer | Read/Direct/RxStart 为 `BUSY` |

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
生命周期由状态机表示。aDev 和 aUsartTxQueue 不能直接包含 FreeRTOS 头文件。

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
- callback 可能来自硬件 ISR，也可能由任务上下文 Cancel 产生；
- callback 必须短小、不可阻塞，并避免直接递归调用同一个控制接口；
- 复杂业务通过 ISR-safe 事件通道通知任务；
- 每个成功提交的零拷贝 buffer 必须且只能收到一次最终归还事件。

未来可以增加 aOS deferred callback，但不得改变 FIFO 顺序和 buffer 所有权语义。

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
| `RxStart/RxBufferQueue()` | RX session 持有空 buffer | RX_BUFFER_RELEASED |
| `TxQueueSubmit()` | TX queue 持有用户 buffer | complete/abort/cancel/timeout event |

## 17. 推荐实施顺序

1. 在 aOS 增加 mutex、IRQ-safe 临界区和 deadline timer 抽象；
2. 给 aDevUsart 增加独立 TX/RX 状态机和内部锁；
3. 稳定普通 Read/Write，完成 TX DMA buffered 路径；
4. 实现单请求 WriteAsync/Cancel 和 per-operation callback；
5. 用同一引擎实现 WriteDirect；
6. 实现 ReadDirect；
7. 实现 RxStart/RxBufferQueue/RxStop 双 buffer session；
8. 在 `func/aUsartTxQueue` 实现静态 FIFO 和 ISR 链式 DMA；
9. 验证 timeout、取消、队列满、DMA 错误、IDLE、tick 回绕和 DeInit；
10. 最后迁移 Shell，并删除被替代的旧字段和旧接口。

当前仓库已有普通 Read/Write、RX DMA circular、TX DMA buffered、内部 IRQ callback、
aOS 等待对象、TX/RX mutex 和独立方向状态。Direct、完整 Async、RX session、
deadline timer 以及 aUsartTxQueue 仍是后续实施项；尚未实现的功能不加入只返回失败
的空壳接口。
