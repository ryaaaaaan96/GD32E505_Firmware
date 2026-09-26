# USART / RS485 统一设备接口

RS485 是 aDevUsart 的可选方向管理功能，不是独立设备，也不是 TX/RX 模式。
旧的 aDev_RS485 目录、aRS485 CMake target 和 aDevRS485* 接口已移除。
APP 统一链接 aDevUsart，使用 aDevUsartConfig_t / aDevUsartHandle_t。
原有 RS485 波特率、校验、停止位改为初始化时填写 drv_config；本次不新增运行期
线路参数修改 API。仓库内没有旧 RS485 API 的业务调用需要迁移。

## 配置

```c
aDevUsartConfig_t config;
aDevUsartConfigStructInit(&config);
/* APP 填写 config.drv_config、TX/RX 模式以及对应缓冲区。 */
config.rs485.enabled = A_TRUE;
config.rs485.de_pin = ADRV_PIN(ADRV_GPIO_PORT_B, 1); /* 示例，按实际板子改 */
config.rs485.de_active_level = ADRV_GPIO_HIGH;
config.rs485.re_pin = ADRV_PIN_NONE; /* DE 与 /RE 绑在一起时只配置 DE */
/* 独立 /RE 可填写 re_pin，默认 re_active_level = LOW。 */
config.rs485.receive_during_tx = A_FALSE;
```

默认关闭，不改变现有 shell 串口配置。引脚不能与 USART TX/RX 重叠，DE/RE
不能重复配置。独立 RE 支持单独有效电平和发送期间保持接收；没有独立 RE 时，
接收是否关闭由实际电路决定，软件不承诺过滤回显。

## 生命周期与发送语义

| 操作 | 方向管理 |
| --- | --- |
| Init | DE 无效；独立 RE 有效 |
| Read / ReadDirect | 不切换方向，不抢占发送 |
| 轮询 Write | 开始发送前使能 DE，返回后由 TC IRQ 释放 |
| 中断缓冲 Write | 入队启动发送时使能 DE，缓冲排空且 TC 后释放 |
| DMA 缓冲 Write | 第一个 DMA 块前使能 DE，后续连续块保持方向，最后 TC 后释放 |
| WriteDirect | DMA 直接读取用户 buffer；返回前停止 DMA 访问，方向仍由最终 TC 释放 |
| WaitTransmitComplete | 等待线路排空并完成方向释放；超时不取消已接受的数据 |
| DeInit | 活跃调用、待发送数据或方向尚未释放时返回 BUSY |

Write 返回长度表示接受的数据量，不等于线路发完。部分写入、等待超时不能立即
关闭 DE；已接受的数据继续发送。Direct 超时会停止 DMA 对用户内存的访问，
但外设数据寄存器/移位寄存器中可能还有尾字节，仍由 TC 完成方向释放。
Direct 返回正长度仍遵循已有的部分成功语义；需要确认线路状态时调用
WaitTransmitComplete。不要用返回长度推断 USART TC。

发送 mutex 串行化任务中的 Write/Direct，USART IRQ 屏蔽保护方向与发送状态的
原子更新；GPIO 操作必须由 aDrv 提供非阻塞、ISR-safe 实现。业务回调看到正常
TX_COMPLETE 时方向已释放。Read 可以等待对端响应，但不会通过读操作关闭 DE。
初始化/反初始化仍须由应用保证没有并发调用。

## 当前边界

- 当前支持 GPIO DE/RE；不实现硬件自动 DE 和收发器建立/保持延迟。
- 开启 RS485 必须支持 USART TC IRQ，缺少该能力返回 UNSUPPORTED，不静默退化。
- Modbus 帧间隔、总线仲裁和请求/响应超时属于协议层。
- 连续的缓冲写入可能合并成一次总线占用，不保证每次 Write 都产生独立方向脉冲。
- 无独立 RX 使能控制时，软件不模拟关闭接收，也不丢弃回显。
- 硬件异常锁存在 tx_error，由等待接口报告；已停止且释放方向后可以 DeInit/Init
  恢复。若硬件故障导致 TC 永远不出现，不保证自动释放线路，不强行截断尾字节。
- Async TX 和发送队列已实现；队列归属 aDevUsart，接口见 `aDev_usart_tx_queue.h`。

内部文件：aDev_usart.c 管理发送状态与 TC，aDev_usart_rs485.c 管理 GPIO，
aDev_usart_internal.h 仅供模块内部使用；公共接口仍只有 aDev_usart.h。

## 验证

运行 `python3 tests/usart/run.py`，以模拟 aDrv/aOS 编译真实 device 源码，覆盖：
三种 TX 模式、部分写入、等待超时、Read 不改方向、DMA 跨环形缓冲尾部续传、
Direct 零拷贝指针及超时停止、正反有效电平、接收保持、引脚冲突与缺失 IRQ。
这不替代板上测量；实板需用逻辑分析仪确认 DE 在起始位前有效、最后停止位后释放，
并测试多任务发送和真实 DMA/USART 中断时序。
