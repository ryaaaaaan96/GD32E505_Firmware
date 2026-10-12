# GD32E505 aDrv

本目录包含 GD32E505 的完整驱动边界：

- `include/`：不含 GD32 类型的公共 aDrv 接口；
- `src/`：GD32E505 实现，按 gpio、dma、usart、spi、qspi 分目录；
- `CMSIS/Device/GD/GD32E50x/`：官方 Device、system、ARM/IAR startup，以及
  基于官方 CL 向量表转换的 GCC startup；
- `GD32E50x_standard_peripheral/`：完整官方 SPL Include 与 Source；
- `templates/`：GD32 `libopt` 头文件模板。
- 项目功能选项位于仓库根目录 `config/`，依赖解析位于根目录 `cmake/`。

厂商文件来自 `GD32E50x_Firmware_Library_V1.7.0`。完整 SPL 包含 28 个官方头文件
和 28 个官方源文件；CMake 根据项目模块开关只编译当前所需部分。生成的
`gd32e50x_libopt.h` 位于构建目录，不修改官方库。SPL 的编译清单消费根配置
resolver 给出的有效模块值，不在 aDrv 目录内补齐跨层依赖。

每个驱动模块独立实现。USART 进一步拆成基础轮询、可选 IRQ 和可选硬件 DMA
源码；未启用能力时不编译对应源码，并通过 aDrv target 的 public compile
definitions 隐藏不可用 API，不提供 stub。DMA 通道和
外设请求映射只存在于 `aDrv_usart_dma.c`，不会暴露给 device 或 app。当前
GD32 实现封装 USART0、UART3、USART5 的 TX 与 RX DMA 通道。UART3 与 USART5
固定共享 DMA1 Channel 4（TX）和 DMA1 Channel 2（RX），通用 DMA 驱动禁止
同一通道被多个句柄占用。当前只为三个支持 DMA 的串口分配适配状态。

USART 中断优先级由 `aDrvUsartConfig_t.interrupt_priority` 配置，默认 5。
`aDrvUsartInitStatic` 在写硬件前校验并保存，之后 USART 回调注册、IDLE、
单次及循环 DMA 接收统一使用该值；回调配置及 DMA 启动接口不再接收优先级。
GD32 要求 0..15，同时受当前 OS ISR 安全范围限制。aOS 构建导出范围，
`platform/CMakeLists.txt` 通过私有编译定义传给 aDrv；驱动不依赖 OS 运行时。
独立编译驱动时须提供 `ADRV_USART_INTERRUPT_PRIORITY_MIN/MAX`，不能省略安全范围。

USART 对设备层使用事件和回调命名：`aDrvUsartEvent_t`、
`aDrvUsartCallbackConfig_t.event`、`aDrvUsartSetEventEnabled`、
`aDrvUsartRequestDispatch`。GD32 内部仍使用 NVIC、外设中断源和 IRQ 入口。
`PauseDispatch/ResumeDispatch` 只控制 USART 事件派发，不暂停 DMA 回调，
不支持嵌套、不等待在途回调退出，也不代替任务互斥。调用细节见公共头。

DMA 中断入口、标志消费和循环计数由 `src/dma/aDrv_dma.c` 统一管理。
`aDrvDmaConfigureInterrupt` 注册 ISR 事件，`aDrvDmaGetProgress` 查询累计搬运
数量；查询先消费事件时会挂起 DMA IRQ，回调不会转移到查询线程。
DMA1 仅有五个通道，逻辑通道范围是 7..11。驱动不依赖 aOS，不执行软件等待。
完整职责和实例设计见 [aDrv 与 aDevice 设计](../../docs/driver_device_design.md)。

## DMA 缓冲区与缓存交接

`aDrv_dma_memory.h` 提供以下接口，不分配内存、不依赖 aOS：

