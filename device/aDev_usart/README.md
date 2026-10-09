# aDevUsart

当前实现按管理、TX、RX、单 DE RS485 分文件；业务接口与实例定义分别由 aDev_usart.h 和 aDev_usart_instance.h 提供。
aDrv 负责非阻塞硬件操作与 IRQ/DMA，aDev 负责消费互斥、超时、缓冲和回调，
app 负责配置、业务任务、排队和协议；不提供设备内 TX Queue。

## 初始化只有一套收发配置

mode 的 TX/RX 字段分别选择 POLLING、INTERRUPT_BUFFERED、DMA_BUFFERED，
RX 还可选择独占的 INTERRUPT_CALLBACK，
OPTION_RX_IDLE 是接收附加功能。普通、Direct、Async 都使用对应方向的同一配置，
不再有 direct_tx_backend/direct_rx_backend 或第二份后端选择。
DMA_BUFFERED 名称表示具备缓冲流能力；不提供 ring 时可用于显式用户 buffer 操作。

- 轮询：普通与 Direct 可用；不支持 Async。
- 中断缓冲：普通收发、持续 ReadAsync 可用；当前不实现中断 Direct/TX Async，返回 UNSUPPORTED。
- DMA：提供 ring 时支持普通缓冲收发；TX Direct/Async 使用用户 buffer。
- DMA RX 不提供 ring（NULL、0）时只提供 ReadDirect，不能启用 IDLE 选项。
- 已启动的 DMA RX ring 不被 ReadDirect 抢占，返回 BUSY。
- 构建开关决定能力上限，初始化仍校验硬件路由、缓冲区与所选模式。
- 当前 ASYNC 构建包含 DMA TX 实现，配置解析仍要求驱动 DMA 能力；实例 RX 可选择中断。

设备缓冲区由 app 提供，有效至 DeInit 完成。静态设备对象并不意味着内部 OS 对象
完全不用堆；TX 超时定时器目前仍按需创建。生命周期由 app 串行管理。

## Async 与接收字节回调

不提供 CallbackSet、RegisterEventCallback、RegisterIsrEventCallback。
硬件 IRQ 分发始终由 aDev 注册。缓冲模式通过 ReadAsync/WriteAsync 请求传入回调。
活动期间不替换回调：RX 先 Cancel，再重新 ReadAsync；TX 等本次终结后再提交。

所有 Async 回调统一在 USART/DMA ISR 中执行。TX 超时由 aOS 定时服务记录终态，
TX/RX 取消由调用任务记录状态，再软件挂起 USART IRQ 派发回调。
回调必须非阻塞、耗时有界，不得重入 USART 操作或销毁句柄。
可以直接读取、校验、轻量解析数据；复杂操作由 app 复制必要信息后通知自己的任务。
事件结构只在调用期间有效，不能保存事件指针。
回调参数在终结回调退出前必须有效。Cancel 返回 OK 仅表示取消已受理；
回调可能在 Cancel 返回前或之后执行，应用通过 ISR 安全通知让任务在终态回调退出后回收资源。
等待终态期间 DeInit 返回 BUSY；不要在回调中调用任务版同步 API。

## 接收字节钩子与错误恢复

`ADEV_USART_RX_INTERRUPT_CALLBACK` 用于需要字节到达时刻的协议端口。
初始化配置 `rx_byte_callback/context`，每字节直接在 USART ISR 交付，不进入
设备 ring，也不创建 RX 等待对象。不支持 Read、ReadDirect、ReadAsync 或
IDLE 选项，不与缓冲消费共用。回调必须非阻塞，不得重入设备 API；context
在 DeInit 完成前有效。Modbus Demo 在此回调中保存帧边界，业务解析仍在任务。

硬件 ORERR/NERR/FERR/PERR 会被检查、清除并锁存为接收错误；字节回调收到
ERROR 时 byte 无效。缓冲 Read 被唤醒后返回错误，Async 报错并终止订阅。
应用先恢复协议边界，再用 `aDevUsartClearRxError` 清除软件错误；该操作不
清除缓存或 overflow 标志，不修复 DMA 硬件故障，后者需要重新初始化。
控制台适配在 Read 报错后清除锁存，Shell 丢弃异常半行至下个回车；
不会因一次硬件接收错误永久停止输入。

