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


aModbus 提供 RTU/TCP 主从事务、具体协议数组和地址映射回调，依赖 aComm，
不依赖 aBus 实现。协议表项首成员是 aCommEntry_t，周期、权限、目标和事件
配置在公共部分；协议地址与类型转换由 aModbus 解释。
aComm 只依赖 aOS/aLib，通过注册回调遍历数组、构建计划、执行调度，管理
有限重试、事件、请求队列和通用通道。数据绑定管理快照工作区、订阅槽、
非阻塞入队及完成发布，通过公共端点契约接入数据容器。
应用 protocol 配置并注册数组、端口与上下文，管理任务；不再组装 jobs/groups。
从站通过同步地址回调接入协议映射；应用注册连续作业，不创建采集计划。
