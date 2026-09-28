# aDevUsart 当前设计

## 1. 职责与文件划分

只保留公共管理、TX、RX、RS485 四条实现主线：

```text
device/aDev_usart/
├── include/aDev_usart.h          公共接口
├── src/aDev_usart_internal.h     私有状态与模块内部辅助函数
├── src/aDev_usart.c              生命周期、配置、事件与能力查询
├── src/aDev_usart_tx.c           普通/Direct/Async TX、TX IRQ 与 TC
├── src/aDev_usart_rx.c           普通/Direct/Async RX、RX IRQ 与共享 DMA ring
├── src/aDev_usart_rs485.c        可选 DE/RE GPIO 方向控制
└── CMakeLists.txt
```

只有 include 目录向调用方公开。DMA RX、异步 RX 和 Direct 不再各建文件；
它们按收发方向归属 TX/RX，并继续按能力宏裁剪。TX/RX 状态枚举只在内部头中定义。
aDrv 负责非阻塞硬件操作，aOS 提供等待和时基，app 选择板级资源与创建业务任务。

## 2. 构建能力与实例配置

构建能力分为 INTERRUPT、DMA、ASYNC、RS485；依赖由统一 resolver 校验，不自动开启。
构建能力只是上限；每个实例仍通过 mode 选择独立的 TX/RX 模式和 RX_IDLE 选项。
实例还需具备实际硬件 DMA 路由，不支持时明确返回错误。

当前 Shell 产品只启用 INTERRUPT，TX/RX 使用中断缓冲并启用 IDLE。
DMA、ASYNC、RS485 及对应驱动 DMA 能力默认关闭，但实现保留。
关闭 DMA/ASYNC 时，对应函数声明与实现同时裁剪，不提供成功空实现。
固定静态存储容量仍为 1024 字节，不承诺功能关闭后该容量自动缩小。

## 3. 接口主线

| 接口 | 返回时机 | 数据所有权 |
|---|---|---|
| Read | 有数据可读或等待结束 | 复制到调用者 buffer |
| Write | 当前模式接收数据或等待结束 | 缓冲模式复制；轮询模式直接写硬件 |
| ReadDirect | DMA 收满、出错或总超时 | DMA 直接写用户 buffer；返回前停止访问 |
| WriteDirect | DMA 已消费数据或调用结束 | DMA 直接读用户 buffer；不承诺物理 TC |
| ReadAsync | 请求提交后立即返回 | 共享 DMA ring 中的数据复制到节点快照，再回调 |
| WriteAsync | 请求提交后立即返回 | 用户 buffer 保持有效到终结回调 |
| WaitTransmitComplete | 软件缓冲和物理线路均排空或超时 | 不转移 buffer 所有权 |

普通与 Direct 接口返回实际长度，失败返回 -1 并设置 aOS errno；已传输部分数据时优先返回长度。
异步提交返回 aStatus_t，完成/取消/超时通过请求回调报告，不依赖 worker 的 errno。

## 4. 生命周期

ConfigStructInit 填充默认值；InitStatic 使用调用者存储，Create 使用动态存储。
DeInit 释放内部资源，动态句柄由 Destroy 释放。不得重复初始化活动存储。
外部所有者负责串行管理生命周期：停止新提交并结束在途操作后才能销毁。
worker 内禁止 DeInit；有活动操作或待处理 RX 请求时返回 BUSY。
静态句柄并不代表内部互斥锁、等待对象、异步 RX 节点完全不使用堆。

## 5. TX：单请求，不内置发送队列

设备层已删除 TX Queue、其公开 API 和 Claim/Release/Queued 内部桥接。
WriteAsync 同时只接受一个请求，重复提交返回 BUSY；可用 WriteAsyncCancel 取消。
请求结构体的字段在提交时复制，payload 和 argument 的生命周期必须覆盖回调。
当前 Async TX 基于 DMA，长度为 1..65535，完成以 USART TC 为准。

多条消息排队属于应用策略，当前不额外创建应用队列：

- 最简单的方式是一个 app TX 任务持有串口，从业务队列取出请求，依次同步发送。
- 使用 WriteAsync 时，由完成回调通知该任务继续处理下一项；回调本身不阻塞。
- 应用负责容量、排队超时、重试、取消未提交项，以及 payload 的保留/释放。
- 若要求严格顺序，其他任务不能绕过该发送所有者直接 Write。
- 设备内部 mutex 只保证一次调用的互斥，不保证不同任务的提交顺序。

缓冲 Write 返回不等于最后停止位已发送；需要物理排空时调用 WaitTransmitComplete。
Direct TX 使用 TC 唤醒等待对象，并最多每 10 ms 睡眠检查无中断通知的 DMA 错误；
返回只保证 DMA 不再访问 buffer，不把 DMA complete 和 TC 混为一谈。

## 6. RX：共享缓冲、单次请求

RX_POLLING 不使用内部 ring；RX_INTERRUPT_BUFFERED 使用 RXNE 填充 ring；
RX_DMA_BUFFERED 使用初始化时提供的循环 DMA ring。IDLE 仅作为进度通知，不是报文边界承诺。

