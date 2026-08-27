# GD32E505 固件

工程采用硬件无关上层与芯片驱动层分离的结构。GD32E505 的 CMSIS Device、
system、startup、完整标准外设库和驱动实现全部由 `aDrv` 管理；上层不包含 GD32
厂商头文件。

## 当前调试板

当前 app 面向 GD32E505VET7 最小调试板：

- HXTAL_IN 接入 20 MHz 外部有源时钟，使用 bypass 模式，系统时钟为 180 MHz；
- USART0 使用 PA9(TX)/PA10(RX)，115200-8-N-1，作为 Shell 控制台；
- PA8 通过 `aDevLed` 设备驱动，每 500 ms 翻转一次；
- LED 暂按低电平点亮配置，实物极性不同时只需修改 `app/system/system_config.h`。

app 不再链接 Flash、RS485、数据库和 Modbus。当前工程也只启用 GPIO 与 USART
驱动，未使用模块仍保留源码，可由后续项目按需打开。

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

USART 的 `ADRV_USART_INTERRUPT` 与 `ADRV_USART_ASYNC` 是独立能力开关；只有
Async-DMA 能力会自动引入 DMA，基础轮询和中断模式不依赖 DMA。

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

## WSL 宿主机与远程调试

先在 Windows 宿主机或其他连接调试器的计算机上启动 JLinkGDBServer/OpenOCD。
J-Link 目标器件选择 `GD32E505VET6`、接口选择 SWD；这是 SEGGER 当前设备表中
与 GD32E505VET7 对应的同容量、同封装调试配置。直接运行脚本后，可以输入 `1`
选择 WSL 所在的 Windows 宿主机，或者输入 `2` 后填写其他远程地址和端口：

```sh
python3 scripts/debug.py
```

也可以使用参数跳过交互：

```sh
python3 scripts/debug.py --mode wsl-host
python3 scripts/debug.py --mode remote --host 192.168.1.100 --port 2331
```

脚本默认按 J-Link 生成命令，并主动选择 `GD32E505VET6`。使用 OpenOCD 时指定：

```sh
python3 scripts/debug.py --mode wsl-host --server openocd
```

默认下载 Debug 固件并运行到 `main`。只附加、不重新下载时使用：

```sh
python3 scripts/debug.py --mode remote --host 192.168.1.100 --attach
```

脚本在 WSL 镜像网络模式下使用 `127.0.0.1` 访问 Windows；在传统 NAT 模式下
自动使用 WSL 默认网关。可先用 `--mode wsl-host --dry-run` 检查最终地址、ELF、
GDB 路径及生成的命令。远程主机的防火墙应仅向可信网络开放 GDB Server 端口，
因为 GDB 协议本身不提供认证和加密。

详细边界见 [架构说明](docs/architecture.md)，构建职责见
[CMake 设计](docs/cmake_design.md)，统一时间与超时规则见
[超时方案](docs/timeout_design.md)。
