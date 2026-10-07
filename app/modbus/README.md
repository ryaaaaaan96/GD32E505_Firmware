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
| 收发方式 | 中断缓冲收发，RX 512 字节、TX 256 字节 |

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

端口先将一帧收进 256 字节缓存，观察到连续 4 ms 无可读字节才交给协议层，
之后不会把下一帧拼进本帧。发送前也等待保守空闲窗口，错误后丢弃残留直到
再次空闲。应用端口同时检查基础功能码的 ADU 长度，拒绝合法写请求后追加
垃圾字节的情况，CRC 和数值校验继续由协议模块完成。

这依赖任务调度和 1 ms OS 时基，没有 ISR 字节时间戳或硬件帧定时器。
它不能严格判定 RTU 的 t1.5/t3.5，也不能重建任务长时间未运行期间的帧边界；
合法但间隔很短的连续帧可能被合并后拒绝。连续广播或共享总线其他站点通信
建议至少留 10 ms 空闲，并在目标负载下验证。当前 8N1 按本板约定配置。
需要严格 RTU 时序时应增加硬件计时的收帧端口，规范参考
[Modbus Serial Line V1.02](https://www.modbus.org/modbus-specifications)。

本轮主机测试覆盖主从、静动态、范围检查、广播、非本站帧、CRC 错误、
尾部垃圾、短帧、RX 溢出、主站超时及初始化失败回收；仍需实际板卡联调，
特别是 PA15 换向波形与任务栈余量。
