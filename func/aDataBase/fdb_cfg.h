#ifndef FDB_CFG_H
#define FDB_CFG_H

#define FDB_USING_KVDB
#define FDB_USING_FAL_MODE
#define FDB_WRITE_GRAN 1

/* 日志由上层决定；FlashDB 底层不直接占用 USART 或 printf。 */
#define FDB_PRINT(...) ((void)0)

#endif
