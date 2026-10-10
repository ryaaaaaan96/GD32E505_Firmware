# aDrv 与 aDevice 设计

[返回文档索引](README.md)

本文记录当前重构后的实现。代码目录仍使用 `device/`，公共接口使用 `aDev`
前缀。参考 Zephyr 的按外设分类、配置与运行状态分离、DMA 服务复用方式，
保留本工程的显式初始化和 aOS 移植边界。

## 分层与接口

| 层 | 拥有的内容 | 对外行为 |
| --- | --- | --- |
| aDrv | 寄存器适配、固定路由、IRQ、DMA 通道 | 非阻塞硬件操作和 ISR 通知 |
| aDevice | 缓冲、事务、等待、互斥、器件协议 | 按设备类别提供强类型接口 |
| app/devices | 引脚、设备实例、缓冲区、产品配置 | 显式编排初始化，业务借用实例 |

依赖保持 `device -> aDrv + aOS`。aDrv 不使用 aOS、堆、任务、软件超时，
短临界区只保护驱动与 ISR 共用的状态。aDevice 不包含芯片厂商头文件。

USART 保留 Read/Write，Flash 保留 Read/Write/Erase，LED 保留 Set/Toggle。
函数在编译时绑定当前实现，不增加所有设备共用的 void 指针操作表。SPI 和
QSPI 也不合并成串口字节流：SPI 包含双向事务和片选，QSPI 包含指令、地址、
线宽等控制阶段。

## 配置、运行状态与存储

Zephyr 将只读配置、实例运行数据和设备类别 API 分开。本工程采用相同的职责
划分，同时保留现有初始化函数允许局部配置对象的契约。

- 固定寄存器、时钟、IRQ、路由映射保留在 aDrv 的静态只读表或芯片实现中。
- 输入配置只在初始化/注册期间读取，需要长期使用的字段由实例复制保存。
  不把应用栈上的 `config` 指针长期留在句柄中。
- 缓冲区和回调参数仍是借用对象，必须存活到对应传输、回调或实例生命周期结束。
- 业务头声明不透明设备句柄；实际静态布局在 `*_instance.h` 中，创建层才包含。
- 静态创建和动态创建使用相同的真实实例类型；动态标记只决定 Destroy 的所有权。
  静态句柄仍可能创建动态 OS 对象，不等同于系统完全无堆。

USART 的 RAM 实例现在按下面的结构组织，各部分直接内嵌，不额外分配：

```text
aDevUsartHandle
├── drv_handle                 硬件实例状态
├── settings                   初始化后固定的模式、缓冲区、字节钩子、DE 配置
├── tx                         TX 缓冲进度、等待、超时和异步事务
├── rx                         RX 缓冲进度、等待、订阅和错误
├── de_gpio / rs485_transmitting
└── dynamic_storage            对象分配的所有权
```

`settings` 是句柄内的配置快照，实际位于 RAM；并未宣称它已移入 Flash。
可运行时修改的波特率等信息由 aDrv 的运行状态维护。

USART 不创建任务互斥锁。同方向 API 的串行化与生命周期协调由应用负责；
设备内部仍保留 ISR/DMA 临界区、忙状态检查、完成通知和超时。
当前控制台由 Shell 任务收发，其他任务只向 Shell 队列提交输出；
RS485 由单一 Modbus 协议任务使用，因而两者均不额外创建串口锁。
新增共享调用者时，在 app/devices 的统一入口实施互斥，不能绕过该入口。
全双工可分别设置一个发送者和接收者；RS485 需要协调完整请求响应事务。
LED 使用业务头中的不透明句柄和独立的实例头，字段直接平铺。
Flash25Q 已区分共享 Bus 和各 Flash 实例，继续保留现有结构和 SFUD 移植边界。

## LED 实例与状态

`aDevLedConfig_t` 描述引脚、有效电平与初始亮灭状态；初始化期间读取。
句柄保存 GPIO 运行状态与有效电平，动态分配启用时增加所有权标记。
就绪状态以 GPIO 为准，亮灭状态从输出锁存器读取，不另存软件缓存。

