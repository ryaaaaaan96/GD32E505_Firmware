# CMake 最终设计

## 职责划分

| 位置 | 职责 |
|---|---|
| 根 `CMakeLists.txt` | 选择固件名、版本、MCU、链接脚本和工具链；加入各层 |
| `cmake/Aclass.cmake` | 解析选择项、校验配置、设置公共编译规范和固件产物 |
| `cmake/toolchains/GCC.cmake` | 查找 Arm GCC，设置 CPU/FPU、链接器和 binutils |
| `config/mcu_gd32e505.cmake` | CPU/FPU、主频、厂商宏、startup variant 和 FreeRTOS port |
| `config/aclass_config.cmake` | 产品功能、aDev 能力和直接 aDrv 请求，按层分区 |
| `cmake/aclass_resolve.cmake` | 集中解析跨层依赖，产生各层使用的有效配置 |
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
产品、device 与 driver 的启用请求分别在 `config/aclass_config.cmake` 的对应分区
表达；依赖统一由 `cmake/aclass_resolve.cmake` 推导。外设实例、引脚和运行参数仍由
app 配置。

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

## 跨层配置解析

项目配置只声明 `*_REQUESTED` 输入，不直接启用其他层的模块。根 CMake 在加入
各层之前调用 `cmake/aclass_resolve.cmake`，生成供构建系统消费的有效配置：

```text
产品请求 + aDev 请求 + aDrv 请求
                |
                v
cmake/aclass_resolve.cmake
                |
                +--> ASHELL_ENABLED / ADATABASE_ENABLED / ...
                +--> ADEV_USART_ENABLED / ADEV_USART_ASYNC_ENABLED / ...
                +--> ADRV_MODULE_USART / ADRV_MODULE_DMA / ...
                |
                v
       func / device / platform CMake
```

依赖必须显式开启，解析器不会覆盖 OFF。缺少依赖时 configure 立即失败，错误消息
列出发起依赖的选项、必须开启的选项，以及配置文件位置。用户输入使用普通变量，
不写入 `CMakeCache.txt`。发送队列属于 aDevUsart，不再有独立 target 或开关。ASYNC 要求 USART DMA，
USART DMA 要求 aDrv USART Async，aDrv USART Async 要求 aDrv DMA。
device USART 按 INTERRUPT、DMA、ASYNC、RS485 四个功能组配置，
TX/RX 方向与 IDLE 模式由应用初始化时选择。

解析器先校验全部输入并归一为 ON/OFF，再按 func、device、driver 检查依赖。
新增能力先在产品配置声明 REQUESTED，在输入区登记，再在对应模块区声明依赖。
Shell 的串口模式和状态灯属于当前 app 的选择，由 `app/devices/CMakeLists.txt` 校验，
公共 resolver 不强制应用必须具有 LED 或串口 Shell。

层级 CMakeLists 只消费解析结果：device/func 根据有效模块决定是否加入 target，
aDrv 根据有效硬件能力选择 GD32 SPL 与驱动源文件，不再自行补齐依赖或改写其他
模块的配置。

## aDrv 配置生成

`ADRV_MODULE_*` 等有效值由中央 resolver 提供。aDrv CMake 对同一组最终值执行：

```text
解析后的 ADRV_MODULE_* 配置
├── configure_file -> generated/aDrv/gd32e50x_libopt.h
├── 选择 GD32E50x_standard_peripheral/Source/*.c
└── 选择 src/aDrv_*.c
```

生成文件只位于构建目录，官方 SPL 目录始终保持原貌。

