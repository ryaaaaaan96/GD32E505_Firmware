# 固件最终架构

## 分层职责

| 层 | 职责 |
|---|---|
| aLib | 统一布尔、状态、超时/错误类型、编译器属性和纯 C 工具 |
| aCore | 通用 Arm CMSIS Core 和按工具链选择的 C 运行库适配 |
| aDrv/include | 稳定的硬件无关驱动接口与类型 |
| aDrv/src | GD32E505 的分模块驱动实现及少量私有共享声明 |
| aOS | FreeRTOS 后端的硬件无关封装、等待/锁/时基/errno 与故障记录 |
| device | 组合 USART、RS485、Flash25Q 等设备语义 |
| func | 组合 device/aOS，提供数据库（含其存储适配）、Modbus、Shell 等功能 |
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

`aShell` 是进程内唯一实例，提供 Init/Process/DeInit，由 app 创建任务并调用；
读写通过 aLib 的 aStream_t 注入，USART 适配由 app/devices 的私有回调提供，
appSystemConsoleInit 初始化 USART，通过 Stream 绑定并初始化 aShell 单例；flush 为 NULL。
aShell 内部适配 nr_micro_shell 2.0.0，命令通过 ASHELL_CMD_EXPORT 分散注册、由 GCC 链接器收集，不依赖具体 USART 类型。
关闭时保留无操作 stub。func 不创建业务任务；aOS worker 属于平台服务。`aOS` 后端和上游 FreeRTOS source set 的归属见
[aOS 目录说明](../platform/aOS/README.md)。

## aDrv 边界

