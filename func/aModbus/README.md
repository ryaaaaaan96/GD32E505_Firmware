# aModbus：协议实现与通信表

基于 nanoMODBUS，负责 RTU/TCP 主从事务、请求编码、响应解析、协议校验及
RTU 默认时间的提供。时间的最终选择由应用负责，计数换算、间隔判断与等待
由外部通道执行。上游 `nanoMODBUS/` 源码保持不变。

本模块依赖 aComm 的公共通信结构与数据访问契约，不依赖 aBus 或设备驱动。
协议事务仍由 nanoMODBUS 执行；Modbus 地址映射、寄存器转换和协议数组回调
由本模块提供。周期分组、重试计数、数据绑定、事件队列、通道等待由 aComm 管理。
应用提供配置数组并注册回调，模块不创建任务。
应用装配见 [app/protocol](../../app/protocol/README.md)。

## 文件与接口

| 文件 | 职责 |
| --- | --- |
| `include/aModbus.h` | 传输配置、RTU 默认时间与帧检查、地址请求、执行结果、生命周期 |
| `include/aModbus_client.h` | 主站即时事务与固定读请求预编码 |
| `include/aModbus_server.h` | 从站配置、同步地址回调和请求处理 |
| `include/aModbus_endpoint.h` | 取得协议内嵌端点及通道回调绑定 |
| `include/aModbus_mapping.h` | 协议数组、数据映射及供应用注册的生命周期函数 |
| `include/*_instance.h` | 静态分配的真实实例布局 |

`src` 按实现职责保留六个源文件：

| 源文件 | 职责 |
| --- | --- |
| `aModbus.c` | 共享传输、生命周期、错误转换及 RTU 默认时间/帧检查 |
| `aModbus_client.c` | 主站同步事务、固定读请求预编码 |
| `aModbus_server.c` | 从站请求处理、nanoMODBUS 地址回调 |
| `aModbus_endpoint.c` | 接入 aComm、寄存器布局与字序转换规则 |
| `aModbus_mapping.c` | 映射生命周期、主从统一注册、事件执行和数组批次回调 |
| `aModbus_mapping_data.c` | 地址路由、完整/部分字段访问、批次结果写入数据绑定 |

主从映射入口和数组回调共用同一映射实例，合在 mapping.c 内用注释分段。
其生命周期辅助函数为文件内 static，不再通过私有头跨文件暴露。
地址转换规则并入 `aModbus_mapping_internal.h`，核心协议的内部声明保留在
`aModbus_internal.h`。公共 API 仍放在 include，文件合并不改变调用方式。
数据绑定、订阅槽和完成通知在 aComm，具体寄存器解释保留在本模块。

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
应用注册地址回调，可连接 RAM、本模块的数据映射或其他数据源，不要求使用 aBus。
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

事务 timeout 控制整笔预算，byte_timeout 控制连续传输中的单次底层读写等待。
一次底层调用可能传递整帧，配置该预算时应考虑本次调用可能传输的最大字节数。
部分读写会继续补齐，但总预算不重置。正长度 read 返回 0 表示 EOF，当前
返回 EOF，后续调用返回 NOT_READY；无数据使用 -1/EAGAIN 或 -1/ETIMEDOUT。

串口配置可直接使用常量初始化宏，公式为：

`timeout_ms = ceil(1000 × character_bits × byte_count / baud_rate) + margin_ms`

```c
static const aModbusCommConfig_t config = {
    .role = AMODBUS_ROLE_CLIENT,
    .transport_type = AMODBUS_TRANSPORT_RTU,
    .unit_id = 1U,
    .byte_timeout = AMODBUS_RTU_TIMEOUT_INIT(115200U, 10U, 256U, 20U),
    .timeout = AMODBUS_RTU_TIMEOUT_INIT(115200U, 10U, 512U, 500U),
};
```

参数依次为波特率、每字符总位数、线上字节数、用户余量（毫秒）。
8N1 为 10 位，8E1 为 11 位；字节数包含地址、功能码、数据和 CRC。
示例按单帧最多 256 字节、请求与响应合计最多 512 字节保守估算，
得到单次读写 43 ms、整笔事务 545 ms。实际已知最大帧长时可缩小预算。
余量由用户覆盖设备响应、调度和静默等待；从站还需考虑等待请求的空闲时间。
这些预算不替代通道分帧规则，也不是报文之间必须等待的延迟。

