# CMake 优化交流记录

本文用于逐项讨论和实施 GD32E505 固件的 CMake 优化。每次只修改一项，完成
设计、代码调整、Debug/Release 验证后再进入下一项。

状态定义：`待讨论`、`讨论中`、`进行中`、`已完成`、`暂缓`。

## 优化清单

| 编号 | 优化项 | 状态 | 验收标准 |
|---|---|---|---|
| 01 | 解除 aCore 对 aDrv/SPL 的反向依赖 | 讨论中 | aCore CMake 不引用 `platform/aDrv`；依赖方向与 Demo 一致；Debug/Release 通过 |
| 02 | GD32 aDrv 构建逻辑下沉至 port | 待讨论 | 通用 aDrv CMake 不列出 GD32 源文件 |
| 03 | 移除 aDrv 通用源码中的厂商依赖 | 待讨论 | `aDrv/src`、`aDrv/include` 不包含厂商头文件 |
| 04 | 使用 INTERFACE target 管理编译规范 | 待讨论 | 第三方源码不再依赖逐文件抵消全局告警 |
| 05 | 收紧 PUBLIC/PRIVATE 依赖 | 待讨论 | 公开依赖与头文件一致，最终链接依赖更清晰 |
| 06 | 完善工具链适配边界 | 待讨论 | GCC 专用 runtime、链接和产物规则由 GCC 适配负责 |
| 07 | 统一目标命名与配置校验 | 待讨论 | device 目标命名一致，配置缺失时尽早报错 |

## 工程选择机制重构

### 已确认职责

```text
根 CMake       声明工程名、MCU、工具链和链接脚本名称
Aclass.cmake   解析名称、检查文件、加载 MCU profile
mcu_*.cmake    保存芯片型号、CPU、时钟、FreeRTOS port 和能力参数
aCore/aDrv     消费选择结果并实现芯片适配
```

根目录现在只需要传入 `MCU gd32e505`、`TOOLCHAIN GCC` 和
`LINKER_SCRIPT GD32E505_flash.ld`。`Aclass.cmake` 自动解析为：

```text
config/mcu_gd32e505.cmake
cmake/toolchains/GCC.cmake
<工程根目录>/GD32E505_flash.ld
```

芯片 profile 中的公共定义已改为 `MCU_PUBLIC_DEFINITIONS`，只保留
`GD32E50X`、`GD32E50X_CL`；`USE_STDPERIPH_DRIVER` 不再作为全平台定义。

### 当前验证边界

- 工程、MCU profile、GCC 工具链和链接脚本解析通过。
- ARM GCC 15.3 编译器识别通过。
- 完整 configure 当前停在 Demo aCore 仍要求 `MCU_FAMILY`、`TARGET_CORE`；
  根据讨论，本轮不补 GCC startup、aCore API 或 GCC runtime，也不使用临时参数
  绕过，留到后续 aCore 适配时统一处理。

## 01：解除 aCore 对 aDrv/SPL 的反向依赖

### 当前问题

`platform/aCore/CMakeLists.txt` 为编译系统时钟源码，直接加入了
`platform/aDrv/GD32E50x_standard_peripheral/Include`。这形成目录级反向依赖：

```text
aCore ──包含──> aDrv/SPL
aDrv  ──链接──> aCore
```

### Demo 为什么没有反向依赖

Demo 中的依赖方向始终为：

```text
aDrv/STM32 HAL ──链接──> aCore/CMSIS Device
```

`system_stm32h7xx.c` 只包含 `stm32h7xx.h`。STM32 的 CMSIS Device 头文件自身
已经定义了 RCC、PWR、FLASH 寄存器，因此 aCore 不需要访问 HAL 目录。
`USE_HAL_DRIVER` 也只由 HAL target 提供，没有作为 aCore 的芯片公共定义。

GD32 官方库的组织方式不同：`system_gd32e50x.c` 虽然只包含
`gd32e50x.h`，但系统时钟代码使用的 RCU、FMC、PMU 宏分别位于标准外设库的
三个头文件中；`gd32e50x.h` 又通过 `gd32e50x_libopt.h` 条件包含这些头文件。
当 `USE_STDPERIPH_DRIVER` 被放入全局 MCU 定义后，aCore 编译系统文件时也必须
找到 SPL 头文件，于是形成了原来的目录级反向依赖。

这说明原问题来自 GD32 与 STM32 官方库的文件组织差异，而不是 Demo 隐藏了
CMake 依赖。

### GD32 当前对标准外设库的实际依赖

需要区分“官方 CMSIS system 的必要依赖”和“aDrv 主动使用的外设驱动”。

1. aCore 中的官方 `system_gd32e50x.c`：
   - 使用 `gd32e50x_rcu.h` 中的 RCU 寄存器和时钟宏；
   - 使用 `gd32e50x_fmc.h` 中的 Flash wait-state 宏；
   - 使用 `gd32e50x_pmu.h` 中的电源控制宏；
   - 调用 `nvic_vector_table_set()`，需要 `gd32e50x_misc.h/.c`。
   - 它不调用 `gd32e50x_rcu.c`、`gd32e50x_fmc.c` 或
     `gd32e50x_pmu.c` 中的驱动函数。
