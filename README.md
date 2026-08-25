# GD32E505 固件

本工程采用硬件无关上层与芯片 port 分离的结构。GD32E505 的 CMSIS Device、
startup、system、标准外设库和驱动实现全部集中在 aDrv port；aCore 只保留通用
Arm CMSIS Core。

## 最终分层

```text
app
├── func
├── device
└── platform
    ├── aLib
    ├── aCore
    ├── aOS
    └── aDrv
        ├── include                  硬件无关接口
        └── port/gd32e505            GD32E505 完整芯片适配
```

依赖方向：

```text
app -> func -> device -> aDrv
app/func/device/platform -> aLib
aOS -> aCore
aDrv -> aDrv_gd32e505_vendor -> aCore
```

- `aLib`：统一状态码、编译器属性和纯 C 公共定义。
- `aCore`：与具体厂商和型号无关的 Arm CMSIS Core。
- `aDrv/include`：GPIO、USART、DMA、SPI、QSPI 等稳定接口，不暴露 GD32 类型。
- `aDrv/port/gd32e505`：GD32 CMSIS Device、system、startup、完整 SPL 和接口实现。
- `aOS`：FreeRTOS 的硬件无关封装。
- `device`：USART、RS485、Flash25Q 等设备组合，不保存集中式 board 配置。
- `func`：aMemory、aDataBase、aModbus、aShell；aLog 仅保留说明。
- `app`：提供 `main()`，显式完成所需模块的配置、初始化和测试。

详细约束见 [架构说明](docs/architecture.md)，CMake 职责见
[CMake 设计](docs/cmake_design.md)。

## GD32 官方库

工程保存 `GD32E50x_Firmware_Library_V1.7.0` 的完整标准外设库，并使用其中的
CMSIS Device 文件：

```text
platform/aDrv/port/gd32e505/
├── CMSIS/Device/GD/GD32E50x/
├── GD32E50x_standard_peripheral/
├── CMakeLists.txt
└── aDrv_*_gd32e505.c
```

标准外设库的 28 个头文件和 28 个源文件保持官方原貌。工程维护
`config/gd32e50x_libopt.h.in` 模板，并根据 `config/aDrv_config.cmake` 在构建目录
生成 `gd32e50x_libopt.h`；同一组模块开关同时决定聚合头文件内容和实际编译的
SPL/aDrv 源码。Examples、Docs 和 USB 库不纳入工程。

## 工程配置

根 `CMakeLists.txt` 通过 `aclass_select()` 指定：

- 固件名称；
- MCU profile 名称；
- 链接脚本；
- 工具链名称，省略时使用 GCC。

`config/mcu_gd32e505.cmake` 保存 CPU/FPU、主频、厂商宏、aDrv port、FreeRTOS
port 和芯片能力。`config/aDrv_config.cmake` 选择启用的驱动模块。FreeRTOS 配置位于 `config/`，公共 CMake 规范位于
`cmake/Aclass.cmake`，GCC 工具链位于 `cmake/toolchains/GCC.cmake`。

## 构建

默认工具链目录：

```text
~/Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3
```

可用 `ARM_GCC_ROOT` 环境变量覆盖。构建入口：

```sh
source aclass.env.sh
build
build release
build clean
```

也可以执行 `python3 scripts/build.py`。

当前 Debug/Release CMake 配置和 `aDrv_gd32e505_vendor` 已通过 GCC 15.3 验证。
完整固件还缺少 GD32 GCC startup、aCore 时间接口和 GCC runtime 适配。