USART 还提供 `ADRV_USART_INTERRUPT` 与 `ADRV_USART_ASYNC` 两个有效能力值。
`ADRV_USART_ASYNC=ON` 对应的 DMA 依赖由中央 resolver 校验；该能力涵盖 DMA 发送与接收。
关闭时不编译对应源文件，aDrv 的 public compile definitions 同步隐藏不可用 API，
不再编译 IRQ/Async stub。device 也按能力裁剪：
DMA 关闭时不编译 Direct 和 DMA RX 文件；ASYNC 关闭时不编译异步 RX 和 TX 队列，
公共头隐藏相应入口；RS485 关闭时不编译 RS485 源文件。公共状态机按同一宏裁剪
相关分支，纯轮询 device 不再依赖 IRQ/DMA。选择未启用的运行模式仍返回 UNSUPPORTED。

`python3 tests/config/build_matrix.py` 在临时目录构建六种配置，并检查 aDevUsart
静态库中的符号，避免只依靠最终 ELF 的链接垃圾回收掩盖未裁剪的引用。
产品可以通过 `-DACLASS_CONFIG_FILE=/absolute/path/product.cmake` 指定另一个配置文件；
默认仍读取工程 config/aclass_config.cmake。

## aOS 配置生成

`platform/aOS/config/freeRTOS_defaults.cmake` 声明完整的 FreeRTOS 通用默认值，
根目录 `config/freeRTOS_config.cmake` 使用普通 `set()` 覆盖当前工程参数。两个文件
按顺序在 aOS 的同一目录作用域中加载，因此后加载的工程配置自然覆盖默认值，
不会把 FreeRTOS 内部配置保存到 `CMakeCache.txt`。aOS
使用最终参数和自身的 `templates/FreeRTOSConfig.h.in` 生成：

```text
build/<配置>/aOS_config/FreeRTOSConfig.h
```

模板和默认值随 aOS 复用，工程只保存差异参数，源代码目录中不保存生成头文件。
FreeRTOS port 仍由 MCU profile 决定。

func 层模块请求由 `config/aclass_config.cmake` 设置，再由 resolver 输出有效值。
`ASHELL_REQUESTED=ON` 编译 Letter Shell 和 OS 适配；设为 `OFF` 时，`aShell` target 改为编译
`aShell_stub.c`，公共 API 保持不变，同时 aSystem 不再链接 `aDevUsart` 或创建
Shell 传输资源。业务模块不需要用条件编译包围已有的 `aShellPrint()` 等调用。

`ADATABASE_ENABLED` 和 `AMODBUS_ENABLED` 控制可选功能的构建；启用数据库需要显式开启
Flash25Q、QSPI 和 GPIO。所有项目请求均为普通 CMake 变量，切换工程配置时不会
沿用上次 configure 的 cache 值。

## 公共规范与状态

工程名称、MCU 和链接脚本使用普通变量，避免 CACHE FORCE 覆盖用户状态。
构建目录保存 MCU/toolchain 身份用于一致性检查；切换二者必须选择新的构建目录。
ARM_GCC_ROOT 是本机工具路径，仍保留 CACHE。同一 profile 内修改 CPU/FPU 或 GCC
安装路径时也应重新建立构建目录，因为编译器初始化参数不会自动全部刷新。

aOS 的生成配置和 FreeRTOS port include 只用于内部编译；aDrv/aOS 对 aCore 使用
PRIVATE 依赖。aOS 公共 include 目录仍混有上游头文件，后续可进一步物理分离。

`aclass_initialize()` 统一设置 C11、`compile_commands.json`、Debug/Release
选项、告警规则及 ELF/HEX/BIN/MAP 产物。工具链文件只处理编译器和目标架构。

当前 MCU profile、链接脚本和 GCC 15.3 解析通过。GD32E50X_CL GCC startup、
newlib syscall/sysmem、全部 aDrv 模块和最终 ELF/HEX/BIN 均已完成构建验证。

## 应用设备构建

app/devices 按 led、usart 分目录，通过普通 target_sources 加入固件。
USART 应用实现根据 ADEV_USART_ENABLED 编译，console 配置受 ASHELL_ENABLED 控制。
设备映射不依赖链接段、KEEP 或特定工具链适配。

详见 [应用设备映射设计](device_registry.md)。
