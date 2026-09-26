# aDevUsart API 设计与实现说明

## 1. 文档目的

本文记录 `aDevUsart` 公共接口的设计选择、实现语义与后续优化项。Async、DMA 异步 RX
和 TX queue 已实现；当前实现状态及准确调用语义以 `docs/usart_design.md` 和公共头文件为准。

## 2. 已经确认的接口定位

USART 对外提供三种不同的数据所有权模型：

| 类型 | 接口 | Payload 拷贝 | 完成通知 |
|---|---|---:|---|
| 普通流式 | `aDevUsartRead/Write` | 使用内部 ring，有一次拷贝 | 函数返回长度 |
| 同步 Direct | `aDevUsartReadDirect/WriteDirect` | 零拷贝 | DMA 停止访问 buffer 后返回 |
| 异步 | `aDevUsartWriteAsync`、`aDevUsartReadAsync` | TX 零拷贝；RX 借用共享 ring | callback |

本项目中的 Direct 强调 payload 不经过 aDev 内部 ring，也不调用 `memcpy()`。GD32E505
port 使用 DMA 实现 Direct；USART 实例没有对应 DMA 路由时，公共接口仍然存在，但
返回 `A_STATUS_UNSUPPORTED`。

普通 `Read/Write` 不承诺零拷贝，目的是提供容易使用的阻塞字节流接口。Direct 和
Async 是可选的高性能能力，不能在内部静默退化成 ring copy，否则会破坏调用者对
buffer 所有权和性能的判断。

## 3. 两种发送完成必须分开

DMA complete 和 USART TC 不是同一个完成条件：

| 状态 | 含义 | 上层可以执行的操作 |
|---|---|---|
| DMA complete | DMA 不再读取当前 TX buffer | 归还 buffer、启动下一个 DMA 请求 |
| USART TC | 数据寄存器和移位寄存器均为空，最后一个停止位已发出 | RS485 换向、关闭 USART、确认线路排空 |

因此：

- `WriteDirect()` 在 DMA complete 后即可返回；
- 当前 `WriteAsync()` 在 USART TC 确认物理发送完成后调用 callback；
- `aDevUsartTxQueue` 在请求 callback 的任务上下文启动下一请求，因此按 TC 顺序发送；
- `aDevUsartWaitTransmitComplete()` 单独等待 USART TC；
- 不能用 USART TC 代替每个 DMA buffer 的所有权归还事件。

## 4. 当前公共接口状态

当前已经实现：

```c
aSSize_t aDevUsartRead(...);
aSSize_t aDevUsartWrite(...);

aSSize_t aDevUsartReadDirect(...);
aSSize_t aDevUsartWriteDirect(...);

aBool_t aDevUsartIsSupported(...);
aStatus_t aDevUsartWaitTransmitComplete(...);
aStatus_t aDevUsartWriteAsync(...);
aStatus_t aDevUsartReadAsync(...);
aStatus_t aDevUsartReadAsyncCancel(...);
aStatus_t aDevUsartTxQueueSubmit(...);
```

异步 RX 使用初始化提供的 DMA ring；多个 one-shot waiter 通过 token 管理，不配置
每请求 DMA buffer，也没有由 callback 返回值控制的循环模式。
## 5. 当前主要问题

### 5.1 TX DMA 完成通知与 USART TC

当前同步 `WriteDirect()` 通过查询 DMA remaining 判断完成，因此 payload 已经零拷贝，
但任务等待期间仍是协作式查询。普通 DMA buffered TX 也使用 USART TC 回调推进 ring
分块。

存在的问题：

- Direct 等待期间不能真正进入 Blocked；
- DMA buffered 分块之间需要等待 TC，可能产生发送间隙；
- DMA error 不能通过统一完成事件立即上报；
- Async TX 与队列目前可用，但 DMA error 与 USART TC 仍共享设备层查询流程，
  后续可考虑拆分硬件 DMA 完成和物理线路完成事件。

当前 Async TX 由 aDev 在 TC IRQ 查询 DMA remaining，并在线路发送完成后完成请求；
因此用户 buffer 持有时间比“DMA 已停止读取”更长，但语义简单且适合队列串行发送。
如果未来需要 DMA 完成即归还 buffer、同时允许线路并行移出，还需增加明确的 DMA
complete/error 硬件事件，并调整 RS485/TC 状态机，不能把 TC 与 DMA 完成混为一谈。

