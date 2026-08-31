# aDevUsart 工作模式

`aDevUsartRead()`、`aDevUsartWrite()` 和超时语义在各模式下保持一致，
应用只通过 `aDevUsartConfig_t.mode` 选择底层传输方式。

| 模式 | 发送 | 接收 | 配置要求 |
| --- | --- | --- | --- |
| `ADEV_USART_MODE_POLLING` | 轮询 TBE | 轮询 RBNE | 无额外资源 |
| `ADEV_USART_MODE_DMA_TX` | DMA | 轮询 RBNE | aDrv 提供芯片内部 DMA 路由 |
| `ADEV_USART_MODE_INTERRUPT_IDLE` | TBE 中断 + TX 环形缓冲区 | RBNE 中断 + RX 环形缓冲区，IDLE 中断标记总线空闲 | 配置中断优先级和两个缓冲区 |
| `ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE` | TBE 中断 + TX 环形缓冲区 | 循环 DMA 直接写 RX 环形缓冲区，IDLE 提交写入位置 | 同时启用 USART 中断和异步能力，并配置 TX、RX 两个缓冲区 |

模式是设备实例的运行策略，能力是否编译进固件由
`config/aDrv_config.cmake` 独立控制：

```cmake
para_set(ADRV_MODULE_USART    1)
para_set(ADRV_USART_INTERRUPT 1)
para_set(ADRV_USART_ASYNC     1)
```

`ADRV_USART_ASYNC=1` 会自动启用 `ADRV_MODULE_DMA`；轮询和中断能力均不依赖
DMA。实例选择了未编译的模式时，`aDevUsartInit()` 返回
`A_STATUS_UNSUPPORTED`。

当前 Shell 使用 `ADEV_USART_MODE_BUFFERED_TX_DMA_RX_IDLE`。发送端先把数据
写入 TX 环形缓冲区，再由 TBE 中断逐字节送出；接收端由 DMA 直接采集到
RX 环形缓冲区。发生 IDLE 后，aDev 只根据 DMA 剩余计数提交新的写入位置，
不会停止 DMA、重启 DMA 或在中断中复制数据。`aDevUsartRead()` 也会主动同步
DMA 当前位置，因此读取新数据不依赖 IDLE 事件。

DMA 每次回绕都会由 aDrv 的完成中断更新累计接收量。如果生产速度追上消费
速度，循环 DMA 会覆盖最旧的未读数据；aDev 保留最新一圈数据、移动读取位置，
并锁存 RX overflow 标志供应用查询。

IDLE 是接收帧间空闲事件，不负责发送。中断模式下发送由 TBE 中断推进，
接收字节由 RBNE 中断写入环形缓冲区；每次 IDLE 事件会递增
`aDevUsartGetIdleEventCount()` 的返回值。协议层可利用这个计数判断一段连续
接收是否结束，普通 Shell 仍可继续按字节调用 `aDevUsartRead()`。

DMA 发送在 `aDevUsartWrite()` 返回前会停止使用调用者的发送缓冲区，因此
调用者可以在函数返回后安全复用该内存。发生超时时，函数先停止 DMA，再
返回已经送入 USART 的字节数；若一个字节也未发送，则返回 `-1` 并设置
`aOS` 维护的 errno。

当前板级 Shell 的选择位于 `app/system/aclass_system_config.h`。aDev 不包含 DMA
handle、通道或寄存器地址；USART 到 TX/RX DMA 的芯片专用映射和传输状态
完全由 `aDrv_usart_async.c` 管理。
