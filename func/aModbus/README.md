# aModbus：纯协议栈

基于 nanoMODBUS，负责 RTU/TCP 主从事务、请求编码、响应解析、协议校验及
RTU 默认时间的提供。时间的最终选择由应用负责，计数换算、间隔判断与等待
由外部通道执行。上游 `nanoMODBUS/` 源码保持不变。

协议核心不依赖 aBus、aComm 或设备驱动，不包含测点映射、订阅、轮询计划、
重试计数和任务。启用 aComm 时另外编译可选端点实现，提供协议规则与事务回调。
通用映射见 [aComm](../aComm/README.md)，应用装配见
[app/protocol](../../app/protocol/README.md)。

## 文件与接口

| 文件 | 职责 |
| --- | --- |
| `include/aModbus.h` | 传输配置、RTU 默认时间与帧检查、地址请求、执行结果、生命周期 |
| `include/aModbus_client.h` | 主站即时事务与固定读请求预编码 |
| `include/aModbus_server.h` | 从站配置、同步地址回调和请求处理 |
| `include/aModbus_endpoint.h` | 可选公共端点实现及通道回调绑定 |
| `include/*_instance.h` | 静态分配的真实实例布局 |
| `src/` | 模块实现与私有头 |

寄存器请求的 `data` 是按本机字节序保存的 `uint16_t` 数组，位请求是低位
在前的紧凑位数组。`quantity` 表示寄存器数或位数，`size` 表示缓冲区字节容量。
寄存器缓冲区须满足 `uint16_t` 对齐，地址从零开始。

支持 FC01/02/03/04/05/06/0F/10。读取上限 125 个寄存器或 2000 位，批量写入
上限 123 个寄存器或 1968 位。RTU 主站允许站号 0 广播写，禁止广播读；
从站站号为 1..247。TCP 仍由应用提供已建立连接的流，不负责监听或重连。

## 主站

`aModbusClientConfig_t` 只包含传输配置。应用填好同步 `aStream_t`，RTU 还须
提供输入恢复、帧前等待、发送完成回调，再调用 Create 或 InitStatic。

- `aModbusClientRead`：站号、区域、地址、数量及输出缓冲区。
- `aModbusClientWrite`：站号、区域、地址、数量及输入缓冲区。
- `aModbusClientPrepareRead`：根据 `aModbusReadSpec_t` 生成固定读请求，不访问端口。
- `aModbusClientReadPrepared`：执行准备好的请求，响应校验与普通事务相同。

固定请求保存于 `aModbusPreparedRead_t`，准备后至最后一次使用前不得修改。
RTU 缓存完整 8 字节请求及 CRC，TCP 在执行时生成事务编号和 MBAP。
临时写请求在执行时编码；协议栈不缓存业务数据，不自动重复发送。

`aModbusResult_t` 中：

- `exception` 保存协议异常；当前上游支持异常码 1..4，其他异常视为无效响应。
- `started` 表示已取得事务执行权，不表示报文已发出或远端已执行。
  入口并发 BUSY 时为 false，进入传输后的 BUSY 时为 true。
- `retryable` 只对可恢复的 RTU **读**通信错误置真，写事务、远端合法异常、
  TCP 故障及 EOF 不置真。它提供错误分类，重试次数和等待由调用方决定。

## 从站

`aModbusServerConfig_t` 只包含协议配置、站号、`read`、`write` 和回调 `context`。
应用注册地址回调，可连接 RAM、aComm 映射 映射或其他数据源，不要求使用 aBus。
read/write 至少提供一个，未提供的方向返回功能异常。

`aModbusServerProcess` 处理一次请求。地址回调在调用者任务中同步完成，
请求及缓冲区仅本次回调借用；返回 OK 须完成整个地址请求。
回调不得重入当前协议实例。

| 回调状态 | Modbus 异常 |
| --- | --- |
| NOT_FOUND | 02：非法地址 |
| INVALID_PARAM | 03：非法数值 |
| UNSUPPORTED | 01：非法功能 |
| 其他错误 | 04：设备故障 |

成功发送异常响应时 Process 仍返回 OK，异常码通过 result 返回。
从站不需要调度计划；接收、数据访问和响应在本次事务预算内完成。

## 传输与并发

所有事务仅用于任务上下文，不创建线程。每个协议实例通过原子占用标志拒绝
并发或重入调用（BUSY），不等待额外互斥锁。多个实例互不影响；同一物理端口
由应用统一串行使用，不能以多个实例绕过端口仲裁。

事务 timeout 控制整笔预算，byte_timeout 控制连续传输中的字节等待。
部分读写会继续补齐，但总预算不重置。正长度 read 返回 0 表示 EOF，当前
返回 EOF，后续调用返回 NOT_READY；无数据使用 -1/EAGAIN 或 -1/ETIMEDOUT。

完整报文写入后调用可选 stream.flush，再等待 RTU 线路发送完成。
缓冲流部分写入/flush 失败可能遗留半帧，实例停止复用；应用清理端口并重建。
nanoMODBUS 的输入清理回调与 stream.flush 的输出提交不是同一操作。
TCP 接收错误后不复用连接；RTU 错误后的下一事务先恢复输入。

