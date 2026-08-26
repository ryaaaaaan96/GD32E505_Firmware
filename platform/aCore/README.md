# aCore

提供与具体 MCU 厂商和型号无关的 Arm CMSIS Core 支持，以及按工具链选择的 C
运行库适配。

具体芯片的 CMSIS Device、startup、system 和厂商外设库由对应的 aDrv port
负责，aCore 不依赖 aDrv。

GCC 使用 `runtime/GCC/syscalls.c` 和 `sysmem.c` 适配 newlib。默认 I/O 钩子返回
`ENOSYS`，app 可覆盖 `aCoreRuntimeRead()` 和 `aCoreRuntimeWrite()`；aCore 本身不
依赖 device、aDrv 或 aOS。newlib heap 严格使用链接脚本提供的
`_heap_start/_heap_end` 边界。