```text
platform/aDrv/
├── include/                               硬件无关公共接口
├── src/
│   ├── aDrv.c
│   ├── aDrv_basic.c
│   ├── aDrv_gpio.c
│   ├── usart/
│   │   ├── aDrv_usart.c
│   │   ├── aDrv_usart_irq.c (按配置选择)
│   │   ├── aDrv_usart_async.c (按配置选择)
│   │   └── aDrv_usart_internal.h
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
`usart/aDrv_usart_internal.h` 连接。公共头文件不出现 GD32 寄存器类型或 DMA 通道映射。

`cmake/aclass_resolve.cmake` 集中计算产品、aDev 与 aDrv 之间的依赖。各层
CMakeLists 只消费有效配置：aDev 选择是否加入设备 target，aDrv 选择生成的
`gd32e50x_libopt.h`、SPL 源文件和驱动实现源文件，不在本层推导或改写依赖。

`ADRV_USART_INTERRUPT_ENABLE` 和 `ADRV_USART_DMA_ENABLE` 是 resolver 输出的有效能力。USART DMA 能力
依赖通用 DMA 驱动，不等于 device 的异步请求 API；关闭某项能力时不编译对应实现，aDrv target 通过 public compile
definition 隐藏该能力的头文件 API。aDev USART 的 INTERRUPT、DIRECT、ASYNC、RS485 接口按配置裁剪；
DIRECT 表示用户缓冲区直传，后端在初始化选择；普通 DMA ring 不依赖 DIRECT 开关。
只启用轮询时不需要 IRQ/DMA。不支持的实例仍返回
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
- aDrv 初始化使用调用者提供 handle 的静态模型，驱动内部不动态分配内存；device
  可按模块契约提供静态不透明存储与动态创建两种方式。
- 资源初始化状态由真正拥有该资源生命周期的层保存，并且只能有一个权威状态。
  纯转发或薄包装层不得重复保存下层的 `initialized`；只有管理多个资源、存在独立
  初始化/反初始化过程或部分初始化回滚的组合对象，才保存本层生命周期状态。
- 当对象具有未初始化、就绪、忙、错误等三种以上互斥状态时，使用状态枚举，禁止
  用多个布尔字段拼接生命周期状态。
- 不支持的能力返回 `A_STATUS_UNSUPPORTED`。
- 当前工程不保留旧接口兼容别名或转发包装。公共接口发生调整时，同步修改全部
  调用方并删除旧声明、旧实现和旧字段，避免长期维护两套接口。

## device、func 与 app

device 提供硬件无关的设备组合，例如 `RS485 = USART + DE GPIO`（作为
aDevUsart 可选配置，统一收发接口，详见 [RS485 设计](usart_design.md)）、
`Flash25Q = QSPI + Flash 操作语义`。device 依赖 aOS 的单调时基实现软件超时，但
aDrv 不依赖 aOS。func 在其上构成 aDataBase（含 FlashDB 所需 FAL 适配）、aModbus
和 aShell 等功能。

工程不设置集中式 board 目录。引脚、外部器件型号、总线参数和设备句柄由使用它
们的应用模块持有。platform 不提供 `main()` 或自动初始化注册表。

## 更换芯片或项目

更换芯片时，保留 `aDrv/include` 的公共契约，替换 aDrv 的芯片实现、CMSIS Device
和厂商库，并新增对应 `config/mcu_<mcu>.cmake`。若 CPU 架构不同，再替换 aCore
内容。更换项目时只需在根 `CMakeLists.txt` 选择固件、MCU profile 和链接脚本，
并在 `config/` 与 app 中设置模块及产品资源。

## 应用设备初始化与句柄访问

当前 app/devices/system/app_system_device.c 持有 console 与 LED 的配置、缓冲区和私有
静态句柄；编号及类型明确的接口放在同目录 app_system_device.h。
通用 device 层仍分别提供 aDevUsart 和 aDevLed，不因为应用组合而合并设备类型。

启动顺序为 main → aDrvInit → aOSInit → 创建 appInit 任务 → aOSRun。
调度器启动后，由 main.c 内的 appInitTask 调用 aSystemInit；Shell/status 仍在
各自模块中创建任务。初始化任务优先级为 HIGH，初始化可能阻塞并允许其他就绪任务运行，
不承诺所有初始化完成前业务任务绝不执行。初始化成功后自删除，不复用为 workqueue。
初始化任务通过 aOSTaskExit() 自退出，无需保存全局任务句柄；
aOSDeleteTask(handle) 仍用于删除指定任务，传入 NULL 保持空操作语义。
system.c 内部的静态函数 statusInit 调用 appSystemStatusLedInit 并创建状态任务；
同文件内的静态函数 shellInit 调用 appSystemConsoleInit 完成控制台及 Shell 初始化，再创建调用 Process 的任务。
不存在集中初始化全部实例的注册表、链接段或分散加载。

应用通过专用 Init 合并初始化与接口获取：LED 返回句柄，控制台返回 aStream_t，
不提供独立 Open：

- 仅在驱动/OS 就绪后的启动阶段单线程调用，由应用保证每个实例只初始化一次。
- 首次初始化可能分配 OS 资源，不能当作无副作用查询。
- 直接初始化并返回本次结果，不缓存阶段或错误，不提供重复/重入保护；各实例独立，不联动回滚。
- NULL 输出参数返回 INVALID_PARAM；失败清空输出句柄或流字段。
- 句柄为共享借用，调用者不得销毁、反初始化或修改配置；运行阶段传递已取得的句柄。
- Shell 关闭时 console 声明、配置和实现一起裁剪。
- 不做运行时跨设备资源冲突检查；构建期资源告警尚未实现，设备参数/能力检查仍保留。

tests/app_devices/run.py 验证独立初始化、错误传递、参数检查和 Shell 裁剪，使用硬件替身，
不代表上板验证。通用契约见[接口规范](interface_contract.md)，USART 细节见
[USART 设计](usart_design.md)。

## 平台复用边界

更换芯片或 OS 时允许重写 app/devices；引脚、实例和初始化方式属于产品适配。
aDrv 保持 MCU 驱动定位。Linux 设备可使用独立实现，不要求模拟 IRQ/DMA 寄存器模型。
共同业务复用操作、错误、超时及所有权契约，不要求复用硬件资源配置。
当前 MCU 临界区仅针对单核；未来 Linux 后端必须使用实际线程同步，不能以 volatile
或空临界区代替。当前 MCU USART Async 回调统一在 ISR 中执行；DMA RX 使用调用者
快照区保证回调期间稳定，IRQ RX 保留零拷贝。未来 Linux API 必须明确自己的回调
执行上下文，不能声称用户态回调运行在硬件 ISR。引脚路由 Python/XML 校验尚未实现。
