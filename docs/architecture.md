# 固件最终架构

## 分层职责

| 层 | MCU 相关性 | 职责与允许依赖 |
|---|---:|---|
| aLib | 无关 | 统一状态码、编译器属性、公共常量和纯 C 工具 |
| aCore | CPU 架构相关 | Arm CMSIS Core；不包含具体 MCU Device 或厂商外设库 |
| aDrv/include | 无关 | 稳定驱动接口和硬件无关类型 |
| aDrv/port | 相关 | CMSIS Device、startup、system、厂商库和 aDrv 接口实现 |
| aOS | 通常无关 | FreeRTOS 封装；FreeRTOS CPU port 由 MCU profile 选择 |
| device | 无关 | 使用 aDrv 组合 USART、RS485、Flash25Q 等设备语义 |
| func | 无关 | 使用 device/aOS 构成数据库、Modbus、Shell 等功能 |
| app | 项目相关 | `main()`、资源配置、显式初始化、任务创建和测试 |

最终依赖方向：

```text
app -> func -> device -> aDrv
                 aDrv -> MCU vendor target -> aCore
                 aOS  -> aCore
all stable layers     -> aLib
```

禁止 aCore 依赖 aDrv。禁止 app、func、device、aOS、aLib、`aDrv/include` 和
`aDrv/src` 包含 GD32 厂商头文件。

## aCore 与 aDrv 的边界

```text
platform/aCore/
└── Core/Include/                          通用 Arm CMSIS Core

platform/aDrv/
├── include/                               硬件无关公共接口
├── src/                                   硬件无关公共实现
└── port/gd32e505/
    ├── CMSIS/Device/GD/GD32E50x/          Device、system、startup
    ├── GD32E50x_standard_peripheral/      完整官方 SPL
    ├── CMakeLists.txt                     芯片构建边界
    └── aDrv_*_gd32e505.c                  GD32E505 接口实现
```

`GD32E50x_standard_peripheral` 完整保存官方 28 个头文件和 28 个源文件。
`aDrv_gd32e505_vendor` 按需编译 `system_gd32e50x.c`、MISC、RCU、GPIO、USART、
DMA、SPI 和 SQPI；其他厂商源码保留在仓库中，但不加入当前固件目标。

`config/aDrv_config.cmake` 是驱动模块选择的唯一项目配置入口。同一组
`ADRV_MODULE_*` 开关同时控制：

- 生成到构建目录的 `gd32e50x_libopt.h` 所包含的外设头文件；
- 加入 `aDrv_gd32e505_vendor` 的 SPL 源文件；
- 加入 aDrv target 的 GD32E505 port 实现源码。

system 固有依赖由 port 自动加入；模块间依赖由 port CMake 校验。

厂商 include 和编译定义只服务于 vendor target 与 GD32 port，不向 device、func
或 app 传播。

## 公共接口规则

- 同步接口统一使用 `aStatus_t`；成功为 `A_STATUS_OK`。
- 返回数据长度的接口以非负数表示长度，以负数 `A_STATUS_*` 表示失败。
- 公共结构体不得出现 GD32 寄存器类型或厂商 handle。
- 引脚使用 `ADRV_PIN(port, pin)` 逻辑编码，寄存器映射只在 port 中完成。
- 初始化使用调用者提供 handle 的静态模型，驱动内部不动态分配内存。
- 毫秒时间使用无符号类型，并以差值方式处理回绕。
- 不支持的芯片能力返回 `A_STATUS_UNSUPPORTED`，上层不使用芯片宏绕过接口。
- 已发布结构体的新字段只能追加，已有枚举值和接口语义保持兼容。

## device、func 与 app

device 负责组合基础驱动：

```text
RS485    = USART + DE GPIO
Flash25Q = SPI/QSPI + CS GPIO
```

func 负责硬件无关功能。当前持久化链路为：

```text
FlashDB KVDB -> FAL/aMemory -> aDev_Flash25q -> aDrv QSPI
```

工程不设置集中式 board 目录。引脚、外部器件型号、总线参数和设备句柄由使用它们
的应用模块持有。platform 不提供 `main()` 或自动初始化注册表。

## 更换 MCU

1. 新建 `platform/aDrv/port/<mcu>`。
2. 放入该 MCU 的 CMSIS Device、startup、system 和实际需要的厂商库文件。
3. 实现 `platform/aDrv/include` 定义的接口，不修改上层公共契约。
4. 在 port 中提供由 aDrv 模块开关驱动的 vendor 源码映射和配置头模板。
5. 新建 `config/mcu_<mcu>.cmake`，设置 CPU/FPU、厂商宏、aDrv port、OS port 和能力。
6. 在 `config/aDrv_config.cmake` 选择项目需要的驱动模块。
7. 在根 `CMakeLists.txt` 选择 MCU profile 和项目链接脚本。
8. 只有 CPU 架构或 CMSIS Core 要求变化时才修改 aCore。
