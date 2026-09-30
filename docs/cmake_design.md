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
2. project() 启用编译器，aclass_initialize() 设置 C11、输出目录和选项 target。
3. include AclassLibraries.cmake，调用 aclass_add_libraries(CONFIG_FILE ...)。
4. 库入口加载产品配置并执行 resolver，再加入 platform、device、func。
5. 产品自行加入 app，由 app 创建 ELF。

MCU gd32e505 映射到 config/mcu_gd32e505.cmake，TOOLCHAIN GCC 映射到
cmake/toolchains/GCC.cmake。链接脚本相对工程根目录解析，也接受绝对路径。
完整调用以[根 CMakeLists.txt](../CMakeLists.txt)为准，不复制第二份工程配置。

功能请求使用普通 set()，不是 CACHE。依赖缺失直接报错，不覆盖显式 OFF。
resolver 先校验布尔输入，再按 func、device、driver 的模块顺序检查依赖。
新增选项须同步输入校验、依赖、生成宏/源码选择及配置矩阵测试。

## 能力裁剪

USART 保留 INTERRUPT、DIRECT、ASYNC、RS485 四组 device 能力；
导出的 C 宏统一使用 `ADEV_USART_<能力>_ENABLE`，值为 0 或 1，
例如 `ADEV_USART_DIRECT_ENABLE`；代码通过 `#if` 判断，不用 `#ifdef`。
CMake 配置、依赖校验、构建条件和对应公开 C 宏统一使用 `*_ENABLE`。
resolver 原地规范化布尔值，不维护请求与结果两套开关。

USART 开关按职责区分，不把硬件 DMA 与业务异步 API 合并：

| 公开 C 宏 | 含义 |
|---|---|
| `ADRV_USART_DMA_ENABLE` | driver USART 的 DMA 启停、进度查询等硬件操作 |
| `ADEV_USART_DIRECT_ENABLE` | 同步用户缓冲区直传 API，不规定是否使用 DMA |
| `ADEV_USART_ASYNC_ENABLE` | device 异步请求、取消、超时及统一 ISR 回调 |

底层 USART DMA 通过 `ADRV_USART_DMA_ENABLE` 配置。
通用 DMA 驱动仍通过 `ADRV_MODULE_DMA_ENABLE` 独立选择，它不代表 USART 专用支持。
DIRECT 不依赖 DMA，默认可使用轮询直传。DMA 数据路径由底层 DMA/IRQ 能力提供，
统一在初始化 mode 的 TX/RX 字段中选择；不能用 DIRECT 开关控制 DMA ring。
当前 ASYNC 需要底层 DMA/IRQ，但不依赖同步 DIRECT API。私有的
`ADEV_USART_DMA_BACKEND_ENABLE` 仅从底层能力派生，不是产品配置或公开 API 开关。
底层现有函数名中的 `Async` 仍表示非阻塞硬件启动，本次配置重命名不修改函数 ABI。

TX/RX 模式和 IDLE 在初始化时选择。主要依赖如下：

| 请求 | 必需能力 |
|---|---|
| device USART | driver USART |
| device INTERRUPT | driver USART interrupt |
| device DIRECT | device USART；后端在初始化选择 |
| device ASYNC | driver USART DMA、interrupt，不依赖 device DIRECT |
| device RS485 | driver GPIO、USART interrupt |
| driver USART DMA | driver USART、通用 DMA 驱动 |
| 数据库 FLASH25Q 后端 | device Flash25Q，继而依赖 SPI/GPIO |
| 数据库 CUSTOM 后端 | 调用者注入存储操作，不依赖 Flash25Q |

完整依赖以 resolver 为准。当前 app 的 LED、Shell 中断串口要求由
app/devices/system/CMakeLists.txt 校验，不强加给所有应用。

aDrv 按有效能力选择 SPL、USART IRQ/DMA 源码，生成
generated/aDrv/gd32e50x_libopt.h；不修改官方 SPL，不编译 IRQ/Async stub。
aDevUsart 固定编译公共管理、TX、RX 三个源文件，内部用能力宏裁剪代码及公共声明；
RS485 源文件按开关加入。设备层没有 TX Queue。
编译能力开启不代表每个硬件实例都支持，初始化校验模式和 DMA 路由；
没有 DMA/IRQ 时显式选择 DMA 后端返回 UNSUPPORTED，不退化为轮询。

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

## 外部产品接入

`aclass_select(PRODUCT_DIR ...)` 指定产品根目录，默认当前源码目录。
MCU profile 和链接脚本从产品目录解析，工具链脚本从平台库自身目录解析。
`ACLASS_FREERTOS_CONFIG_FILE` 可显式指定 OS 参数文件；默认产品的
config/freeRTOS_config.cmake。平台库不再根据顶层 CMAKE_SOURCE_DIR 寻找产品配置。

独立产品在 project() 前调用 aclass_select，之后调用 aclass_initialize 和
aclass_add_libraries(CONFIG_FILE <绝对路径>)，最后自行创建应用目标。
库入口不创建 main 或固件目标，构建产物仍使用顶层统一 lib/bin 目录。
当前只实现 Embedded/FreeRTOS，选择其他后端仍明确报错。

启用数据库必须提供 ADATABASE_LAYOUT_FILE，模块复制产品头到构建目录。
本示例布局为 config/aDatabase_flash_layout.h；不同产品可提供自己的布局。

### Flash25Q / SFUD 构建

ADEV_FLASH25Q_ENABLE 当前依赖 ADRV_MODULE_SPI_ENABLE。
STATIC_ENABLE / DYNAMIC_ENABLE 控制两种对象创建方式，至少启用一种。
官方 SFUD 使用独立 OBJECT target aFlash25qSfud 并合入 aFlash25q，
项目 config 优先于官方示例配置，官方 port 不参与编译。
QSPI 后端尚未实现，不是当前 Flash25Q 的构建依赖。
详见 [Flash25Q 设计](spi_flash_design.md)。
