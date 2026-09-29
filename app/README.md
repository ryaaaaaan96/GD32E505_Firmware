# 应用层组织

- 系统 console 实例随 ASHELL_ENABLE 启用或裁剪，不再单独设置应用 USART 开关。
- devices/system/app_system_device.h：系统设备的 LED 句柄、console 流专用初始化接口。
- devices/system/app_system_device.c：统一管理系统设备的 LED 与 USART 配置结构、缓冲区和私有句柄；参数直接在本文件中配置。
- task/system/system.c：统一管理 LED 状态任务和 Shell 任务，内部逐项初始化设备、服务并创建任务；对外仅提供 aSystemInit。
- task/system/system.h：声明系统服务启动入口 aSystemInit。
- main.c：初始化驱动和 OS，创建 appInitTask 后启动调度器；初始化任务完成系统功能初始化后通过 aOSTaskExit 自退出。

每个实例由应用保证只调用一次 Init，直接返回底层初始化结果，不缓存阶段或错误。
运行阶段传递已取得的句柄；不通过重复 Init 获取句柄，也不提供重入保护。
无独立 Open、统一设备初始化入口或分散注册。仅启动阶段单线程调用。
设备参数在 devices 下对应 .c 文件的配置结构及缓冲区声明中直接设置；任务栈、优先级和单次使用的延时在使用处设置。重复使用的状态灯闪烁周期保留为 system.c 内的具名常量。各头文件的包含保护宏保留原位，平台功能裁剪由 config 下的 CMake 配置控制。

详见 [设备按实例初始化](../docs/architecture.md) 和 [接口规范](../docs/interface_contract.md)。

当前不做运行时跨设备资源冲突检查。后续可使用 Python/CMake 增加构建期告警，尚未实现；设备参数、能力和初始化错误检查继续保留。

业务任务统一归 app/task 所有，func 不创建/删除任务，也不配置任务优先级和栈大小。
func 提供 Init/Process/DeInit 或事件处理入口；app 决定调用线程、周期和停止顺序。
销毁 func 实例前必须停止其调用者并等待在途操作结束，禁止强行删除持有模块锁的任务。
system 设备公开 appSystemStatusLedInit 与 appSystemConsoleInit，保留按实例初始化，不做统一重初始化。

控制台通过 `appSystemConsoleInit(void)` 初始化 USART、绑定 Stream 并初始化 aShell 单例，
USART 类型、私有控制台句柄指针和适配回调只在 app/devices 内部使用；
流接口不携带 context，回调直接访问已绑定的控制台句柄。aStream_t 定义在
platform/aLib/include/aStream.h；Shell 配置在设备初始化函数内构造，任务层创建并运行 Shell 任务。
flush 非空时表示显式提交缓冲输出；NULL 表示 write 已直接提交，无需刷新。
flush 不等待线路完成、不丢弃输入；Shell 不自动调用。

当前控制台使用 aDevUsartCreate 动态分配对象，Shell 初始化失败时调用
aDevUsartDestroy 回收；产品必须开启 ADEV_USART_DYNAMIC_ENABLE。
