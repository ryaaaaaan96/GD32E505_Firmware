/**
 * @file aDatabase_flash_layout.h
 * @brief 当前产品的 FAL 编译期存储布局。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 容量、偏移、分区长度和擦除块均以字节表示；操作超时以毫秒表示。
 * 后端容量/擦除粒度必须与布局匹配。偏移基于整个介质，不是分区内相对地址。
 */

#ifndef ADATABASE_FLASH_LAYOUT_H
#define ADATABASE_FLASH_LAYOUT_H

/* Product-specific storage layout used by the FlashDB FAL adapter. */
#define ADATABASE_FLASH_DEVICE_NAME "flash25"
#define ADATABASE_FLASH_BLOCK_SIZE 4096U
#define ADATABASE_FLASH_OPERATION_TIMEOUT_MS 5000U
#define ADATABASE_FLASH_CAPACITY_BYTES (16U * 1024U * 1024U)

#define ADATABASE_PART_PARAM_NAME "param"
#define ADATABASE_PART_PARAM_OFFSET 0x00100000U
#define ADATABASE_PART_PARAM_SIZE   0x00020000U

#define ADATABASE_PART_LOG_NAME "log"
#define ADATABASE_PART_LOG_OFFSET 0x00120000U
#define ADATABASE_PART_LOG_SIZE   0x00080000U

#endif
