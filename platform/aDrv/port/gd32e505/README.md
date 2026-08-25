# GD32E505 aDrv port

本目录是 GD32E505 的完整芯片适配边界，包含：

- `CMSIS/Device/GD/GD32E50x`：官方 CMSIS Device、system 和 ARM/IAR startup；
- `GD32E50x_standard_peripheral`：完整的官方 SPL Include 与 Source；
- `aDrv_*_gd32e505.c`：硬件无关 aDrv API 的 GD32E505 实现。

厂商文件来自 `GD32E50x_Firmware_Library_V1.7.0`。完整 SPL 包含 28 个官方头文件
和 28 个官方源文件；CMake 当前只编译 MISC、RCU、GPIO、USART、DMA、SPI 和
SQPI。`config/gd32e50x_libopt.h.in` 根据根目录 `config/aDrv_config.cmake` 在
构建目录生成最小 `gd32e50x_libopt.h`，并与 SPL/aDrv 源码选择使用同一组开关。

GCC startup 尚未加入；现有 ARM/IAR startup 保持官方原文件，不能交给 GCC
直接汇编。