## 普通与 Direct

Read/Write 返回实际长度，失败为 -1 并设置 aOS errno；已有部分结果优先返回长度。
Read 返回当前可用数据，不要求收满；Direct RX 收满或达到总超时后结束。
Direct 不经过内部 ring；CPU 轮询直传不是 DMA，也不代表 CPU 无搬运。
Write 返回代表数据已被接收/消费，不等于线路 TC；需要排空使用 WaitTransmitComplete。
ReadDirect 的 DMA 分段重装可能存在接收间隙，不承诺无限连续零丢包。

## 持续 ReadAsync

ReadAsync(request) 包含 callback、argument，以及 DMA RX 使用的 buffer、buffer_size。
它启用对初始化 ring 的独占消费，直到取消或错误；没有请求队列和单次读取超时。
未读的普通流数据须先用 Read 排空，否则启动返回 BUSY，不隐式丢弃旧数据。
订阅活跃时 Read/ReadDirect 返回 BUSY，重复 ReadAsync 不替换回调。

- IRQ RX：直接借用 ring，回调返回前不释放占用区间；生产者满时丢弃新字节并报溢出，
  不覆盖正在处理的数据。保留零拷贝；环绕最多分两段回调；request.buffer 可为空。
- DMA RX：调用者提供独立快照区，容量至少等于 rx_buffer_size，不能与 ring 或其他活动
  缓冲区重叠。ISR 复制当前可用数据（环绕合并）并重新检查 DMA 进度；源区间已覆盖则
  只报告 ERROR，不发布该快照。复制通过后才发布 DATA_READY，回调期间 DMA 不会修改快照。

快照区由订阅独占，有效至 CANCELLED/ERROR 回调退出；DATA_READY 返回后可被下一次事件改写。
需要跨回调长期持有数据时由应用另行复制。每次 DMA 数据只做一次复制，不分配堆、不经过 worker。
ReadAsyncCancel 停止订阅并在 USART ISR 报告 CANCELLED，不停止底层 ring；后续数据可由 Read 消费。
ERROR 终止订阅，错误状态保持供诊断；需要应用明确恢复，不偷偷重启。IDLE 不代表协议帧完成。

DMA 订阅示例（ring 与 snapshot 均须持续有效）：

```c
static uint8_t rx_snapshot[RX_RING_SIZE];
aDevUsartReadRequest_t request = {
    .buffer = rx_snapshot,
    .buffer_size = sizeof(rx_snapshot),
    .callback = on_rx,
    .argument = context,
};
aStatus_t status = aDevUsartReadAsync(usart, &request);
```

### 循环 DMA 的边界

累计收发计数允许无符号回绕；物理读游标独立推进，不由累计计数对 ring
容量取模。发生覆盖后使用 DMA 当前物理写位置恢复，支持非二次幂容量。

快照保证已交付数据在当前回调期间稳定，不保证任意延迟下无丢包。
GD32 循环 DMA 完成标志不能累计多圈：必须保证最坏 IRQ 服务间隔小于一圈接收时间，
包括其他 ISR、关中断区和本回调执行时间。超出这一硬件观测前提，多圈覆盖可能无法检测。
ring 满一圈时间约为 `ring 字节数 × 每字节线路位数 / 波特率`；应用需留足余量并上板验证。
真正的 DMA 零拷贝需要缓冲区所有权交接，见下方未来 RX session，不能通过延长借用时间实现。

## WriteAsync

一个在途请求，不做发送排队。提交不等待 TX 锁，竞争时立即返回 BUSY。
有限 timeout 从提交阶段开始计时，内部准备和传输使用同一剩余预算。request 包含用户 buffer、size、总 timeout、callback、argument。
当前只实现 DMA 后端；传输期间用户 buffer 不得修改/释放。
正常结果以 USART TC 为准，RS485 先释放 DE 再回调。
超时/取消先停止 DMA 内存访问再回调；尾字节仍可能在移位寄存器中，
此时保持 DRAINING，等待 TC 释放 DE，期间拒绝新发送。
完成回调直接执行，不通过 aOS workqueue。
回调执行期间仍占用发送状态，不允许递归提交或销毁。

