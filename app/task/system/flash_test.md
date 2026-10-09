# Flash 手动测试

测试命令位于 `app/task/system/flash_test.c`，默认不参与固件编译。
需要重新调试时，取消 `app/task/system/CMakeLists.txt` 中下列行的注释：

```cmake
target_sources(${APP_TARGET} PRIVATE flash_test.c)
```

同时需要开启 `ADEV_FLASH25Q_ENABLE` 和 `ASHELL_ENABLE`，重新编译并烧录后
才能使用下列命令。未加入源码列表时，`flash info` 和 `flash test` 均不注册；
Flash 设备、启动探测和数据库存储仍按各自的配置正常工作。
使用设备层已初始化的应用 Flash 实例，不创建任务，不在启动时自动擦写。
测试期间应用不得同时访问该区域；每次接口调用的锁不覆盖整套测试流程。

```text
flash info
flash test <扇区起始地址>
```

地址支持十进制和 0x 十六进制。test 会擦除一个完整的 erase_size 区域，
确认全部为 FF，按 320 字节分块写入测试图案（覆盖 256 字节页边界），
再逐字节读回比较。成功打印 Flash test PASS；不一致打印首个错误地址。
测试结束保留测试图案，不恢复原数据，也不自动再次擦除。

例如，对于当前 8 MiB、4 KiB 擦除粒度的器件，最后一个扇区起点为
0x7FF000。仅在确定该扇区未使用时执行：

```text
flash test 0x7FF000
```

这不是保留的测试分区，不能据此认定它没有业务数据。
