# GD32E505 固件

工程采用硬件无关上层与芯片驱动层分离的结构。GD32E505 的 CMSIS Device、
system、startup、完整标准外设库和驱动实现全部由 `aDrv` 管理；上层不包含 GD32
厂商头文件。

## 当前调试板

当前 app 面向 GD32E505VET7 最小调试板：

- HXTAL_IN 接入 20 MHz 外部有源时钟，使用 bypass 模式，系统时钟为 180 MHz；
- USART0 使用 PA9(TX)/PA10(RX)，115200-8-N-1，作为 Shell 控制台；
- PA8 通过 `aDevLed` 设备驱动，每 500 ms 翻转一次；
- LED 暂按低电平点亮配置，实物极性不同时修改 `app/devices/system/app_system_device.c` 中的实例配置。

app 包含 Shell、SPI Flash、KV/TSDB 和日志测试；日志当前接 Shell 输出队列，
测试命令见 [日志演示](app/log/README.md)。QSPI、Modbus 和 RS485 默认关闭；
RS485 属于 aDevUsart 可选功能，详见 [USART / RS485 统一设计](docs/usart_design.md)。

## 分层

```text
app                 main()、项目配置、显式初始化和测试
├── func            aDataBase、aLog、aModbus、aShell 等功能
├── device          USART、RS485、Flash25Q 等硬件无关设备语义
└── platform
    ├── aLib        布尔/状态/超时/错误类型、编译器属性和公共定义
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

产品功能、device 能力和直接 driver 请求统一写在
`config/aclass_config.cmake` 的分区中。`cmake/aclass_resolve.cmake` 集中推导
跨层依赖，输出的有效配置控制：

- 构建目录中 `gd32e50x_libopt.h` 的内容；
- 实际参与编译的 GD32 SPL 源文件；
- 实际参与编译的 `src/aDrv_*.c` 实现。

GD32 拥有的外设实例和通道由对应 `.c` 内的私有映射表描述，不通过 CMake 注入
`COUNT` 宏。具体使用哪个实例、哪些引脚以及波特率等参数均由 app 配置。

配置依赖必须显式开启：USART Async 依赖 DMA，LED 依赖 GPIO，
Flash25Q 已封装 SFUD，当前依赖 SPI 并接入板上 SPI1；QSPI 后端尚未实现。
详见 [Flash25Q 链路设计](docs/spi_flash_design.md)。缺少依赖时 configure 报错并指出需要开启的选项。
`*_ENABLE` 是配置输入，普通变量且不进入 CMake cache；层级
CMakeLists 只消费 resolver 生成的有效值，不在各自目录改写配置。当前 Shell 采用
缓冲中断发送、中断接收和 IDLE 通知。

完整 SPL 的 28 个头文件和 28 个源文件保持官方 V1.7.0 原貌。Examples、Docs 和
USB 库不纳入工程。

## 工程选择与构建

根 `CMakeLists.txt` 使用 `aclass_select()` 选择固件名、MCU profile、链接脚本和
工具链。MCU profile 保存 CPU/FPU、主频、厂商宏和调试器件名；链接脚本属于
具体项目；FreeRTOS port 和参数位于 `config/freeRTOS_config.cmake`。

默认 GCC 位于：

```text
~/Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3
```

构建入口：

```sh
python3 scripts/build.py
python3 scripts/build.py release
python3 scripts/build.py clean
```

脚本直接执行，无需 source，适用于 bash、zsh 和 fish。当前工程已使用 GCC 15.3 无警告完成 ELF、
HEX 和 BIN 构建。GD32E50X_CL 的 GCC startup、newlib syscall/sysmem 以及统一超时
机制均已接入；startup 的向量表来自官方 V1.7.0 CL 启动文件。

Shell 是可裁剪的 aClass 功能模块，由 `config/aclass_config.cmake` 统一选择。
量产配置中将模块关闭：

```cmake
set(ASHELL_ENABLE OFF)
```

重新配置并构建后，`aShell` target 和公共 API 继续存在，但实现切换为不创建任务、
不申请内存的 stub；aSystem 同时不再初始化 Shell USART/DMA 或声明收发缓冲区。

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
[超时方案](docs/interface_contract.md)。

### 设备初始化与跨文件访问

设备配置位于 app/devices，业务模块在 aDrv/aOS 就绪后按实例调用 Init(&handle)。
业务通过 appSystemConsoleInit 获取控制台流（read/write/flush），通过
appSystemStatusLedInit 获取 LED 句柄；底层静态设备由 app/devices 持有。
详见 [应用设备映射与分层](docs/architecture.md)。
## 构建与任务边界

业务任务由 app/task 创建；func/aShell 仅提供 Init/Process/DeInit，不创建线程。
aOS worker 是平台服务，配置在 config/aclass_config.cmake；回调禁止阻塞。
system 设备入口为 appSystemStatusLedInit / appSystemConsoleInit。

CMake 自动输出 build/Debug/firmware-Debug.json（Release 对应另一配置），
scripts/debug.py 从中读取 ELF 和调试芯片名。改固件名无需再修改脚本；
--elf 与 --device 可显式覆盖。更换旧构建目录后需重新配置生成元数据。
PLATFORM Embedded / OS FreeRTOS 是当前唯一实现的后端组合，不表示已经支持 Linux。

## 设计文档

docs 只维护以下四份现行说明，不保留旧方案和迁移过程：

| 文档 | 内容 |
|---|---|
| [架构](docs/architecture.md) | 分层、依赖、应用设备初始化与句柄归属 |
| [接口规范](docs/interface_contract.md) | 类型、错误、超时、缓冲区和任务上下文契约 |
| [CMake](docs/cmake_design.md) | 配置来源、依赖解析、能力裁剪和构建产物 |
| [USART](docs/usart_design.md) | Read/Direct/Async、生命周期、RS485 和当前限制 |

模块 README 用于模块本地使用说明；根 DESIGN_REVIEW.md 保留评审讨论及用户回复，
属于历史记录，不是当前设计依据。修改接口时须同步公共头注释及相应现行文档。

任务栈以字节配置（APP_*_STACK_BYTES / AOS_WORKER_STACK_BYTES），任务入口允许
自然返回。Shell 每轮最多读取 64 字节，成功后继续处理，仅空读或错误时退避。
OS 分配失败记录诊断后返回，应用决定 fatal 策略。当前 FreeRTOS 时基固定为
32 位 tick / 1000 Hz。统一构建入口为 cmake/Aclass.cmake，产品 app 单独创建。

VS Code 的 clangd 通过根目录 `.clangd` 读取 `build/Debug/compile_commands.json`，
头文件路径、功能宏和 ARM 参数均跟随真实构建。首次打开或修改构建配置后运行
`python3 scripts/build.py` 更新编译数据库；`.vscode/settings.json` 允许 clangd 查询
ARM GCC 的系统头文件路径。修改编辑器配置后执行 `clangd: Restart language server`。
若使用其他构建目录，同步修改 `.clangd` 中的 `CompilationDatabase`。