### 5.2 WriteAsync 能力语义

GD32E505 的 `WriteAsync()` 必须使用 DMA，成功后直接持有用户 buffer，直到 complete、
error、timeout 或 cancel callback。没有 DMA 路由时返回
`A_STATUS_UNSUPPORTED`，不退化成内部复制。

公共能力需要独立定义：

```c
ADEV_USART_CAP_TX_DIRECT
ADEV_USART_CAP_RX_DIRECT
ADEV_USART_CAP_TX_ASYNC
ADEV_USART_CAP_RX_ASYNC
```

即使当前 GD32 的 TX Direct 与 TX Async 都依赖同一 DMA 路由，也不能把两个 capability
合并，因为其他 port 可能具有不同能力组合。

### 5.3 当前事件接口无法完整表达异步结果

现有 `aDevUsartEventCallback_t` 只报告 `RX_READY`、`RX_IDLE`、`TX_SPACE` 和
`TX_COMPLETE`，没有携带 buffer、实际长度和错误状态，不能用于归还零拷贝 buffer。

`WriteAsync()` 应使用 per-operation callback，不占用设备级事件 callback：

```c
typedef struct {
    const void *buffer;
    size_t requested;
    size_t transferred;
    aStatus_t status;
} aDevUsartTxEvent_t;
```

每个成功提交的 buffer 必须且只能收到一次最终 callback。

### 5.4 TX 后台错误缺少恢复接口

DMA buffered TX 的错误目前锁存在 handle 中，但缺少完整的查询、终止和恢复流程。
后续至少需要明确：

- DMA 启动失败时是否回滚刚写入 ring 的数据；
- DMA 中途错误时如何报告已发送长度；
- 残余 ring 数据是保留、丢弃还是允许重试；
- `Abort/Reset` 后如何恢复到可写状态。

已知错误不能使用 `TX_COMPLETE` 事件报告。完成和错误必须是不同结果。

### 5.5 方向状态与硬件引擎状态混在一起

当前 `tx_state/rx_state` 主要表达哪个公共操作正在占用方向，但 `Write()` 返回后，
内部 ring 或 DMA 仍可能继续运行。只检查 `tx_state == IDLE` 不能完整表示 TX 硬件空闲。

建议内部拆分为：

```text
owner state:  NONE / STREAM / DIRECT / ASYNC / QUEUE
engine state: IDLE / DMA_ACTIVE / WAIT_TC / ERROR
```

Direct 启动前还必须检查 stream ring 和 DMA active 状态，不能仅检查 owner state。

### 5.6 Read 与 ReadDirect 的边界

Read 支持轮询、中断 ring 或 DMA ring，读取当前可用内容后返回。DMA buffered RX
使用初始化提供的共享 ring，Read 与 ReadAsync 共用一个消费游标；异步 callback 借用
最多 64 字节的稳定节点快照；复制前后检查生产游标，覆盖时报告 ERROR。ReadDirect 使用 DMA 写入调用者 buffer；DMA buffered RX 持续占用通道，
所以两者不能同时使用。其他 RX 模式下 ReadDirect 返回前停止 DMA。

### 5.7 DeInit 生命周期保护不足

当前 DeInit 会检查 TX/RX state，但检查与新操作进入之间仍存在时间窗口。后续应增加
独立生命周期状态：

```text
UNINITIALIZED -> READY -> CLOSING -> UNINITIALIZED
```

进入 `CLOSING` 后禁止新的 Read/Write/Direct/Async 请求，再终止硬件并销毁等待对象。

### 5.8 源文件职责过多

`aDev_usart.c` 已同时包含生命周期、ring、IRQ、DMA、Direct、超时和事件逻辑。继续
加入 Async 会降低可读性。建议保持同一个 `aDevUsart` library，但拆分为：

```text
aDev_usart.c           公共生命周期与接口入口
aDev_usart_tx.c        普通 TX 与 Direct TX
aDev_usart_rx.c        普通 RX 与 Direct RX
aDev_usart_irq.c       aDrv 内部 callback
aDev_usart_async.c     单请求异步状态机
aDev_usart_internal.h  私有状态和内部函数
```