额外检查 TCP PDU 长度和位响应的准确 byte count，避免使用旧缓存或把短响应
发布为有效值。预组帧路径与普通事务共用传输预算和恢复逻辑。

## RTU 规则与通道

时间和帧检查接口统一在 `aModbus.h`，不再单独提供 RTU 头文件。
`aModbusRtuTimingDefaults(baud_rate, character_bits, &timing)` 是可选助手，
仅提供微秒默认值，不接收时钟频率，不执行计数换算或等待。
字符位数包含起始、数据、校验、停止位，全部结果向上取整到微秒：

- 波特率大于 19200：字节间隔 750 us、帧间隔 1750 us。
- 其余波特率：字节间隔 1.5 字符、帧间隔 3.5 字符。

应用可调整默认值后交给通道，也可以直接配置最终值，完全不调用此函数。
两个 `AMODBUS_RTU_DEFAULT_*_GAP_US` 宏只适用于大于 19200 的波特率。
例如 115200、8N1 可配置字符时间 87 us 和上述两个间隔；降低波特率后须同步
调整时间，默认助手可避免重复手算。应用自行负责最终配置是否符合协议要求。
`aModbusRtuTiming_t` 只是默认值的输出，不由协议实例持有，也不依赖 aComm。
事务 `timeout` 和 `byte_timeout` 仍控制操作预算，不替代通道的帧间隔。
`aModbusRtuFrameCheck` 接收完整帧和 role/unit_id 上下文，检查站号及准确长度，
非目标返回 NOT_FOUND，损坏帧返回 ERROR，CRC 仍由协议事务处理。
必须在协议业务回调前验证整帧长度，避免带多余字节的写请求被部分执行。

本模块不再持有接收队列或 RTU 端口实例。应用创建
[aComm 通道](../aComm/README.md)，选择 GAP、传入时序并注册帧检查回调；
也可以提供符合相同契约的其他传输。通道执行字节收集、溢出处理、静默等待及
发送完成等待。设备层仍负责 GPIO、DE 方向和串口配置。

nanoMODBUS 保持同步事务：执行回调调用协议接口，协议通过注入的通道回调
收发数据。未改写上游为独立的异步编码/解码状态机。

## 分配与生命周期

AMODBUS_ENABLE、AMODBUS_STATIC_ENABLE、AMODBUS_DYNAMIC_ENABLE、
AMODBUS_CLIENT_ENABLE、AMODBUS_SERVER_ENABLE 控制模块、分配接口和角色。
启用本模块不要求启用 ABUS_ENABLE 或 ACOMM_ENABLE。

Create 动态分配实例；InitStatic 使用应用提供的零初始化对象，布局见
`aModbus_instance.h`。传输和回调上下文均借用，生命周期必须覆盖协议实例。
停止所有协议使用者后，再反初始化；通道须在接收生产者停止后另行释放；映射仍借用协议端点时不能释放。
Destroy/DeInit 不关闭串口或 TCP 连接，也不销毁业务映射。

## 验证

```sh
python3 tests/modbus/run.py
python3 tests/modbus_demo/run.py
python3 tests/modbus/build_matrix.py
```

覆盖 RTU/TCP、短帧/CRC/EOF、固定请求、主从回调、静动态分配和应用链路。
构建矩阵额外检查不启用 aBus/aComm 的纯协议栈及不启用 Modbus 的通用调度。
主机测试与编译通过不替代上板验收。


## 可选通信端点

`aModbusEndpoint_t.base` 是首成员。应用先初始化端点，再把 base 交给 aComm；
端点借用协议实例，只持有指针及从站回调，不创建线程、锁或堆对象。
从站把 aCommMappingServerRead/Write 填入端点，再将
 aModbusEndpointServerRead/Write 注册到协议配置，context 为端点地址。

端点负责寄存器编码：U8/U16 占一寄存器，U32/S32 占两个；字序由
AMODBUS_WORD_HIGH_FIRST/LOW_FIRST 选择。RAW 前一字节在高位，独立奇数 RAW
尾部低字节补零。位区域仅接受 0/1 的 U8。数量显式填写并与目标类型匹配。

整组 STRUCT 要求字段按 offset 连续覆盖 size，每项为偶数字节，支持
U16/U32/S32/偶数 RAW，不接受隐式填充或嵌套。quantity=size/2，字段偏移
为 offset/2。读取可截取任意寄存器区间，部分写入必须覆盖完整字段。
单项整组最多读 250 字节，写 246 字节；快照更新和事件通知由 aComm 与数据端点完成。

端点的 prepare/read_prepared 回调调用本模块固定读取接口，不在 aComm 中
计算 CRC。`aModbusBindChannel` 只把通用通道回调转交给协议传输，不改变时间。
关闭 ACOMM_ENABLE 后不编译端点，纯 client/server 协议接口继续可用。
