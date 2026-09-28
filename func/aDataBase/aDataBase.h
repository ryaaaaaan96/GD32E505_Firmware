/**
 * @file aDataBase.h
 * @brief FlashDB 的通用存储后端绑定接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 当前只有一个全局存储后端；本层不拥有 context，也不初始化具体外设。
 * 先初始化存储设备，再绑定，最后初始化 FlashDB。关闭所有数据库并停止所有用户后
 * 才能解绑和释放 context。绑定生命周期由调用方串行化，不提供热切换保证。
 */

#ifndef ADATABASE_H
#define ADATABASE_H

#include "aLib.h"

/**
 * @brief 同步存储操作表；成功必须完成全部请求，失败返回 aStatus_t。
 * 回调 address 是整个存储介质内的字节偏移，size 为字节数，timeout 是总预算。
 * read/write 的 buffer 只在调用期间借用；返回后不得继续访问。
 * FAL 不提供全局操作锁；后端自行串行化介质访问，FlashDB 实例也需遵循自身锁约定。
 */
typedef struct {
    void *context; /**< 原样传给后端回调；生命周期覆盖整个绑定期。 */
    size_t capacity; /**< 存储容量，字节，必须匹配 FAL 布局。 */
    size_t erase_block_size; /**< 擦除块字节数，必须匹配 FAL 布局。 */
    /** 读取 size 字节到 buffer；非空必填，不负责分配输出。 */
    aStatus_t (*read)(void *context, uint32_t address, uint8_t *buffer,
                      uint32_t size, aTimeout_t timeout);
    /** 编程 size 字节；非空必填，不隐式擦除。 */
    aStatus_t (*write)(void *context, uint32_t address, const uint8_t *buffer,
                       uint32_t size, aTimeout_t timeout);
    /** 擦除地址范围；非空必填，后端校验擦除对齐。 */
    aStatus_t (*erase)(void *context, uint32_t address, uint32_t size,
                       aTimeout_t timeout);
} aDataBaseStorage_t;

/**
 * @brief 复制存储操作表并绑定全局 FAL 后端。
 * @param[in] storage 操作表，本次调用后可释放；context 及回调依赖对象必须持续有效。
 * @retval A_STATUS_OK 绑定成功。
 * @retval A_STATUS_INVALID_PARAM 操作表/回调缺失，或几何参数不匹配编译期 FAL 布局。
 * @retval A_STATUS_BUSY 已存在绑定，不覆盖原后端。
 * @note 当前要求容量和擦除块大小与布局完全一致，不接受仅容量大于布局的配置。
 */
aStatus_t aDataBaseBindStorage(const aDataBaseStorage_t *storage);

/**
 * @brief 清除全局后端绑定，不销毁实际存储设备。
 * @retval A_STATUS_OK 已解绑。
 * @retval A_STATUS_NOT_READY 尚未绑定。
 * @warning 调用前必须关闭所有数据库并停止在途存储回调。
 */
aStatus_t aDataBaseUnbindStorage(void);

#endif
