# aDevUsart

当前实现按管理、TX、RX、单 DE RS485 分文件；只有 include/aDev_usart.h 对外公开。
aDrv 负责非阻塞硬件操作与 IRQ/DMA，aDev 负责消费互斥、超时、缓冲和回调，
app 负责配置、业务任务、排队和协议；不提供设备内 TX Queue。

## 初始化只有一套收发配置

mode 的 TX/RX 字段分别选择 POLLING、INTERRUPT_BUFFERED、DMA_BUFFERED，
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

设备缓冲区由 app 提供，有效至 DeInit 完成。静态不透明句柄并不意味着内部 OS 对象
完全不用堆；TX 超时定时器目前仍按需创建。生命周期由 app 串行管理。

## Async 是唯一的应用回调入口

不提供 CallbackSet、RegisterEventCallback、RegisterIsrEventCallback。
硬件回调由 aDev 注册；app 只能在 ReadAsync/WriteAsync 的 request 中传 callback 和 argument。
活动期间不替换回调：RX 先 Cancel，再重新 ReadAsync；TX 等本次终结后再提交。

回调直接在事件来源上下文执行：数据/TC 通常来自 ISR，取消来自调用者，
TX 超时来自 aOS 定时服务。没有固定 worker 上下文保证。
回调必须非阻塞、耗时有界，不得重入 USART 操作或销毁句柄。
可以直接读取、校验、轻量解析数据；复杂操作由 app 复制必要信息后通知自己的任务。
事件结构只在调用期间有效，不能保存事件指针。
回调参数在终结回调退出前必须有效，取消成功返回后可以回收。

## 普通与 Direct

Read/Write 返回实际长度，失败为 -1 并设置 aOS errno；已有部分结果优先返回长度。
Read 返回当前可用数据，不要求收满；Direct RX 收满或达到总超时后结束。
Direct 不经过内部 ring；CPU 轮询直传不是 DMA，也不代表 CPU 无搬运。
Write 返回代表数据已被接收/消费，不等于线路 TC；需要排空使用 WaitTransmitComplete。
ReadDirect 的 DMA 分段重装可能存在接收间隙，不承诺无限连续零丢包。

## 持续 ReadAsync

ReadAsync(request) 只包含 callback、argument；不再有 token、请求 FIFO、64 字节快照
或单次读取超时。它启用对初始化共享 ring 的独占消费，直到取消或错误。
未读的普通流数据须先用 Read 排空，否则启动返回 BUSY，不隐式丢弃旧数据。
订阅活跃时 Read/ReadDirect 返回 BUSY，重复 ReadAsync 不替换回调。

中断或 DMA 产生新数据时，DATA_READY 回调直接获得连续区间 buffer/length，可当场处理。
环绕最多分两段报告，IDLE 不是协议帧完成标志。回调结束后数据视为已经消费。
ReadAsyncCancel 停止订阅并报告 CANCELLED，但不停止底层 ring；后续数据可由 Read 消费。
ERROR 终止订阅，错误状态保持供诊断；需要应用明确恢复，不偷偷重启。

### 循环 DMA 的边界

DMA 不因进入 ISR 而暂停。数据指针只允许在回调期间读取，禁止修改或长期保存。
必须在 DMA 覆盖之前完成处理；回调前后进度检查能发现部分覆盖，但不能撤销已经进行的
协议处理，也不能在长期屏蔽 IRQ、硬件计数丢失等情况下证明未覆盖。
因此本接口是有处理时限的借用，不是无条件的数据稳定保证。
慢速/长期处理须由 app 复制；严格所有权交接留给未来 RX session。
应用应按波特率、ring 容量、最坏中断延迟验证预算，不能用平均处理时间代替。

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

默认 TTL，不操作方向 GPIO。RS485 只配置 enabled/de_pin/de_active_level。
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