ReadAsync 只支持 RX_DMA_BUFFERED，注册一次等待并返回 token；取消指定请求使用
ReadAsyncCancel。多个等待项按 FIFO 分配数据，与 Read 竞争同一消费游标，不是广播。
DATA_READY 回调持有节点内最多 64 字节稳定快照，offset 为 0，指针只在回调期间有效。
复制前后校验 DMA 游标；检测到覆盖时不发布可能撕裂的数据，并报告错误。
持续读取需重新提交，callback 返回值不控制循环接收。

ReadDirect 使用有限 DMA 直接写调用者 buffer，由 DMA 完成/错误唤醒等待任务。
收满或总超时结束，IDLE 不提前结束；NO_WAIT 只检查即时进度并停止。
它不能抢占正在运行的 DMA RX ring，也不会丢弃中断 ring 中已收到的数据。
超过 65535 字节分段重装 DMA，存在接收间隙，不承诺连续流无丢包。

## 7. 回调与并发

业务事件和异步结果均在 aOS 共享 worker 上下文执行，不在硬件 ISR。
回调必须短小且不阻塞，耗时处理转交 app 任务。
RX/TX 分别持有互斥锁，可全双工运行；同方向 Direct/Async/stream 有明确占用检查。
RegisterEventCallback 用于通用事件，请求结果 callback 由每次异步请求指定。
通用事件允许合并，不替代精确的异步请求完成结果。
worker 是平台服务；func 不创建任务，业务任务仍由 app/task 管理。

## 8. RS485 与剩余边界

RS485 集成在 USART 配置中，管理 GPIO DE/RE；不管理 Modbus 协议或总线仲裁。
DE 释放依据 TC，而不是 DMA 完成。硬件自动 DE、建立/保持延迟尚未实现。
当前只有 GD32/FreeRTOS 后端验证；Linux/裸机未实现。
TX DMA error 尚无统一即时通知，Direct 使用有界睡眠检查。
共享 DMA ring 消费太慢仍可能溢出；中断不能长时间饿死。
真实 DMA、TC、DE 时序仍须上板验证。

## 9. 验证

- tests/usart/run.py：真实 device 源码与模拟 aDrv/aOS，覆盖收发、Direct、Async、RS485、FIFO。
- tests/config/build_matrix.py：六种 USART 能力组合及数据库后端，检查编译/链接/库符号裁剪。
- tests/app_devices/run.py：系统设备初始化、失败与 Shell 关闭。
- tests/architecture/run.py：func 任务所有权和 OS 头文件隔离。

当前不再测试已删除的 TX Queue API；aLib 通用 FIFO 保留，供应用或其他模块复用。

## 10. API 索引

完整签名、参数和错误见 [aDev_usart.h](../device/aDev_usart/include/aDev_usart.h)，
下表函数均使用 aDevUsart 前缀；不在文档维护另一份函数声明。

| 分组 | 接口 |
|---|---|
| 生命周期 | ConfigStructInit、InitStatic、Create、DeInit、Destroy |
| 普通流 | Read、Write |
| 同步零拷贝 | ReadDirect、WriteDirect |
| 单次异步 | ReadAsync、ReadAsyncCancel、WriteAsync、WriteAsyncCancel |
| 通用事件 | RegisterEventCallback、UnregisterEventCallback |
| 线路完成 | WaitTransmitComplete |
| 能力/诊断 | IsSupported、GetIdleEventCount、HasRxOverflowed、ClearRxOverflow、GetRxError |

WriteAsync 的请求包含 buffer、size、timeout、callback、argument；
ReadAsync 请求包含 timeout、callback、argument，单独输出 token。
请求结构字段提交时复制，但引用的 payload/argument 必须满足生命周期要求。

## 11. RS485 方向控制

RS485 不是独立 target 或收发模式，通过 config.rs485 配置 GPIO DE/RE。
启用前必须具备 USART TC IRQ 能力；不支持时明确失败。
DE/RE 不得与 TX/RX 重叠，也不能重复配置；独立 RE 可指定有效电平及发送期间接收。
没有独立 RE 时，是否关闭接收由电路决定，软件不模拟关闭接收或过滤回显。

Init 使 DE 无效、独立 RE 有效；发送开始前使能 DE，最终 TC 后释放。
Read 不改变方向。缓冲写入可能合并成一次总线占用，不保证每次 Write 独立产生 DE 脉冲。
Write 超时不撤回已接受的数据，WaitTransmitComplete 超时也不强制释放方向；
Direct 超时停止 DMA 访问，但 USART 内尾字节仍待 TC 完成。
正常 TX_COMPLETE 事件发生时方向已释放。

方向未释放、仍有发送数据或活动调用时，DeInit 返回 BUSY。
硬件故障导致 TC 永不出现时不承诺自动释放线路。
硬件自动 DE、建立/保持延迟未实现；Modbus 帧间隔和总线仲裁归协议/应用层。
上板须测量 DE 在起始位前有效、最后停止位后释放，主机测试不能替代时序验证。