`AMODBUS_RTU_WIRE_TIME_MS` 仅返回向上取整的线路耗时。
`AMODBUS_RTU_TIMEOUT_INIT` 输出初始化列表，供 `static const` 等对象初始化，
不作为普通赋值表达式；参数使用无副作用的常量，波特率须非零，
字符位数为 7～16，字节数不超过 UINT32_MAX，最终超时须能由 uint32_t 表示。
中间运算使用 64 位整数；常量配置在编译期完成计算，不增加运行期分配或等待。

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

静态配置可使用 `AMODBUS_RTU_CHARACTER_US`、`AMODBUS_RTU_BYTE_GAP_US`、
`AMODBUS_RTU_FRAME_GAP_US` 宏，参数都是波特率和字符位数，计算与助手函数一致。
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
启用本模块必须启用 ACOMM_ENABLE，不要求 ABUS_ENABLE。
协议实例和协议映射跟随 AMODBUS_STATIC/DYNAMIC_ENABLE；
调度器、计划、通道跟随 ACOMM_STATIC/DYNAMIC_ENABLE，分配方式可以不同。

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
构建矩阵额外检查不启用 aBus 的协议模块、不启用 Modbus 的通用调度，
以及主从和混合静动态配置。
主机测试与编译通过不替代上板验收。


## 协议数组注册

应用维护 aModbusClientMap_t 数组。首成员 comm 是 aCommEntry_t，周期、权限、
目标与事件配置都在公共部分，target 已平铺：

```c
static const aModbusClientMap_t entries[] = {
    {
        .comm = {
            .poll_time_ms = 1000U,
            .flags = ACOMM_MAP_FLAG_READ,
            .deviceID = 2U,
            .sigIndex = 0U,
            .paramIndex = ACOMM_DATA_WHOLE,
            .subscribe_events = ACOMM_REQUEST_READ,
            .publish_events = 0U
        },
        .area = AMODBUS_AREA_HOLDING_REGISTERS,
        .address = 2U,
        .quantity = 4U,
        .word_order = AMODBUS_WORD_HIGH_FIRST
    }
};
```

应用将具体数组放在 aCommConfig_t.protocol.items/count，并将
aModbusCommConfig_t 作为不透明 config 注册。生命周期回调表由应用定义，
模块提供可直接注册的 aModbusCommCheck/Init/Process/DeInit 函数：

```c
/* 应用从站配置；主站 process 可为 NULL，使用 init 返回的采集表执行。 */
static const aCommProtocolOps_t comm_ops = {
    .check = aModbusCommCheck,
    .init = aModbusCommInit,
    .process = aModbusCommProcess,
    .deinit = aModbusCommDeInit,
};
```

应用配置填写 `protocol.ops = &comm_ops`；回调表具有静态生命周期，
各设备可独立选择或包装回调。模块不提供全局生命周期回调表或自动注册。
init 创建主从实例及映射；主站返回 aModbusClientTableOps，
从站 process 调用 aModbusServerProcess。aComm 负责计划和作业创建，
应用负责选择回调和装配资源。

应用持有零初始化的 aModbusComm_t，填写 channel、binding.data，按需提供
submit/submit_context 和静态 protocol_storage/mapping_storage。
未提供静态资源时使用动态接口；纯静态构建必须提供这两个存储对象。
绑定的通道和数据端点不由注册回调释放。
aModbusCommConfig_t 平铺 role、transport_type、unit_id、byte_timeout、timeout，
不嵌入带收发回调的 aModbusConfig_t；初始化时组装底层配置并注入通道回调。
transport_type 表示 Modbus 的 RTU/TCP 报文格式，决定 CRC、MBAP 等编码规则；
aCommChannelConfig_t.mode 表示 STREAM/GAP 收发方式。两者分别配置，不能互相推导。
主站与从站分别通过 role 区分，Modbus 自己解释；aComm 不包含 Modbus 类型。
站号和超时对整台设备统一配置，逐项不重复；主站的 timeout 是事务预算，
从站是一次 ServerProcess 的等待预算。零周期项仍参与事件或即时读写。

