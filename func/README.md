# func 边界

func 提供可复用功能，不拥有应用任务。禁止在 func 中调用 aOSCreateTask、
aOSDeleteTask 或直接使用 OS 创建线程；任务栈、优先级、周期和退出顺序由 app/task 配置。
模块可使用 aOS 锁、时基和内存服务；底层平台 worker、OS idle/timer task 不属于业务任务。

aShellInit 仅分配状态并绑定传输，aShellProcess 每次最多处理一个字符，
由应用任务或主循环调用；读取可能等待传输超时。aShellDeInit 前需停止所有调用者。
关闭 Shell 时 API 保留空实现，app 不创建 Shell 任务。

aDataBase 封装 KV 和 TSDB，通过 aDataBaseStorage_t 注入存储操作，不依赖特定 Flash 型号；
Flash25Q 适配是可选 target。协议/功能模块不得直接绑定板级引脚或产品设备编号。
