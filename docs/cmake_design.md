# CMake 最终设计

## 职责划分

| 位置 | 职责 |
|---|---|
| 根 `CMakeLists.txt` | 选择固件名、版本、MCU、链接脚本和工具链；加入各层 |
| `cmake/Aclass.cmake` | 解析选择项、校验文件、设置公共编译规范和生成固件产物 |
| `cmake/toolchains/GCC.cmake` | 查找 Arm GCC，设置 CPU/FPU、链接器和 binutils |
| `config/mcu_gd32e505.cmake` | 保存芯片事实、厂商宏、aDrv/FreeRTOS port 和能力 |
| `config/aDrv_config.cmake` | 选择项目启用的硬件无关 aDrv 模块 |
| `config/freeRTOS_config.cmake` | 保存 FreeRTOS 可配置参数 |
| `platform/aCore/CMakeLists.txt` | 导出通用 CMSIS Core include |
| `platform/aDrv/CMakeLists.txt` | 加载所选 port，构建硬件无关 aDrv target |
| `platform/aDrv/port/gd32e505/CMakeLists.txt` | 选择 GD32 Device/system、SPL 和 port 源码 |
| `app/CMakeLists.txt` | 创建最终 ELF，链接所需层并生成固件产物 |

## 工程选择

根目录使用统一入口：

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
MCU gd32e505                 -> config/mcu_gd32e505.cmake
TOOLCHAIN GCC                -> cmake/toolchains/GCC.cmake
LINKER_SCRIPT <name>.ld      -> 工程根目录/<name>.ld
```

链接脚本属于具体项目。CPU/FPU、芯片主频、厂商宏、外设数量和 FreeRTOS port 属于
MCU profile。FreeRTOS 功能开关属于 `config/`。各层 CMake 只消费这些配置。

## target 关系

```text
aCore (INTERFACE)
   ^
   |
aDrv_gd32e505_vendor (STATIC)
   ^
   |
aDrv (STATIC) -> aLib
```

`aDrv_gd32e505_vendor` 包含 GD32 CMSIS Device/system，并从仓库内的完整 SPL
中选择实际启用的源码。
它向 aDrv port 提供厂商 include 与 `GD32E50X`、`GD32E50X_CL` 定义，但 aDrv
通过 PRIVATE 依赖阻止这些使用要求传播到上层。

通用 `platform/aDrv/CMakeLists.txt` 不列出 GD32 文件，只根据 `MCU_PORT` 加载：

```text
platform/aDrv/port/<MCU_PORT>/CMakeLists.txt
```

新芯片 port 需要向父级提供：

- `ADRV_PORT_SOURCES`：该芯片的 aDrv 实现源码；
- `ADRV_PORT_VENDOR_TARGET`：封装芯片 SDK 的 target。

## aDrv 配置生成

`platform/aDrv/config/aDrv_defaults.cmake` 声明所有 `ADRV_MODULE_*` 默认值，
`config/aDrv_config.cmake` 使用 `para_set()` 选择本项目启用的模块。GD32 port
读取最终值后执行三项同步操作：

```text
ADRV_MODULE_* 配置
├── configure_file -> generated/aDrv/gd32e505/gd32e50x_libopt.h
├── 选择 GD32E50x_standard_peripheral/Source/*.c
└── 选择 aDrv_*_gd32e505.c
```

生成目录位于官方 SPL include 路径之前。官方 SPL 目录不保存工程生成文件，
因此始终与 V1.7.0 原始目录一致。

## 公共规范

`aclass_initialize()` 统一设置：

- C11；
- `compile_commands.json`；
- Debug/Release 编译选项；
- 告警规则；
- ELF、HEX、BIN、MAP 输出目录和生成规则。

工具链文件只处理编译器与目标架构参数，不保存产品引脚、外设实例或应用配置。

## 当前验证状态

- GCC 路径、MCU profile 和链接脚本解析通过。
- Debug 和 Release CMake 配置通过。
- `aDrv_gd32e505_vendor` 在 Arm GCC 15.3 下编译通过。
- 完整固件仍需要 GD32 GCC startup、aCore 时间接口和 GCC runtime 适配。
