# 可换芯片分层设计

## 稳定层与变化层

| 层 | 是否随 MCU 更换 | 允许依赖 |
|---|---:|---|
| aLib | 否 | C 标准类型；统一状态与纯 C 通用工具 |
| app | 否 | func、device、aDrv、aOS 公共接口；应用模块自行持有资源配置和句柄 |
| func | 否 | device、aOS 等硬件无关公共接口 |
| device | 通常否 | aDrv 公共接口 |
| aOS | 否 | FreeRTOS、aCore；保持 OS 接口一致 |
| aDrv/include | 否，接口按兼容规则演进 | C 标准头文件、aDrv 自有类型 |
| aDrv/port | 是 | aDrv 公共接口、目标 MCU 官方驱动库 |
| aCore | 是 | CMSIS、启动文件、链接脚本、工具链运行库适配 |

关键点不是目录名称，而是公共结构体中不能出现厂商句柄。公开 handle 仅保存整数宽度的 opaque 数据；GD32 寄存器基址如何解释是 port 的私有职责。

## 接口约束

- 跨层同步接口统一返回 `aStatus_t`，成功固定为 `A_STATUS_OK`（0）。返回数据长度的接口使用非负长度，失败时返回负数 `A_STATUS_*`。
- 资源使用逻辑 ID；引脚由 `ADRV_PIN(port, pin)` 编码，不暴露 GPIO 寄存器。具体资源配置由使用它的应用模块持有。
- 初始化采用“调用者提供 handle”的静态模型，不在驱动内动态分配内存。
- 时间统一使用无符号毫秒，并用差值判断处理 32 位回绕。
- 不支持的引脚复用或芯片能力返回 `A_STATUS_UNSUPPORTED`，上层不得用芯片宏绕过。
- 新增字段只允许追加；已有枚举值和函数语义不得静默改变。

## 外设扩展模板

新增 SPI、ADC、DMA、Timer 或 Watchdog 时，每个模块保持三部分：

```text
platform/aDrv/include/aDrv_xxx.h             稳定公共契约
platform/aDrv/port/gd32e505/aDrv_xxx_*.c    GD32E505 实现
app/                                        板级资源配置和硬件自检
```

Device 层负责把多个基础驱动组合成板上设备语义，例如 RS485 = USART + DE GPIO，W25Q = SPI + CS GPIO。aDrv 不应知道 Modbus、Flash 型号或产品点表。

`aDev_usart` 复用 Demo 的设备层语义，允许依赖硬件无关的 `aLib.h` 和 aOS
时间接口；GD32/STM32 的寄存器收发差异必须留在各自的 aDrv port 中。

当前持久化链路固定为 `FlashDB KVDB → FAL → aMemory → aDev_Flash25q → aDrv QSPI`；
工程不提供 aVFS。aLog 尚未实现，仅保留 README，不应被应用依赖。

## 与 Demo 的对应关系

固件 `main()` 位于应用层，应用显式完成驱动、OS、外设和任务初始化；platform 不保存应用入口或自动初始化注册表。aKernel 已删除，通用常量和编译器属性归 aLib，GCC/newlib 系统调用归 aCore 的 `runtime/gcc`。GD32 官方库不放入通用 third-party：CMSIS 归 aCore，标准外设库归 aDrv；Examples、Docs、USB 和 Utilities 未复制。aDrv 只编译实际启用的外设源文件。

与 Demo 相比，公共 aDrv 头不暴露 STM32/GD32 厂商类型，这是保证换芯片时上层接口无需修改的必要收敛。

统一错误码位于 `platform/aLib/include/aStatus.h`，不放入随 MCU 更换的 aCore。
错误分类与使用规则见 `platform/aLib/README.md`。

## 构建脚本边界

- 默认工具链为 `~/Tools/toolchain/mcu_arm_toolchain/arm-none-eabi-15.3`，可用环境变量 `ARM_GCC_ROOT` 覆盖，不使用项目配置文件。
- `scripts/build.py` 只负责配置、编译和清理，不修改源代码或下载依赖。
- Debug 与 Release 分别输出到 `build/Debug` 和 `build/Release`，避免复用错误的 CMake 缓存。