2. aDrv 的 GD32E505 port：
   - GPIO：依赖 `gd32e50x_gpio.h/.c` 和 RCU；
   - USART：依赖 `gd32e50x_usart.h/.c`、GPIO 和 RCU；
   - DMA：依赖 `gd32e50x_dma.h/.c` 和 RCU；
   - SPI：依赖 `gd32e50x_spi.h/.c`、GPIO 和 RCU；
   - QSPI/SQPI：依赖 `gd32e50x_sqpi.h/.c`、GPIO 和 RCU；
   - 公共时钟/复位服务：依赖 `gd32e50x_rcu.h/.c`。
3. 当前没有实际调用 `gd32e50x_dbg.c` 的函数；aDrv 只读取 `DBG_ID` 寄存器宏，
   因此 DBG 实现源文件可以从构建中移除。

GD32 官方模板的 `gd32e50x_libopt.h` 默认包含几乎全部 SPL 头文件，但这是
“统一聚合头文件”的便利设计，不表示 system 或当前工程真正依赖全部外设库。
本工程应继续按实际使用集合裁剪。

### 当前试验方案

1. 在 aCore 中建立 `mcu_support` INTERFACE target，统一提供 CMSIS、Device、
   vendor 头文件和芯片编译定义。
2. 将系统时钟必需的 RCU/FMC/PMU 寄存器头文件归入 aCore Device。
3. aCore 和 GD32 SPL 共同依赖 `mcu_support`；aCore 不再访问 aDrv 路径。
4. 仅在编译 aDrv/SPL 时启用额外外设头文件。

该方案已经消除了反向路径并通过构建，但 `mcu_support` 与 aCore 的职责存在
重叠，还没有确认是否作为最终结构。

### 建议的最终方案（待确认）

进一步贴近 Demo：

1. 不单独保留 `mcu_support`，由 aCore 自身 PUBLIC 提供 CMSIS、Device 头文件
   和纯芯片型号定义。
2. GD32 SPL 像 Demo 的 STM32 HAL 一样 PUBLIC 链接 aCore，依赖方向保持
   `aDrv/SPL -> aCore`。
3. 将 `USE_STDPERIPH_DRIVER` 从 MCU 公共定义移出，只对确实需要 SPL 的 target
   启用；aCore 编译官方 system 文件时作为 PRIVATE 定义使用。
4. RCU、FMC、PMU 是 GD32 官方 system 文件不可缺少的寄存器定义。概念上把它们
   作为 GD32 CMSIS Device 的补充放在 aCore；其余外设头文件仍留在 aDrv/SPL。

这样做与 Demo 的“aCore 提供芯片核心寄存器访问、aDrv 提供外设驱动”方向一致，
也避免新增一个仅为共享 include 路径而存在的抽象 target。需要接受的差异是：
GD32 官方把这三个头文件归档在 SPL 中，而 STM32 把等价寄存器定义直接放在
Device 头文件中。

### 验收记录（当前试验方案）

- [x] aCore CMake 中不存在 `platform/aDrv` 路径
- [x] `mcu_support` 同时服务 aCore 与 GD32 SPL
- [x] Debug 全量构建通过
- [x] Release 全量构建通过

### 当前试验结果

- aCore 定义 `mcu_support`，集中提供 CMSIS、GD32 Device 头文件与 MCU
  编译定义。
- RCU、FMC、PMU 头文件移入 aCore 的 Device 目录；aCore 不再包含
  `platform/aDrv` 下的路径。
- GD32 SPL 通过链接 `mcu_support` 获取芯片公共定义；额外外设头文件只在
  aDrv/SPL 启用。
- Debug 构建：FLASH 23960 B，RAM 28600 B。
- Release 构建：FLASH 20260 B，RAM 28600 B。

## 讨论记录

- 2026-08-25：建立文档，按高到低优先级逐项处理；开始优化 01。
- 2026-08-25：优化 01 的试验方案通过 Debug/Release 全量构建。
- 2026-08-25：经讨论撤销“已完成”状态；补充 Demo 与 GD32 官方库的结构差异，
  等待确认最终依赖表达后再实施并验收。
- 2026-08-25：确认由根 CMake 传入工程、MCU、工具链和链接脚本名称，
  `Aclass.cmake` 负责选择与校验，`config/mcu_*.cmake` 保存芯片事实。
- 2026-08-25：暂不补回 GCC startup、aCore API 和 GCC runtime；这三项作为
  后续 aCore 适配内容处理，不混入当前的 CMake 选择机制重构。
- 2026-08-25：完成根 CMake、`Aclass.cmake` 与 MCU profile 的选择边界重构；
  选择阶段验证通过，完整构建等待 aCore 适配。
