# 应用层组织

- app_config.h：应用配置宏的统一入口，包括任务栈、优先级、时序及设备模式和缓冲区大小。
- 系统 console 实例随 ASHELL_ENABLED 启用或裁剪，不再单独设置应用 USART 开关。
- devices/system/app_system_device.h：系统设备的 LED、console 编号及类型明确的初始化接口。
- devices/system/app_system_device.c：统一管理系统设备的 LED 与 USART 配置结构、缓冲区和私有句柄；配置宏从 app_config.h 引入。
- task/system/system.c：基础功能编排；shellInit 初始化 console、Shell 并创建应用 Shell 任务，任务循环调用 aShellProcess。
- task/system/status.c：初始化 status LED 实例，创建状态任务。
- main.c：初始化驱动和 OS，创建 appInitTask 后启动调度器；初始化任务完成系统功能初始化后通过 aOSTaskExit 自退出。

每个实例首次 Init 执行初始化，后续返回同一句柄或保存的错误，不重复初始化。
无独立 Open、统一设备初始化入口或分散注册。仅启动阶段单线程调用。
设备实例的配置结构归属 devices 下对应的 .c 文件；模式组合、缓冲区大小、闪烁周期、Shell 读写超时以及任务参数等应用配置宏统一放在 app_config.h。尚未宏化的简单参数仍直接填写在配置结构体中，不强制将所有值改成宏。各头文件的包含保护宏保留原位。平台功能裁剪仍由 config 下的 CMake 配置控制。

详见 [设备按实例初始化](../docs/architecture.md) 和 [接口规范](../docs/interface_contract.md)。

当前不做运行时跨设备资源冲突检查。后续可使用 Python/CMake 增加构建期告警，尚未实现；设备参数、能力和初始化错误检查继续保留。

业务任务统一归 app/task 所有，func 不创建/删除任务，也不配置任务优先级和栈大小。
func 提供 Init/Process/DeInit 或事件处理入口；app 决定调用线程、周期和停止顺序。
销毁 func 实例前必须停止其调用者并等待在途操作结束，禁止强行删除持有模块锁的任务。
system 设备公开 appSystemStatusLedInit 与 appSystemConsoleInit，保留按实例初始化，不做统一重初始化。
