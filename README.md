# GD32E505 固件

工程采用硬件无关上层与芯片驱动层分离的结构。GD32E505 的 CMSIS Device、
system、startup、完整标准外设库和驱动实现全部由 `aDrv` 管理；上层不包含 GD32
厂商头文件。

## 分层

```text
app                 main()、项目配置、显式初始化和测试
├── func            aMemory、aDataBase、aModbus、aShell 等功能
├── device          USART、RS485、Flash25Q 等硬件无关设备语义
└── platform
    ├── aLib        状态码、超时/错误类型、编译器属性和公共定义
    ├── aCore       通用 Arm CMSIS Core 与工具链运行库适配
    ├── aOS         FreeRTOS 封装
    └── aDrv
        ├── include 硬件无关公共接口
        ├── src     GD32E505 驱动实现
        ├── CMSIS   GD32E50x CMSIS Device
        └── GD32E50x_standard_peripheral
```

依赖方向为 `app -> func -> device -> aDrv`。各层可依赖 `aLib`，`aOS` 和
`aDrv_gd32e505_vendor` 可依赖 `aCore`。platform 不提供 `main()` 或隐式初始化
注册表；每个应用模块持有自身引脚、外设实例和运行参数。

## aDrv 配置

`config/aDrv_config.cmake` 是本项目驱动模块选择的唯一入口。其
`ADRV_MODULE_GPIO/USART/DMA/SPI/QSPI` 开关同时控制：

- 构建目录中 `gd32e50x_libopt.h` 的内容；
- 实际参与编译的 GD32 SPL 源文件；
- 实际参与编译的 `src/aDrv_*.c` 实现。

GD32 拥有的外设实例和通道由对应 `.c` 内的私有映射表描述，不通过 CMake 注入
`COUNT` 宏。具体使用哪个实例、哪些引脚以及波特率等参数均由 app 配置。

完整 SPL 的 28 个头文件和 28 个源文件保持官方 V1.7.0 原貌。Examples、Docs 和
USB 库不纳入工程。

## 工程选择与构建

根 `CMakeLists.txt` 使用 `aclass_select()` 选择固件名、MCU profile、链接脚本和
工具链。MCU profile 保存 CPU/FPU、主频、厂商宏和 FreeRTOS port；链接脚本属于
具体项目；FreeRTOS 参数位于 `config/`。

默认 GCC 位于：

```text
~/Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3
```

构建入口：

```sh
source aclass.env.sh
build
build release
build clean
```

也可以执行 `python3 scripts/build.py`。当前工程已使用 GCC 15.3 无警告完成 ELF、
HEX 和 BIN 构建。GD32E50X_CL 的 GCC startup、newlib syscall/sysmem 以及统一超时
机制均已接入；startup 的向量表来自官方 V1.7.0 CL 启动文件。

详细边界见 [架构说明](docs/architecture.md)，构建职责见
[CMake 设计](docs/cmake_design.md)，统一时间与超时规则见
[超时方案](docs/timeout_design.md)。
