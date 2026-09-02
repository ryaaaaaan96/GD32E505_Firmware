# aDevUsart 接口问题与重构计划

## 1. 文档目的

本文记录当前 `aDevUsart` 公共接口存在的问题、已经确认的接口语义，以及后续实现
Direct/Async/Queue 时必须遵守的规则。本文用于代码评审和重构跟踪；完整目标架构仍
参见 `docs/usart_design.md`。

## 2. 已经确认的接口定位

USART 对外提供三种不同的数据所有权模型：

| 类型 | 接口 | Payload 拷贝 | 完成通知 |
|---|---|---:|---|
| 普通流式 | `aDevUsartRead/Write` | 使用内部 ring，有一次拷贝 | 函数返回长度 |
| 同步 Direct | `aDevUsartReadDirect/WriteDirect` | 零拷贝 | DMA 停止访问 buffer 后返回 |
| 异步 Direct | `aDevUsartWriteAsync`、RX session | 零拷贝 | callback 归还 buffer |

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
- `WriteAsync()` 在 DMA complete 后调用完成 callback；
- TX queue 在 DMA complete 后立即启动下一请求，不逐项等待 TC；
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
```

当前尚未实现，不能提前增加只返回失败的空壳：

```c
aDevUsartWriteAsync();
aDevUsartWriteAsyncCancel();
aDevUsartRxStart();
aDevUsartRxBufferQueue();
aDevUsartRxStop();
```

## 5. 当前主要问题

### 5.1 aDrv 缺少 TX DMA 完成通知

当前同步 `WriteDirect()` 通过查询 DMA remaining 判断完成，因此 payload 已经零拷贝，
但任务等待期间仍是协作式查询。普通 DMA buffered TX 也使用 USART TC 回调推进 ring
分块。

存在的问题：

- Direct 等待期间不能真正进入 Blocked；
- DMA buffered 分块之间需要等待 TC，可能产生发送间隙；
- DMA error 不能通过统一完成事件立即上报；
- 无法在此基础上可靠实现 `WriteAsync()` 和无间隙 TX queue。

需要由 aDrv 增加一次硬件传输的 complete/error callback。callback 表示 DMA 已经停止
访问 buffer，不表示线路 TC。

### 5.2 WriteAsync 的能力语义尚未落地

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

### 5.6 Direct 与默认 RX circular DMA 冲突

循环 DMA RX 从初始化成功后持续拥有 RX DMA。此时 `ReadDirect()` 不能在不丢失字节流
边界的情况下临时抢占 DMA，因此当前返回 `A_STATUS_BUSY` 是有意设计，不是能力缺失。

如果应用需要 Direct RX，应选择 polling/interrupt buffered 默认 RX，或者直接使用
未来的异步 RX session，不应让 `ReadDirect()` 隐式停止和重启 circular DMA。

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
- RX ring 有未读数据或 circular DMA RX 正在运行时返回 `BUSY`。

### 6.3 Cache 与 DMA 可访问性

零拷贝不只是不调用 `memcpy()`。未来具有 D-Cache 的 MCU port 还必须在 aCore/aDrv
边界处理 cache clean/invalidate，并校验 buffer 所在内存是否能被 DMA 访问。aDev
公共接口不能包含芯片 cache API。

## 7. WriteAsync 目标接口

```c
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

调用成功后：

```text
应用拥有 buffer
    -> WriteAsync 成功
aDev/DMA 拥有 buffer
    -> complete/error/timeout/cancel callback
应用重新获得 buffer
```

同一个 aDev handle 同时只执行一个 Async TX。多 buffer 排队由后续
`aUsartTxQueue` 完成，aDev 不在单请求接口中隐藏动态队列。

异步 timeout 从提交成功开始，覆盖 DMA 等待时间。`A_TIMEOUT_NO_WAIT` 不能表示有效
的异步完成期限，应返回 `A_STATUS_INVALID_PARAM`；调用者应使用有限 timeout 或
`A_TIMEOUT_FOREVER`。

## 8. 推荐实施顺序

1. 给 aDrv TX DMA 增加 complete/error callback，并区分 DMA complete 与 USART TC；
2. 修正 DMA buffered TX 的错误回滚、错误事件和恢复路径；
3. 拆分 `aDev_usart.c`，建立明确的 owner/engine 状态机；
4. 实现单请求零拷贝 `WriteAsync/Cancel`；
5. 让同步 `WriteDirect` 复用 Async 引擎和 aOS 等待对象；
6. 完成异步 RX session 和多 buffer 所有权管理；
7. 在 func 层实现 `aUsartTxQueue`，只存描述符、不复制 payload；
8. 增加 DMA error、timeout、cancel、ring 回绕、并发和 callback-once 测试；
9. 完成目标板上的 Direct/Async 实际 DMA 验证。

## 9. 当前结论

普通流式接口和同步 Direct 的职责已经分开，Direct 也已经实现 payload 零拷贝。
当前最大的接口基础问题是缺少独立的 TX DMA complete/error 通知。在解决该问题前，
不应直接叠加 `WriteAsync()` 或 `aUsartTxQueue`，否则会把 USART TC、buffer 归还和线路
排空三种不同语义继续耦合在一起。