这些文件仍属于硬件无关的 device 层，不建立 GD32 port 子目录。

## 6. Direct 接口约束

### 6.1 WriteDirect

成功调用期间：

- DMA 直接读取用户 buffer；
- 应用不得修改、释放或复用 buffer；
- 函数返回前必须保证 DMA 已停止访问；
- 返回不保证 USART TC；
- TX stream ring 未排空、已有 Direct/Async 或 Queue 接管时返回 `BUSY`。

### 6.2 ReadDirect

成功调用期间：

- DMA 直接写入用户 buffer；
- 应用不得读取或修改 DMA 正在写入的区域；
- 函数返回实际接收长度；
- timeout 前收到部分数据时返回部分长度；
- RX ring 有未读数据或其他 RX 请求活动时返回 `BUSY`。

### 6.3 Cache 与 DMA 可访问性

零拷贝不只是不调用 `memcpy()`。未来具有 D-Cache 的 MCU port 还必须在 aCore/aDrv
边界处理 cache clean/invalidate，并校验 buffer 所在内存是否能被 DMA 访问。aDev
公共接口不能包含芯片 cache API。

## 7. WriteAsync 接口语义

```c
aStatus_t aDevUsartWriteAsync(
    aDevUsartHandle_t *handle,
    const aDevUsartWriteRequest_t *request);

aStatus_t aDevUsartWriteAsyncCancel(
    aDevUsartHandle_t *handle);
```

调用成功后：

```text
应用拥有 buffer
    -> WriteAsync 成功
aDev/DMA 拥有 buffer
    -> complete/error/timeout/cancel callback
应用重新获得 buffer
```

同一个 aDev handle 同时只执行一个 Async TX。多 buffer 排队由 `aDevUsartTxQueue`
完成，aDev 不在单请求接口中隐藏动态队列。

异步 timeout 从提交成功开始，覆盖 DMA 等待时间。`A_TIMEOUT_NO_WAIT` 不能表示有效
的异步完成期限，应返回 `A_STATUS_INVALID_PARAM`；调用者应使用有限 timeout 或
`A_TIMEOUT_FOREVER`。

## 8. 已实施与后续验证

已实现单请求 Async TX、DMA buffered RX FIFO waiter、静态 FIFO `aDevUsartTxQueue`、
超时/取消结果 callback。后续仍应增加目标板 DMA 实测，以及更系统的队列满、
timeout、CancelAll 与 DMA 错误测试。

## 9. 当前结论

普通流式接口、同步 Direct 和异步接口的职责已经分开。当前 TX Async callback
使用 USART TC 作为终结点，因此 buffer 到物理发送完成才归还；ReadAsync 在初始化
提供的共享 DMA ring 上排队等待，每个 token 只收到一次数据、超时或取消 callback。
Read 与异步请求竞争同一数据流，数据只交给一个消费者。是否增加“DMA complete 即归还”的更高吞吐 TX 语义，
应在目标硬件验证后单独讨论，避免把 USART TC、buffer 归还和线路
排空三种不同语义继续耦合在一起。

## 异步请求参数约定

ReadAsync 使用 `aDevUsartReadRequest_t`（timeout、callback、argument），并通过
`aDevUsartReadToken_t` 输出请求 token；RX DMA ring 在 `aDevUsartConfig_t` 初始化时提供；
WriteAsync 使用 `aDevUsartWriteRequest_t`（buffer、size、timeout、callback、argument）。
TxQueueSubmit 使用 `aDevUsartTxQueueRequest_t`（buffer、size、timeout），request_id 单独作为输出参数；
队列回调仍在队列初始化配置中设置。内部 WriteAsyncQueued 复用 WriteRequest。

所有请求结构体在提交期间复制所需字段，不保存其地址，可以是局部变量；提交返回后可修改或销毁
结构体本身。TX buffer 及 argument 必须保持有效直到完成回调；TX payload 不复制，RX Async 使用稳定节点快照。
NULL 请求返回 INVALID_PARAM。同步 Read/Write/Direct 保留原有简短签名。
