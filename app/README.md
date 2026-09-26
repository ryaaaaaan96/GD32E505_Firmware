# 应用层组织

- devices/led/app_led.h、devices/usart/app_usart.h：各自定义设备编号和初始化接口。
- devices/led/app_led.c：私有配置、句柄，appLedInit(id, &handle)。
- devices/usart/app_usart.c：私有配置、缓冲区和存储，appUsartInit(id, &handle)。
- task/system/system.c：基础功能编排；shellInit 初始化 console 实例并接入 Shell。
- task/system/status.c：初始化 status LED 实例，创建状态任务。
- main.c：驱动、OS、系统功能初始化，然后启动调度器。

每个实例首次 Init 执行初始化，后续返回同一句柄或保存的错误，不重复初始化。
无独立 Open、统一设备初始化入口或分散注册。仅启动阶段单线程调用。
固定参数仍位于 task/system/aclass_system_config.h。

详见 [设备按实例初始化](../docs/device_registry.md) 和 [接口规范](../docs/interface_contract.md)。

当前不做运行时跨设备资源冲突检查。后续可使用 Python/CMake 增加构建期告警，尚未实现；设备参数、能力和初始化错误检查继续保留。
