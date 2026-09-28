/**
 * @file fal.h
 * @brief 供 FlashDB 使用的本仓库精简 FAL 适配接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 不是完整上游 FAL；先通过 aDataBaseBindStorage() 绑定存储。
 * 返回的描述符由模块持有，不可修改或释放；解绑后不得继续用于读写。
 * 本接口沿用 FlashDB 适配返回约定：读写擦除返回 0/-1，而不是字节数或 aOS errno。
 */

#ifndef FAL_H
#define FAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 模块持有的介质描述符；调用者不修改或释放。 */
struct fal_flash_dev {
    char name[8]; /**< NUL 结尾名称，最多 7 个字符。 */
    uint32_t addr; /**< 介质基址，当前适配器为 0。 */
    size_t len; /**< 介质容量，字节。 */
    size_t blk_size; /**< 擦除块大小，字节。 */
};

/** @brief 模块持有的分区描述符；偏移/长度均以字节计。 */
struct fal_partition {
    char name[8]; /**< NUL 结尾分区名，最多 7 个字符。 */
    char flash_name[8]; /**< 所属 Flash 名称。 */
    const struct fal_flash_dev *flash_dev; /**< 借用的设备描述符。 */
    uint32_t offset; /**< 相对整个介质的起点。 */
    uint32_t len; /**< 分区容量。 */
};

/**
 * @brief 保留的 FAL 初始化入口。
 * @note 当前为空操作；不会隐式绑定存储，也不会初始化外设。
 */
void fal_init(void);

/**
 * @brief 按名称查找已绑定存储的静态分区。
 * @param[in] name 以 NUL 结束的分区名。
 * @return 借用的分区描述符；NULL 名称、未绑定或未找到返回 NULL。
 */
const struct fal_partition *fal_partition_find(const char *name);

/**
 * @brief 按名称查找已绑定 Flash 描述符。
 * @param[in] name 以 NUL 结束的设备名。
 * @return 借用的设备描述符；NULL、名称不符或未绑定返回 NULL。
 */
const struct fal_flash_dev *fal_flash_device_find(const char *name);

/**
 * @brief 从分区内偏移读取，调用绑定的同步 read 回调。
 * @param[in] part 本模块返回的有效分区描述符。
 * @param[in] address 相对分区的字节偏移。
 * @param[out] buffer 至少 size 字节的目标，不得为 NULL。
 * @param[in] size 字节数，不得越界或超过 UINT32_MAX。
 * @return 成功 0，校验或后端失败 -1；超时使用编译期存储操作预算。
 */
int fal_partition_read(const struct fal_partition *part, uint32_t address,
                       uint8_t *buffer, size_t size);

/**
 * @brief 在分区内编程，调用绑定的同步 write 回调。
 * @param[in] part 本模块返回的有效分区描述符。
 * @param[in] address 相对分区的字节偏移。
 * @param[in] buffer 至少 size 字节的源，不得为 NULL。
 * @param[in] size 字节数，不得越界或超过 UINT32_MAX。
 * @return 成功 0，校验或后端失败 -1；不自动擦除，不回报部分进度。
 */
int fal_partition_write(const struct fal_partition *part, uint32_t address,
                        const uint8_t *buffer, size_t size);

/**
 * @brief 擦除分区内的指定范围。
 * @param[in] part 本模块返回的有效分区描述符。
 * @param[in] address 相对分区字节偏移，满足后端擦除对齐要求。
 * @param[in] size 擦除字节数，满足后端对齐和容量限制。
 * @return 成功 0，校验或后端失败 -1；已经擦除的内容不会回滚。
 */
int fal_partition_erase(const struct fal_partition *part, uint32_t address,
                        size_t size);

#ifdef __cplusplus
}
#endif

#endif