| 接口 | 行为 |
| --- | --- |
| `aDevLedInitStatic(config, handle)` | 初始化调用者提供的对象，无堆分配 |
| `aDevLedCreate(config, &handle)` | 用 aOS 分配对象，初始化失败自动释放 |
| `aDevLedDeInit(handle)` | 设置熄灭电平并释放 GPIO，不释放对象存储 |
| `aDevLedDestroy(handle)` | 反初始化并释放动态对象，拒绝静态对象 |
| `aDevLedSet/On/Off` | 将逻辑亮灭转换为引脚输出电平 |
| `aDevLedGet/Toggle` | 读取或翻转输出锁存值，与引脚输入采样分开 |

顶层 `ADEV_LED_STATIC_ENABLE` 和 `ADEV_LED_DYNAMIC_ENABLE` 独立控制分配
接口；模块启用时至少选择一种。当前产品为静态开启、动态关闭，状态灯仍由
`app/devices/system/system_device.c` 持有。纯静态 LED 不依赖 aOS。
旧 `aDevLedInit` 调用改为 `aDevLedInitStatic`。

GPIO 新增 `aDrvGpioReadOutput`；`aDrvGpioRead` 仍读取实际输入电平，
`aDrvGpioToggle` 改为基于输出锁存值翻转。这使输入受外部电路影响时，
LED 的 Get/Toggle 仍描述软件设置的输出；不代表已检测到真实发光状态。

活动实例不得复制、移动或重复初始化，重配静态对象前先 DeInit。
DeInit 之后引脚为浮空输入，实际电平由板级电路决定，不承诺持续熄灭。
动态对象 DeInit 后仍须 Destroy；释放失败保留句柄供重试。

模块没有内部锁、任务或定时器。应用负责同一实例的访问与生命周期串行化；
Toggle 为非原子读改写，不能与其他写入并发。闪烁周期继续由状态任务设置。

## 驱动文件组织

`ADRV_GPIO_SWD_PROTECT_DISABLE` 未配置或为 `OFF` 时，默认保留 SWD 保护。
此时 GPIO 初始化 PA13/PA14 的所有模式均返回 `A_STATUS_UNSUPPORTED`，
检查在硬件写入之前执行，驱动句柄保持不变。在顶层 CMake 设为 `ON`
才关闭这项检查，
不会自动释放 SWJ 调试复用；如需复用引脚，由板级启动代码显式处理。
PA15/PB3/PB4 原有的关闭 JTAG、保留 SWD 行为不变。

```text
platform/aDrv/
├── include/                   公共硬件接口
├── src/
│   ├── aDrv.c / aDrv_basic.c / aDrv_internal.h
│   ├── gpio/aDrv_gpio.c
│   ├── dma/aDrv_dma.c
│   ├── spi/aDrv_spi.c
│   ├── qspi/aDrv_qspi.c
│   └── usart/
│       ├── aDrv_usart.c
│       ├── aDrv_usart_irq.c
│       ├── aDrv_usart_dma.c
│       └── aDrv_usart_internal.h
└── CMakeLists.txt
```

由一个 CMakeLists 消费顶层配置，USART 的 IRQ/DMA 实现按能力开关编译。
文件按职责和规模拆分，不要求所有外设具有相同文件数量。当前 SPI 仍是基础
非阻塞实现，尚未新增 SPI DMA；QSPI 尚未接入 Flash25Q。

## DMA 作为可复用驱动

通用 DMA 驱动负责通道占用、硬件中断入口、事件采集、循环计数和进度快照。
当前 GD32E505 支持 DMA0 七个通道、DMA1 五个通道，非法逻辑通道初始化失败。

新增接口：

- `aDrvDmaConfigureInterrupt(handle, config)`：复制事件位、优先级、回调及参数；
  `config == NULL` 注销。回调只在 DMA ISR 执行，事件是 HALF、COMPLETE、ERROR。
- `aDrvDmaGetProgress(handle, progress)`：查询本次启动的累计搬运数量和当前块
  剩余数量；错误保持到下一次启动。数量单位是配置的数据宽度，USART 使用字节。

