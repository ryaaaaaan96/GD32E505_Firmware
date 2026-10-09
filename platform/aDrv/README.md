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

DMA 中断入口、标志消费和循环计数由 `src/dma/aDrv_dma.c` 统一管理。
`aDrvDmaConfigureInterrupt` 注册 ISR 事件，`aDrvDmaGetProgress` 查询累计搬运
数量；查询先消费事件时会挂起 DMA IRQ，回调不会转移到查询线程。
DMA1 仅有五个通道，逻辑通道范围是 7..11。驱动不依赖 aOS，不执行软件等待。
完整职责和实例设计见 [aDrv 与 aDevice 设计](../../docs/driver_device_design.md)。

ARM/IAR startup 保持官方原文件。GCC 构建根据 MCU profile 的
`MCU_STARTUP_VARIANT` 选择 `Source/GCC/startup_<variant>.S`；GD32E505 当前选择
`gd32e50x_cl`。startup 负责 `.data/.bss` 初始化、`SystemInit()`、newlib 构造函数
初始化和进入 `main()`。
