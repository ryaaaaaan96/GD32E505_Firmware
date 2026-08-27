# GD32E505 aDrv

本目录包含 GD32E505 的完整驱动边界：

- `include/`：不含 GD32 类型的公共 aDrv 接口；
- `src/`：按外设拆分的 GD32E505 实现；
- `CMSIS/Device/GD/GD32E50x/`：官方 Device、system、ARM/IAR startup，以及
  基于官方 CL 向量表转换的 GCC startup；
- `GD32E50x_standard_peripheral/`：完整官方 SPL Include 与 Source；
- `config/`：模块默认值和 `gd32e50x_libopt.h` 生成模板。

厂商文件来自 `GD32E50x_Firmware_Library_V1.7.0`。完整 SPL 包含 28 个官方头文件
和 28 个官方源文件；CMake 根据项目模块开关只编译当前所需部分。生成的
`gd32e50x_libopt.h` 位于构建目录，不修改官方库。

每个驱动模块独立实现。USART 进一步拆成基础轮询、可选 IRQ 和可选 Async-DMA
源码；未启用能力时使用不依赖 DMA/IRQ 的 stub 保持公共接口稳定。DMA 通道和
外设请求映射只存在于 `aDrv_usart_async.c`，不会暴露给 device 或 app。

ARM/IAR startup 保持官方原文件。GCC 构建根据 MCU profile 的
`MCU_STARTUP_VARIANT` 选择 `Source/GCC/startup_<variant>.S`；GD32E505 当前选择
`gd32e50x_cl`。startup 负责 `.data/.bss` 初始化、`SystemInit()`、newlib 构造函数
初始化和进入 `main()`。
