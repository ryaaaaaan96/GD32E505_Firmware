# IO 固件（GD32E505）

本工程位于独立的 `GD32E505_Firmware` 目录，参考同级 `Demo` 的分层方式，但把芯片边界进一步收紧：

```text
app ────────> func ───────> device
 │             │              │
 ├───────────> aOS            │
 └──────────────────────────> aDrv ──> aCore
                 各层 ───────> aLib
```

`app`、`func`、`device`、`aLib`、`aOS` 以及 `platform/aDrv/include` 禁止包含厂商头文件。换 MCU 时保留这些模块，只替换 `aCore`、`aDrv` 芯片实现和工具链配置。

## 当前可运行基线

- Cortex-M33 启动、`.data/.bss` 初始化、512 KiB Flash / 128 KiB RAM 链接布局
- 官方 V1.7.0 必需部分已纳入工程：`platform/aCore/CMSIS` 和 `platform/aDrv/GD32E50x_standard_peripheral`
- aCore 补充 CMSIS 5.2.0 `cmsis_gcc.h`（官方包缺失的 GCC 编译器适配头）
- 官方 `SystemInit()` 180 MHz HXTAL 时钟配置
- FreeRTOS V11.3.0 Cortex-M33 Non-TrustZone 端口、heap_4 和 1 ms tick
- 应用层显式 `main()` 和初始化流程；aCore 内提供 GCC/newlib 运行库适配
- aOS 任务创建、调度器、延时和 tick 接口
- 当前使用的 aDrv 模块集合：basic、DMA、GPIO、QSPI、SPI、USART
- aLib 提供全平台统一的 `aStatus_t` 与 `A_STATUS_*` 错误分类
- GPIO、USART、DMA、SPI 和 GD32 SQPI 后端
- 应用模块自行初始化运行灯和 USART0 控制台
- device：Flash25Q、RS485、USART
- func：Memory、基于 FlashDB 的 DB、Modbus、基于 Letter Shell 的交互 Shell；aLog 仅保留 README，尚未实现
- ELF、HEX、BIN、MAP 构建产物

示例应用会在启动后执行 USART、GPIO、Modbus CRC 和 Shell 自检；
没有配置实际引脚或外部器件的 RS485、Flash25Q 和 FlashDB 会明确输出 `SKIP`。

`aIPC` 和 `aVFS` 已从本工程删除。aLog 尚未实现，只保留设计说明，不参与构建。
aDataBase 直接使用 FlashDB 2.2.99 KVDB，通过 FAL、aMemory 和 Flash25Q 访问外部 Flash。
Flash25Q 已接入 SQPI 命令、分页写入、扇区擦除和忙状态等待，但外部 Flash 型号与引脚
仍需结合目标 PCB 做硬件验证。

示例应用在 `app/main.c` 内自行声明并初始化 PA8 运行灯和 USART0 PA9/PA10；
其他应用模块按自身需要持有配置和驱动句柄，不设置集中式 board 层。

## 构建

构建脚本默认使用 `~/Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3`。
移机时可通过 `ARM_GCC_ROOT` 环境变量覆盖，不需要项目配置文件。推荐使用与 Demo 一致的环境入口：

```sh
cd GD32E505_Firmware
source aclass.env.sh
build                 # Debug -> build/Debug/bin
build release         # Release -> build/Release/bin
build clean           # 删除 build 目录
```

也可以直接执行 `python3 scripts/build.py`。

固件产物 `io_gd32e505.elf/.hex/.bin/.map` 统一放在相应配置的 `bin/` 目录中。

## 工程配置边界

切换工程或 MCU 时，修改根目录 `CMakeLists.txt` 开头的选择项：

- `FIRMWARE_NAME`：最终固件名称
- `MCU`：MCU 配置名称，例如 `gd32e505`；AClass 自动加载
  `config/mcu_<名称>.cmake`
- `LINKER_SCRIPT`：具体工程的内存布局

`config/mcu_gd32e505.cmake` 保存 GD32E505 的 CPU/FPU、主频、厂商宏、
aDrv port、FreeRTOS port 和外设能力参数。增加新芯片时新增一个 `mcu_*.cmake`
配置文件，不把具体数值重新散落到各层 CMake 中。

FreeRTOS 的 tick、优先级、heap、任务栈和功能开关集中放在 `config/`：

- `config/freeRTOS_config.cmake`：可参数化数值和 heap 实现
- `config/FreeRTOSConfig.h.in`：FreeRTOS 功能宏

platform 下的 CMake 只消费这些配置，不再保存产品级配置值。
通用 C 语言标准、告警选项、输出目录和固件产物生成规则统一位于
`cmake/Aclass.cmake`。顶层通过 `aclass_select(...)` 一次选择工程名、MCU
配置、链接脚本和工具链；随后保留 CMake 要求的直接 `project()` 调用，并用
`aclass_initialize()` 应用公共规范。

工具链使用名称选择，例如 `TOOLCHAIN GCC` 对应
`cmake/toolchains/GCC.cmake`；省略该参数时默认使用 GCC。

## 移植新 MCU

1. 保持 `platform/aDrv/include` 中的类型、返回值和函数签名不变。
2. 新建 `platform/aDrv/port/<mcu>`，只在这里包含厂商库头文件。
3. 替换 `aCore` 的启动、系统时钟、链接脚本和 CMSIS 实现；aLib/aOS 保持不变，并在 aCore 内选择对应工具链适配。
4. 在 CMake 中选择新 port，更新 `aDrvGetCapabilities()`。
5. 编译所有上层模块，再逐项执行 GPIO/USART 及新增外设的硬件测试。

详细边界与扩展规则见 `docs/architecture.md`。
