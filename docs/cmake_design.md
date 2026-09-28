# CMake 构建与配置

本文描述当前实现；后端为 `PLATFORM Embedded / OS FreeRTOS`，Linux/裸机尚未实现。
构建、调试命令见[根 README](../README.md)。

## 配置归属

| 位置 | 职责 |
|---|---|
| 根 CMakeLists.txt | 固件名、版本、平台、OS、MCU、工具链、链接脚本和各层组织 |
| config/mcu_gd32e505.cmake | CPU/FPU、主频、厂商宏、startup variant、调试器件名 |
| config/aclass_config.cmake | 产品功能、device 能力、driver 请求、worker 参数、数据库后端 |
| config/freeRTOS_config.cmake | FreeRTOS port 与项目参数覆盖 |
| cmake/Aclass.cmake | 工程选择、公共构建选项、产物与调试元数据 |
| cmake/aclass_resolve.cmake | 校验请求及跨层依赖，输出有效配置 |
| cmake/toolchains/GCC.cmake | 编译器、CPU/FPU 参数和 binutils |
| 各层 CMakeLists.txt | 消费有效配置，选择源码、生成头文件和声明 target 依赖 |

工程 config/ 保存 CMake 输入；配置头由模块模板生成到构建目录。
引脚、实例、波特率、运行模式和缓冲区仍由 app 的 C 代码配置，不进入 resolver。

## 配置顺序

1. include Aclass.cmake，调用 `aclass_select()`，在 project() 前选择工具链和 MCU。
2. 加载 ACLASS_CONFIG_FILE，默认 config/aclass_config.cmake。
3. include aclass_resolve.cmake，检查依赖。
4. project() 启用编译器，aclass_initialize() 设置 C11、输出目录和选项 target。
5. 加入 platform、device、func、app，由 app 创建 ELF。

MCU gd32e505 映射到 config/mcu_gd32e505.cmake，TOOLCHAIN GCC 映射到
cmake/toolchains/GCC.cmake。链接脚本相对工程根目录解析，也接受绝对路径。
完整调用以[根 CMakeLists.txt](../CMakeLists.txt)为准，不复制第二份工程配置。

功能请求使用普通 set()，不是 CACHE。依赖缺失直接报错，不覆盖显式 OFF。
resolver 先校验布尔输入，再按 func、device、driver 的模块顺序检查依赖。
新增选项须同步输入校验、依赖、生成宏/源码选择及配置矩阵测试。

## 能力裁剪

USART 保留 INTERRUPT、DMA、ASYNC、RS485 四组 device 能力；
导出的 C 宏统一使用 `ADEV_USART_<能力>_ENABLE`，值为 0 或 1，
例如 `ADEV_USART_DMA_ENABLE`；代码通过 `#if` 判断，不用 `#ifdef`。
CMake 输入仍为 `*_REQUESTED`，解析结果仍为 `*_ENABLED`，不与 C 宏混用。
TX/RX 模式和 IDLE 在初始化时选择。主要依赖如下：

| 请求 | 必需能力 |
|---|---|
| device USART | driver USART |
| device INTERRUPT | driver USART interrupt |
| device DMA | driver USART async、interrupt |
| device ASYNC | device DMA |
| device RS485 | driver GPIO、USART interrupt |
| driver USART async | driver USART、DMA |
| 数据库 FLASH25Q 后端 | device Flash25Q，继而依赖 QSPI/GPIO |
| 数据库 CUSTOM 后端 | 调用者注入存储操作，不依赖 Flash25Q |

完整依赖以 resolver 为准。当前 app 的 LED、Shell 中断串口要求由
app/devices/system/CMakeLists.txt 校验，不强加给所有应用。

aDrv 按有效能力选择 SPL、USART IRQ/DMA 源码，生成
generated/aDrv/gd32e50x_libopt.h；不修改官方 SPL，不编译 IRQ/Async stub。
aDevUsart 固定编译公共管理、TX、RX 三个源文件，内部用能力宏裁剪代码及公共声明；
RS485 源文件按开关加入。设备层没有 TX Queue。
编译能力开启不代表每个硬件实例都支持，运行时仍校验模式和 DMA 路由。

Shell 是例外：关闭后保留 aShell target 和空实现，允许已有打印调用继续编译；
app 不创建 Shell 任务和 console 资源。启用时 Shell 也不创建任务，任务归 app。

## target 与配置头边界

- aCore 使用 OBJECT target 提供 CMSIS Core 和 GCC runtime；startup 属于 aDrv。
- aDrv 私有依赖 vendor target，厂商头和宏不向上层传播。
- aOS 只公开 public/；FreeRTOS 头、port 和生成配置均为私有。
- FreeRTOS kernel 为独立 OBJECT target，OS 适配位于 backend/freertos/。
- aDataBase 核心使用通用存储接口，可选 aDataBaseFlash25q target 提供 Flash25Q 适配。
- aclass_build_options 提供优化/调试选项；aclass_project_options 增加项目告警策略。
  上游内核和厂商源码不直接继承项目的严格告警策略。

aOS 先加载 config/freeRTOS_defaults.cmake，再加载工程覆盖值；
使用 templates/ 模板生成 aOS_config/FreeRTOSConfig.h 和 aOS_config.h。
默认值和模板随模块复用，工程只保存差异。

## 产物与验证

build/Debug 或 build/Release 包含 ELF/HEX/BIN/MAP、编译数据库及
firmware-<配置>.json。debug.py 从 JSON 读取 ELF 和调试器件名，支持显式覆盖。

构建缓存保存 MCU/toolchain 身份，切换时要求新构建目录。ARM_GCC_ROOT 是本机
工具路径，仍使用 CACHE；CPU/FPU 或编译器安装路径变化也应重新建立构建目录。

`cmake -P tests/config/test_resolver.cmake` 验证依赖成功/失败路径；
`python3 tests/config/build_matrix.py` 验证六种 USART 组合、两个数据库后端的
编译、链接和符号裁剪。构建验证不代表硬件验证。