| 接口 | 职责 |
| --- | --- |
| `aDrvDmaBufferCheck` | 检查完整字节范围及 TX/TX ring/RX/循环 RX 用途 |
| `aDrvDmaTxPrepare` | CPU 写入结束后、DMA 发送前同步源数据 |
| `aDrvDmaRxPrepare` | DMA 接收前处理旧缓存状态 |
| `aDrvDmaRxComplete` | DMA 停止后、CPU 访问前同步接收保留区 |

aDev 负责交接时机；直接使用 aDrv DMA 的调用者承担相同责任，驱动启动入口
不重复执行缓存维护。缓存接口须可在 ISR 调用，不阻塞、不分配内存。
GD32E505 的 SRAM 无需缓存维护，后端明确导出 `ADRV_DMA_CACHE_COHERENT=1`，
三个接口使用内联空实现；这不是产品可关闭的安全开关。FMC 的 Flash 读缓存
属于 Flash 控制器，不是 SRAM DMA 缓冲区缓存。未知平台保留函数声明，
必须提供实现，不能默认使用空函数。DMA 启停所需的 DMB/DSB 仍在驱动中执行。
循环 RX 仅接受无需缓存维护的区域，检查通过后不调用 RxPrepare/RxComplete，
包括启动、停止及失败清理；关闭 DMA 后才能释放或复用其内存。

当前可达区域由 `GD32E505_flash.ld` 导出 RAM/主 Flash 的实际起止地址：
RX 和 TX ring 仅允许 RAM；Direct/Async TX 允许 RAM 或主 Flash。
TX ring 由 CPU 填充，因此不能像普通发送源一样接受只读 Flash。
未接入外部内存和地址别名。
范围检查采用减法，拒绝零长度、越界和长度回绕，不验证 C 对象实际容量。
通用 DMA 启动还按两侧数据宽度和递增设置计算完整跨度；固定地址仅检查
一个单元。内存到内存模式的 peripheral 为源、memory 为目标，两侧都检查。
宽度在初始化时限定为 1/2/4 字节，地址设置时检查对应对齐，数量设置时限定
为 1..65535；启动不重复校验这些条件。调用者不得直接修改 handle 内部字段。
硬件分块仅检查可达性，缓存行隔离由调用者对整个保留区检查；不能要求每个
65535 字节硬件块也独占缓存行。循环 RX 另检查无需缓存维护的区域策略。
外设侧仅验证总线窗口和对齐，具体寄存器/请求路由仍由外设驱动保证。
通道启动后须显式 Disable 才可修改地址、数量或循环配置。

可选的静态声明方式如下，当前应用中断收发缓冲区无需改用此宏：

```c
#include "aDrv_dma_memory.h"

static ADRV_DMA_BUFFER uint8_t rx_buffer[256U];
```

宏用于文件作用域无初值数组，指定 `.dma_buffer` 和平台对齐值；GD32 GCC
链接脚本将它纳入 `.bss` 清零范围。此宏不会自动关闭缓存，也不提供动态分配。
带缓存平台移植时必须实现区域属性配置：循环 RX 只允许非缓存/硬件一致区域；
Direct RX 还须检查完整缓存行隔离，不能任意扩大维护范围而影响邻接业务变量。
迁移时应联动修改缓冲区检查、对齐要求、链接布局与缓存实现。

依据：[GD32E50x 用户手册](https://www.gd32mcu.com/data/documents/userManual/GD32E50x_User_Manual_Rev2.2.pdf)、
[Linux DMA 数据交接与缓存行隔离](https://docs.kernel.org/core-api/dma-api-howto.html)。

ARM/IAR startup 保持官方原文件。GCC 构建根据 MCU profile 的
`MCU_STARTUP_VARIANT` 选择 `Source/GCC/startup_<variant>.S`；GD32E505 当前选择
`gd32e50x_cl`。startup 负责 `.data/.bss` 初始化、`SystemInit()`、newlib 构造函数
初始化和进入 `main()`。