## TTL 与 RS485

默认 NONE，不操作 DE，可用于 TTL 或外部自动换向电路。
RS485 配置 mode/de_pin/de_active_level；GPIO_DE 由驱动操作 GPIO，
UART_DE 表示 USART 芯片自动控制，当前未实现，初始化前返回 UNSUPPORTED。
方向切换依据 TC，不依据 DMA 搬完；不处理 RE、协议帧间隔、回显或总线仲裁。

## 未来升级：RX session（未实现）

仅在需要严格缓冲区所有权和持续零拷贝时再实现，不作为本轮重构前置条件。
拟采用应用缓冲池 + current/next，不建立设备内无限队列：

1. 启动提交首块 buffer，设备取得写入使用权。
2. BUFFER_REQUEST 请求 next；app 从静态池补充。
3. READY 报告新增区间，不代表整个 buffer 已归还。
4. RELEASED 表示设备不再写入；app 处理结束后才可重新提交。
5. 没有 next 时停止，不覆盖已交付的块；停止归还所有缓冲并报告 DISABLED。
6. 与当前共享 ring 的 Read/ReadAsync 互斥，不自动抢占或迁移数据。

未来再确定 RxStart/RxBufferSupply/RxStop 名称和具体结构；目前没有这些 API、
配置宏或成功 stub。GD32 的硬件切换空窗、流控和丢包风险必须上板验证；
有 next 接口不代表硬件具备双缓冲或无缝切换能力。

## 验证

tests/usart/run.py 覆盖实际 device 源码与模拟硬件/OS；
tests/config/build_matrix.py 验证能力组合、链接与符号裁剪；
tests/aos/run.py 验证通知、定时器与 workqueue。
主机测试不替代真实 DMA/TC/DE 时序、ISR 延迟及吞吐测试。

## 同步流适配

USART 保留类型明确的 Read/Write/WaitTransmitComplete 接口，不依赖 aStream_t。
应用设备层的 appSystemConsoleInit 初始化串口并绑定 aShell 单例：read/write 转发普通
收发，write 已启动输出，flush 为 NULL。WaitTransmitComplete 单独用于等待线路
完成。适配不复制数据、不转移设备所有权。
所有流使用者停止后才能关闭底层设备。

## 静态实例与业务接口

业务代码包含 `aDev_usart.h`，通过不透明的 `aDevUsartHandle_t *` 使用设备。
实例创建代码包含 `aDev_usart_instance.h`，直接声明完整对象：

```c
static aDevUsartHandle_t console_handle;

status = aDevUsartInitStatic(&config, &console_handle);
```

实例头文件中的字段仅由 aDev 管理，应用不得直接修改。对象按真实类型分配，
无需额外的固定容量存储区；初始化后不得复制、移动或重复初始化。
失败后不得执行收发操作。动态创建仍使用 Create/Destroy 接口。

USART 对象分配接口通过 ADEV_USART_STATIC_ENABLE 和
ADEV_USART_DYNAMIC_ENABLE 独立裁剪，可同时开启；USART 启用时至少选择一种。
STATIC 控制 InitStatic，DYNAMIC 控制 Create/Destroy，DeInit 为共享接口。
当前产品关闭 STATIC、开启 DYNAMIC，控制台通过 Create 分配设备对象；
Shell 初始化失败时调用 Destroy。分配失败返回 A_STATUS_NO_MEMORY。
动态创建不改变应用提供的收发缓冲区的所有权及生命周期。
业务接口与实例定义分离的约定仍然适用；动态调用者无需包含实例头文件。

## 缓冲读取性能

中断缓冲 Read 按连续区间复制，复制期间不屏蔽中断；只在取得游标与提交
消费结果时进入临界区。未提交的数据保持占用，ISR 满缓冲时丢弃新字节，
不会覆盖正在复制的数据。RX/TX 的 ISR 单步游标通过边界比较回绕，支持
任意合法容量，不要求缓冲大小为 2 的幂。DMA 与 Async 的契约不变。
