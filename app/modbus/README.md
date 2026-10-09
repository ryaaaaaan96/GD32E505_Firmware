# Modbus RTU 应用演示

默认运行从站，也提供同一串口上的主站采集演示。两种角色通过编译宏选择，
一块板同一时刻只运行一种角色，只有 Modbus 任务读写此串口。

## 硬件与通信参数

| 项目 | 当前配置 |
| --- | --- |
| 外设 | GD32 USART2，部分重映射 |
| TX / RX | PC10 / PC11 |
| DE | PA15，高电平发送、低电平接收 |
| 通信格式 | 115200，8 位数据，无校验，1 位停止位 |
| 站号 | 从站地址 1；主站的远端目标地址也是 1 |
| 收发方式 | ISR 逐字节接收，3 个 256 字节帧槽；TX 中断缓冲 256 字节 |

引脚对应关系参考 [GD32E505 数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.2.pdf)。
代码使用 `ADRV_USART_2`，原工程的 `#define Usart2 1` 不用作驱动实例编号。
aDrv 根据 USART2 引脚选择默认、部分或完全重映射；PA15 用作 GPIO 时释放
JTAG，保留 PA13/PA14 的 SWD 调试。DE 先置低，再交出 JTAG 引脚控制权。
这属于全局引脚复用配置，GPIO 反初始化不会重新打开 JTAG。

DE 由 aDevUsart 的 GPIO_DE 模式控制：提交发送时拉高，TC 确认最后一个
停止位完成后拉低。协议端口还会调用 `aDevUsartWaitTransmitComplete()`，
不会把写入 TX 缓冲区当作线路发送完成。不使用 USART DMA。
当前连接假定一个 PA15 就能控制收发方向，不另配置独立 /RE。

## 切换主从

编辑 `config/aclass_config.cmake`，重新编译并烧录：

```cmake
set(APP_MODBUS_DEMO_ENABLE ON)
set(APP_MODBUS_MASTER_ENABLE OFF) # OFF 从站；ON 主站
set(ADEV_USART_RS485_ENABLE ON)
```

CMake 向应用导出 `APP_MODBUS_MASTER_ENABLE=0/1`，源码使用 `#if` 裁剪。
需要 aModbus、相应 CLIENT/SERVER 能力、USART 中断和 RS485 能力；关闭依赖
时应用示例一起裁剪。其他产品不设置 APP 开关时不会自动启用此示例。
站号在 `app_modbus.h` 的 `APP_MODBUS_UNIT_ID` 统一设置。

## 从站寄存器

所有地址都是保持寄存器的**零起始偏移**。每个寄存器内部大端，32 位数据
高 16 位在前。读取使用 FC03，写入使用 FC10，一次完整写入两个寄存器。

| 地址 | 常见 4xxxx 表示 | 本地 SIG | 类型 | 初值 / 约束 |
| --- | --- | --- | --- | --- |
| 0、1 | 40001、40002 | counter，sigIndex=1 | U32 | 初值 0，测试任务每秒加 1 |
| 2、3 | 40003、40004 | motor.speed，sigIndex=0，paramIndex=0 | U32 | 初值 100，范围 0..6000 |
| 4、5 | 40005、40006 | motor.temperature，sigIndex=0，paramIndex=1 | S32 | 初值 25 |

例如把 speed 设置成 1500：FC10、起始地址 2、数量 2，寄存器值为
`0x0000, 0x05DC`。负温度按 32 位补码编码，例如 -20 为 `0xFFFF, 0xFFEC`。
对上述 32 位映射仅写一个寄存器会返回异常 02；speed 超过 6000 返回异常 03。
支持站号 0 的广播写入，广播不返回响应。未映射线圈和输入寄存器。

PC 上通过 USB-RS485 主站工具选择 `115200 / 8N1 / 地址 1`，先用 FC03
读取地址 0 开始的 6 个保持寄存器，再尝试 FC10 修改 speed。
普通文本串口终端不能直接发送 Modbus 请求；调试 Shell 仍使用原 USART0。
建议主站轮询间隔 100 ms，响应超时 1000 ms。

Shell 可查看或修改同一份 aBus 数据：

```text
sig get 1            # 显示设备 1 的全部测点和元信息
sig get 1 1          # counter
sig get 1 0          # motor 的全部字段
sig get 1 0 0        # motor.speed
sig set 1 0 0 1500   # 修改 motor.speed，再从 Modbus 读取验证
sig set 1 1 100      # counter 设置为 100，随后仍每秒递增
```