查询与 ISR 共用标志消费入口。查询若先消费到硬件事件，会记录待派发事件并
挂起 DMA IRQ，避免任务查询清除标志后漏掉 ISR 通知。事件是可合并的状态提醒，
不是每次边沿独立排队；累计数量通过进度接口查询。

进度查询使用有界的短临界区采样；跨越重装时最多重试八次，仍不一致返回 BUSY，
输出不变。临界区保持调用前的中断屏蔽状态。硬件 DMA 持续运行，若完成标志
超过一整圈都没有被服务，无法仅从单个硬件标志恢复真实圈数。

USART DMA 适配只负责固定请求路由、USART DMA 请求开关、方向占用和通知转接。
只有 USART0、UART3、USART5 有当前实现支持的 DMA 路由，只为它们保留状态。
UART3 与 USART5 的同向共享冲突由通用 DMA 通道所有权阻止。

DMA COMPLETE 表示数据搬运结束，串口线路完成仍以 USART TC 为准。
RS485 的 DE 释放和业务 Async 回调契约沿用原设计；RX 快照检查仍由 aDev 负责。
停止 DMA 后注销通知；销毁实例前调用方必须停止访问，并确保在途回调退出。

## 移植与扩展

新增 MCU 时替换 aDrv 芯片实现和固定映射；板级引脚由应用重新配置。
未来 Linux 可在设备 API 边界提供 POSIX 实现，或为实际使用的驱动接口实现
适配；不需要模拟 MCU 的每个寄存器和 DMA 通道。Linux 实现仍未接入。

后续新增 SPI DMA 时复用本次的 DMA 中断与进度接口。事务片选、同时收发、
最终外设空闲判定仍由 SPI 链路处理，不把它们塞进通用 DMA。
多个 Flash 继续借用共享总线对象，锁和片选的作用范围按 Flash25Q 文档执行。

## 验证与参考

`tests/led/run.py` 使用真实 LED/GPIO 实现和模拟硬件，验证有效电平、
输入与输出锁存值不一致、静态/动态生命周期、失败回收与编译接口裁剪。
`tests/dma/run.py` 使用真实 DMA 和 USART DMA 实现、模拟硬件寄存器，覆盖通道
争用、ISR 与查询交错、重装、错误保持、即时完成和停止后通知清理。
原 USART/RS485、SPI、Flash、Modbus 和启动回归继续验证上层契约。
功能裁剪通过 `tests/config/build_matrix.py` 检查；具体命令见[验证指南](testing.md)。
主机测试和交叉编译不代替实际 DMA、TC、DE 波形与最坏中断延迟测试。

参考资料：

- [Zephyr Device Driver Model](https://docs.zephyrproject.org/latest/kernel/drivers/index.html)
- [Zephyr DMA 接口](https://docs.zephyrproject.org/latest/hardware/peripherals/dma.html)
- [Zephyr GD32 SPI 对通用 DMA 的使用](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/spi/spi_gd32.c)


### GPIO 输出速度

应用通过 `aDrvGpioConfig_t.speed` 选择 LOW、MEDIUM、HIGH 或 MAX，
GD32 分别映射为 2MHz、10MHz、50MHz 和极高速；这些是输出速度等级，
不是信号频率。`ConfigStructInit` 默认 HIGH，保持原有行为。
LED 的 `aDevLedConfig_t.speed` 同样向下传递；输入/模拟模式忽略速度，
但仍检查枚举有效性。已有外设专用引脚配置不受此字段影响。

MAX 在配置输出前开启 AF 时钟和共享 I/O 补偿，检查就绪后配置引脚。
使用最多 100000 次有限轮询，预算不是毫秒；失败返回 TIMEOUT，
引脚和句柄不变，已开启的时钟与补偿保持开启。反初始化不关闭共享补偿。
速度降档会清除该引脚遗留的 SPD 位。板级供电及负载须满足芯片要求，
最高速率仍由芯片规格与电路决定。
