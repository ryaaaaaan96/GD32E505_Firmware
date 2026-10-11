# func 边界

func 提供可复用功能，不拥有应用任务。禁止在 func 中调用 aOSCreateTask、
aOSDeleteTask 或直接使用 OS 创建线程；任务栈、优先级和退出顺序由应用配置；protocol 内装配通信任务。
模块可使用 aOS 锁、时基和内存服务；底层平台 worker、OS idle/timer task 不属于业务任务。

aShellInit 仅分配状态并绑定传输，aShellProcess 每次最多处理一个字符，
由应用任务或主循环调用；读取可能等待传输超时。aShellDeInit 前需停止所有调用者。
关闭 Shell 时 API 保留空实现，app 不创建 Shell 任务。

aMemory 管理设备和分区，通过应用提供的设备回调统一读写擦除。
aDataBase 封装 KV 和 TSDB，FlashDB 存储入口直接使用 aMemory，不依赖 FAL
或特定 Flash 型号。协议/功能模块不得直接绑定板级引脚或产品设备编号。

aLog 封装官方 EasyLogger，应用注入输出回调；默认不创建独立队列或任务。
当前应用复用 Shell 字节队列，后续可适配 Flash、文件或多个输出后端。
日志外层锁使用 aOS；上游配置和头仅私有可见，打印宏统一为 ALOG_*。


aPoll 只依赖 aOS，提供多实例周期作业、可合并事件和临时请求队列。
事件先置 pending 再 Notify，回调在应用调用 Process 的任务中运行。
aModbus 管采集分组、预生成报文和事务；aPoll 管周期、推进、等待及公平性。
应用 protocol 组合双方的配置和实例，并创建每条链路的执行任务。
