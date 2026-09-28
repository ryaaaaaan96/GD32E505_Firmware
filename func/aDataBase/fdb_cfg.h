/**
 * @file fdb_cfg.h
 * @brief FlashDB 的项目功能配置。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 启用 KVDB 和 FAL；写粒度 FDB_WRITE_GRAN 的单位是 bit，当前为 1。
 * 底层打印为空操作，FlashDB 不直接占用串口。
 */

#ifndef FDB_CFG_H
#define FDB_CFG_H

#define FDB_USING_KVDB
#define FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN 1

/* 日志由上层决定；FlashDB 底层不直接占用 USART 或 printf。 */
#define FDB_PRINT(...) ((void)0)

#endif