示例行尾的 `#` 是说明，不要输入终端。counter 任务仍直接操作静态绑定变量，
与协议写入的同步策略保持原演示约定，可能与递增互相覆盖；motor 通过
aBus 接口及其锁访问，更适合验证双向读写。

## 主站演示

主站每轮用 FC03 从远端站号 1 读取地址 2、数量 2，解码为 U32，写入
本地 `motor.speed`。每轮完成后等待 1000 ms；单次事务预算 500 ms，
因此这是轮询演示，不是精确一秒周期调度。

初始化后，通信状态变化时 Shell 打印 `online` 或 `read failed`。
CRC 错误、超时、远端异常以及超过本地范围的值不会覆盖本地旧值。
读失败保留旧值不代表数据仍然新鲜，正式业务可另加通信健康或采样有效性 SIG。
本示例不自动向远端写参数，库的主站写入接口仍保留。

两块板联调：一块编译为从站，另一块编译为主站，连接 RS485 总线；
在从站 Shell 执行 `sig set 1 0 0 1500`，随后在主站执行
`sig get 1 0 0`，应读到 1500。确认 A/B、参考地和终端电阻与板级电路相符。

## 文件职责与启动顺序

- `app/devices/modbus/app_modbus_port.c`：串口配置、收帧缓存、错误清理、
  空闲等待和 TC 确认，不解析业务值。
- `app/modbus/app_modbus.c`：主从配置、寄存器映射、单笔协议处理及生命周期。
- `app/task/modbus/app_modbus_task.c`：唯一通信任务、栈和轮询间隔。
- `app/sig/app_sig_modbus.h`：应用内部装配入口，为协议绑定私有 aBus handle；
  不公开 SIG handle，也不复制第二张运行数据表。

`aSystemInit()` 先初始化 SIG 和计数测试任务，再初始化 Modbus 及其任务，
最后启动 Shell 任务。协议创建或任务创建失败会回收本次协议/串口资源。
动态接口可用时优先动态创建，否则使用显式静态实例；纯静态配置也已验证。

## 时序边界

接收使用 `ADEV_USART_RX_INTERRUPT_CALLBACK`，在 USART ISR 中读取 DWT
周期计数，并按 750 us / 1750 us 判断帧内间隔和帧间静默。RXNE 时间戳位于
字符末尾，因此相邻时间戳比较增加 115200 / 8N1 的一个字符时间（向上取整
87 us）；任务封帧也等待这一余量，避免截断尚未完成的字符。
毫秒时基仅区分长空闲，短间隔由无符号周期差判断，支持计数器回绕。
时序依据见 [Modbus Serial Line V1.01](https://modbus.org/docs/Modbus_over_serial_line_V1_01.pdf)；
8N1 为当前板级约定。

ISR 保存完整帧边界，任务延迟时仍可保留最多 3 帧；满队列丢弃新帧，
不覆盖待消费帧。每帧最多 256 字节，超长、硬件接收错误及帧内间隔超限
都会丢弃当前帧，下一次合法静默后恢复。`dropped_frames` 可在调试器中观察。
应用端口另外核对基础功能码的 ADU 长度，CRC、站号和数值校验由协议模块处理。

发送前确认上一笔 TC / DE 已完成，并等待线路静默。只有实际发送过数据才
更新 TX 时间戳，避免每次发送前无条件多等一轮。帧接收任务仍以 1 ms 轮询
等待，因此响应延迟包含调度和轮询时间；帧边界不再取决于任务何时读取字节。

周期计数器开启失败时初始化返回错误；通信期间要求内核时钟固定，且不进入
会暂停 DWT 的休眠或调试状态。这是 ISR 观测时间，仍存在中断响应延迟，
不是硬件捕获。115200 / 8N1 每字符约 87 us，必须控制最坏 IRQ 延迟，避免
数据寄存器溢出及临界间隔的误判。严格边界精度仍需硬件捕获/定时接收方案。

主机测试覆盖主从、静动态、范围检查、广播、非本站帧、CRC 错误、尾部垃圾、
短帧、硬件错误、帧内间隔、任务暂停期间的多帧、周期回绕、队列满、主站超时
和初始化失败回收。仍需板卡验证 PA15 换向、最坏 ISR 延迟和任务栈余量。
