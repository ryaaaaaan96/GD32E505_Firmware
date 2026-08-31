# 固件最终架构

## 分层职责

| 层 | 职责 |
|---|---|
| aLib | 统一布尔、状态、超时/错误类型、编译器属性和纯 C 工具 |
| aCore | 通用 Arm CMSIS Core 和按工具链选择的 C 运行库适配 |
| aDrv/include | 稳定的硬件无关驱动接口与类型 |
| aDrv/src | GD32E505 的分模块驱动实现及少量私有共享声明 |
| aOS | FreeRTOS 的硬件无关封装 |
| device | 组合 USART、RS485、Flash25Q 等设备语义 |
| func | 组合 device/aOS，提供数据库、Modbus、Shell 等功能 |
| app | `main()`、资源配置、显式初始化、任务创建和测试 |

```text
app -> func -> device -> aDrv -> aDrv_gd32e505_vendor -> aCore
                 └----> aOS --------------------------> aCore
all stable layers -------------------------------------> aLib
```

禁止 aCore 反向依赖 aDrv。app、func、device、aOS、aLib 和 `aDrv/include` 不得
包含 GD32 厂商头文件。

aCore 的 GCC runtime 适配 newlib syscall 和 heap，不包含芯片向量表。其默认
read/write 钩子不依赖任何设备；app 可以提供强定义连接具体控制台。芯片相关的
startup、CMSIS Device 和 `SystemInit()` 仍由 aDrv 管理。

## aDrv 边界

```text
platform/aDrv/
├── include/                               硬件无关公共接口
├── src/
│   ├── aDrv.c
│   ├── aDrv_basic.c
│   ├── aDrv_gpio.c
│   ├── aDrv_usart.c
│   ├── aDrv_usart_irq.c / irq_stub.c
│   ├── aDrv_usart_async.c / async_stub.c
│   ├── aDrv_usart_internal.h
│   ├── aDrv_dma.c
│   ├── aDrv_spi.c
│   ├── aDrv_qspi.c
│   └── aDrv_internal.h                   仅模块间必要的私有声明
├── CMSIS/Device/GD/GD32E50x/             Device、system、startup
├── GD32E50x_standard_peripheral/         完整官方 SPL
├── config/                               驱动默认值与 libopt 模板
└── CMakeLists.txt
```

普通外设保持一个独立 `.c`。USART 按基础轮询、可选中断、可选异步 DMA 收发拆分，
避免基础 USART 对 DMA 形成硬依赖；私有共享状态只通过
`aDrv_usart_internal.h` 连接。公共头文件不出现 GD32 寄存器类型或 DMA 通道映射。

`config/aDrv_config.cmake` 的 `ADRV_MODULE_*` 开关同步选择生成的
`gd32e50x_libopt.h`、SPL 源文件和 aDrv 实现源文件。system 固有依赖由 aDrv
CMake 自动加入，USART/SPI/QSPI 对 GPIO 的依赖在配置阶段校验。

`ADRV_USART_INTERRUPT` 和 `ADRV_USART_ASYNC` 分别控制 USART 可选能力。异步能力
启用时自动选择 DMA；关闭异步能力时编译轻量 stub，因此公共接口稳定，但最终
固件不包含 DMA 驱动和 SPL DMA 源码。不支持的实例或未编译能力返回
`A_STATUS_UNSUPPORTED`。

芯片实际拥有的 USART、SPI、DMA 通道和 GPIO 端口由各实现文件的私有映射表
描述。app 只选择公共逻辑实例并设置引脚、速率和工作模式。

GCC startup 的中断向量顺序来自官方 V1.7.0 的 CL 启动文件，GNU 版本负责初始化
`.data/.bss`、调用 `SystemInit()` 和 C 运行库构造函数，最终进入 app 的 `main()`。

## 公共接口规则

- 配置、控制和 aDrv 非阻塞接口使用 `aStatus_t`，成功为 `A_STATUS_OK`。
- 流式 device read/write 使用 `aSSize_t`：非负数表示实际长度，`-1` 表示失败，
  具体原因通过 `aOSGetErrno()` 查询。
- 软件等待统一使用 `aTimeout_t`；aDrv 不获取 uptime、不执行延时，也不实现软件
  超时轮询。
- 公共结构体不得出现 GD32 类型或厂商 handle。
- 引脚使用 `ADRV_PIN(port, pin)` 编码，寄存器映射留在 aDrv 实现中。
- 初始化使用调用者提供 handle 的静态模型，驱动内部不动态分配内存。
- 资源初始化状态由真正拥有该资源生命周期的层保存，并且只能有一个权威状态。
  纯转发或薄包装层不得重复保存下层的 `initialized`；只有管理多个资源、存在独立
  初始化/反初始化过程或部分初始化回滚的组合对象，才保存本层生命周期状态。
- 当对象具有未初始化、就绪、忙、错误等三种以上互斥状态时，使用状态枚举，禁止
  用多个布尔字段拼接生命周期状态。
- 不支持的能力返回 `A_STATUS_UNSUPPORTED`。
- 当前工程不保留旧接口兼容别名或转发包装。公共接口发生调整时，同步修改全部
  调用方并删除旧声明、旧实现和旧字段，避免长期维护两套接口。

## device、func 与 app

device 提供硬件无关的设备组合，例如 `RS485 = USART + DE GPIO`、
`Flash25Q = SPI/QSPI + CS GPIO`。device 依赖 aOS 的单调时基实现软件超时，但
aDrv 不依赖 aOS。func 在其上构成 aMemory、aDataBase、aModbus 和 aShell 等功能。

工程不设置集中式 board 目录。引脚、外部器件型号、总线参数和设备句柄由使用它
们的应用模块持有。platform 不提供 `main()` 或自动初始化注册表。

## 更换芯片或项目

更换芯片时，保留 `aDrv/include` 的公共契约，替换 aDrv 的芯片实现、CMSIS Device
和厂商库，并新增对应 `config/mcu_<mcu>.cmake`。若 CPU 架构不同，再替换 aCore
内容。更换项目时只需在根 `CMakeLists.txt` 选择固件、MCU profile 和链接脚本，
并在 `config/` 与 app 中设置模块及产品资源。
