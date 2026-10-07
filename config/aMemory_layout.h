/**
 * @file aMemory_layout.h
 * @brief 当前产品的 aMemory 编译期存储布局。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 容量、偏移、分区长度和擦除块均以字节表示；超时由各请求单独提供。
 * 设备容量/擦除粒度必须与布局匹配。偏移基于整个介质，不是分区内相对地址。
 */

#ifndef AMEMORY_LAYOUT_H
#define AMEMORY_LAYOUT_H

/* 当前板上的八兆字节 Flash；末尾测试扇区不属于数据库分区。 */
#define AMEMORY_FLASH_DEVICE_NAME "flash25"
#define AMEMORY_FLASH_BLOCK_SIZE 4096U
#define AMEMORY_FLASH_CAPACITY_BYTES (8U * 1024U * 1024U)

#define AMEMORY_PART_PARAM_NAME "param"
#define AMEMORY_PART_PARAM_OFFSET 0x00100000U
#define AMEMORY_PART_PARAM_SIZE   0x00020000U

#define AMEMORY_PART_LOG_NAME "log"
#define AMEMORY_PART_LOG_OFFSET 0x00120000U
#define AMEMORY_PART_LOG_SIZE   0x00080000U

#endif
