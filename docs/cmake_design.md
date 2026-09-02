# CMake 最终设计

## 职责划分

| 位置 | 职责 |
|---|---|
| 根 `CMakeLists.txt` | 选择固件名、版本、MCU、链接脚本和工具链；加入各层 |
| `cmake/Aclass.cmake` | 解析选择项、校验配置、设置公共编译规范和固件产物 |
| `cmake/toolchains/GCC.cmake` | 查找 Arm GCC，设置 CPU/FPU、链接器和 binutils |
| `config/mcu_gd32e505.cmake` | CPU/FPU、主频、厂商宏、startup variant 和 FreeRTOS port |
| `config/aDrv_config.cmake` | 选择项目启用的 aDrv 模块 |
| `config/freeRTOS_config.cmake` | 当前工程对 FreeRTOS 默认参数的覆盖 |
| `platform/aCore/CMakeLists.txt` | 导出 CMSIS Core，并按工具链构建运行库适配 |
| `platform/aDrv/CMakeLists.txt` | 生成 libopt 并构建 GD32 vendor 与 aDrv target |
| `platform/aOS/config/` | FreeRTOS 通用默认值和 `FreeRTOSConfig.h` 生成模板 |
| `app/CMakeLists.txt` | 创建最终 ELF、链接所需层并生成固件产物 |

## 工程选择

```cmake
aclass_select(
    NAME io_gd32e505
    VERSION 0.1.0
    MCU gd32e505
    LINKER_SCRIPT GD32E505_flash.ld
    TOOLCHAIN GCC
)

project(${FIRMWARE_NAME} VERSION ${FIRMWARE_VERSION} LANGUAGES C ASM)
aclass_initialize()
```

名称解析规则：

```text
MCU gd32e505            -> config/mcu_gd32e505.cmake
TOOLCHAIN GCC           -> cmake/toolchains/GCC.cmake
LINKER_SCRIPT name.ld   -> 工程根目录/name.ld
```

链接脚本属于项目。CPU/FPU、主频、厂商宏和 FreeRTOS port 属于 MCU profile。
驱动模块使能属于 `config/aDrv_config.cmake`；外设实例、引脚和运行参数属于 app。

## target 关系

```text
aCore (OBJECT: CMSIS include + GCC runtime)
   ^
   |
aDrv_gd32e505_vendor (STATIC)
   ^
   |
aDrv (STATIC) -> aLib
```

`aDrv_gd32e505_vendor` 包含 GD32 CMSIS Device/system，并从完整 SPL 中按模块选择
源码。aDrv 对 vendor target 使用 PRIVATE 依赖，公共接口不会向 device、func 或
app 暴露厂商 include 和宏。aDrv 根据 `MCU_STARTUP_VARIANT` 加入所选工具链的
startup；aCore 使用 OBJECT target，确保 newlib 在库扫描前获得项目 syscall。

## aDrv 配置生成

`platform/aDrv/config/aDrv_defaults.cmake` 声明默认值，项目配置使用
`para_set()` 选择模块。aDrv CMake 对同一组最终值执行：

```text
ADRV_MODULE_* 配置
├── configure_file -> generated/aDrv/gd32e50x_libopt.h
├── 选择 GD32E50x_standard_peripheral/Source/*.c
└── 选择 src/aDrv_*.c
```

生成文件只位于构建目录，官方 SPL 目录始终保持原貌。USART、SPI、QSPI 依赖
GPIO，非法组合在 CMake 配置阶段直接报错。

USART 还提供 `ADRV_USART_INTERRUPT` 与 `ADRV_USART_ASYNC` 两个能力开关。
`ADRV_USART_ASYNC=1` 自动选择 `ADRV_MODULE_DMA`；该能力涵盖 DMA 发送与接收。
关闭时使用 async stub，轮询和
中断构建不会带入 DMA。IRQ 与 async 的真实实现/stub 由 aDrv CMake 成对选择。

## aOS 配置生成

`platform/aOS/config/freeRTOS_defaults.cmake` 声明完整的 FreeRTOS 通用默认值，
根目录 `config/freeRTOS_config.cmake` 使用普通 `set()` 覆盖当前工程参数。两个文件
按顺序在 aOS 的同一目录作用域中加载，因此后加载的工程配置自然覆盖默认值，
不会把 FreeRTOS 内部配置保存到 `CMakeCache.txt`。aOS
使用最终参数和自身的 `config/FreeRTOSConfig.h.in` 生成：

```text
build/<配置>/aOS_config/FreeRTOSConfig.h
```

模板和默认值随 aOS 复用，工程只保存差异参数，源代码目录中不保存生成头文件。
FreeRTOS port 仍由 MCU profile 决定。

func 层模块由 `config/aclass_config.cmake` 统一选择。`ASHELL_ENABLED=ON` 编译
Letter Shell 和 OS 适配；设为 `OFF` 时，`aShell` target 改为编译
`aShell_stub.c`，公共 API 保持不变，同时 aSystem 不再链接 `aDevUsart` 或创建
Shell 传输资源。业务模块不需要用条件编译包围已有的 `aShellPrint()` 等调用。

## 公共规范与状态

`aclass_initialize()` 统一设置 C11、`compile_commands.json`、Debug/Release
选项、告警规则及 ELF/HEX/BIN/MAP 产物。工具链文件只处理编译器和目标架构。

当前 MCU profile、链接脚本和 GCC 15.3 解析通过。GD32E50X_CL GCC startup、
newlib syscall/sysmem、全部 aDrv 模块和最终 ELF/HEX/BIN 均已完成构建验证。