直接使用既有的协议/映射 API 仍然有效；外部自行初始化的主站映射可通过
`aCommConfig_t.table` 注册 `aModbusClientTableOps`，此时 aComm 不拥有其生命周期。

Modbus 只回答相邻项能否合并，aComm 决定分组、执行时机与重试。
初始化缓存固定读取报文，执行时不重新选择测点；临时写入按本次数据编码。
acquire/release 记录计划借用，计划未释放时映射销毁返回 BUSY。

aModbusServerMap_t 同样以 comm 为首成员，权限和数据目标位置一致。
从站根据收到的报文查找 maps/ranges，使用同步地址回调，不轮询映射数组。
统一注册回调自动将 aModbusMappingServerRead/Write 接入协议配置；
使用底层接口时也可由应用自行设置其他地址回调。

## 内嵌端点与数据映射

aModbusHandle_t 的首成员是 aCommEndpoint_t。
`aModbusCommEndpoint(handle)` 返回这个成员，不再创建独立的 aModbusEndpoint_t。
传 NULL 返回仅用于初始化预检查的规则端点，不能执行协议事务。
协议实例存续期间不能移动；取得端点不增加引用计数，应用按顺序管理生命周期。

寄存器编码：U8/U16 占一寄存器，U32/S32 占两个；字序由
AMODBUS_WORD_HIGH_FIRST/LOW_FIRST 选择。RAW 前一字节在高位，独立奇数 RAW
尾部低字节补零。位区域仅接受 0/1 的 U8。quantity 必须显式填写并匹配目标。

整组 STRUCT 要求字段按 offset 连续覆盖 size，每项为偶数字节，支持
U16/U32/S32/偶数 RAW，不接受隐式填充或嵌套。quantity=size/2，字段偏移
为 offset/2。读取可截取任意寄存器区间，部分写入必须覆盖完整字段。
单项整组最多读 250 字节，写 246 字节。默认通过 aCommBinding 按 SIG
取快照或调用数据端点 update；协议回调只处理快照中的寄存器转换。
跨 SIG 不回滚，自定义 sig_read/sig_write 由提供者保证权限、范围与同步。

maps 与 ranges 平级，各自按 area/address 排序且不重叠，允许空洞。
从站使用二分查找；回调段命中后失败不再尝试其他映射。
值工作区默认 ACOMM_VALUE_CAPACITY=256，大 SIG 可配置独占 snapshot，
但单个传输目标和合并响应仍受值工作区和协议容量限制。

主站订阅 REQUEST_READ/WRITE 或 MARK_ONLY，不把 UPDATED 自动转成下发。
从站可订阅变化作为观察通知；通知仅登记或尝试入队，不等待通信。
整组与字段按交集匹配；写请求使用请求快照，借用至完成回调返回。
映射实例嵌入 `aCommBinding_t comm`，绑定配置位于 `binding.comm`。
静态映射提供 `comm.subscriptions`、`comm.subscription_capacity` 及原生节点；
动态映射使用 aComm 的存储布局接口，一次分配宿主、槽位和原生节点。
未订阅项不占订阅 RAM。请求槽、入队、通用目标检查和完成发布均由 aComm 管理；
Modbus 只注册执行回调，按实际目标计算地址并执行事务。

```c
mapping.binding.comm.data = &data_endpoint.base;
mapping.binding.protocol = aModbusCommEndpoint(protocol);
/* 可选：binding.comm.sig_read/sig_write/sig_context 和外部 snapshot。 */
```

通用读写回调使用 `aCommValueReadRequest_t` / `aCommValueWriteRequest_t`，
不再使用 Modbus 命名的 SIG 回调类型。部分地址访问是否合法、报文容量和
字序转换仍由 Modbus 判断；通用绑定不解释寄存器。

`aModbusBindChannel` 只转交通道回调，不修改用户时间或取得通道所有权。
释放顺序为：停止生产者与消费者、排空请求、销毁 aComm、销毁映射、销毁协议、
停止接收并释放通道。禁止在仍有借用期间提前销毁协议或数据端点。
