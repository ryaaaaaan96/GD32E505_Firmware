# aDataBase（FlashDB）

本模块使用 FlashDB 2.2.99 的 KVDB 功能，FlashDB 源码位于 `flashDB/`，采用 Apache-2.0 许可。

存储链路为：

```text
FlashDB KVDB -> aMemory/FAL -> aDev_Flash25q -> aDrv QSPI
```

应用层必须先初始化 Flash25Q 设备，再以 aMemory 提供的 `param` 或 `log`
分区名初始化 FlashDB：

```c
struct fdb_kvdb database = {0};
fdb_kvdb_init(&database, "param_db", "param", NULL, NULL);
```

当前示例应用没有目标 PCB 的 SQPI 引脚配置，因此只编译 FlashDB，不执行外部 Flash 数据库自检。
