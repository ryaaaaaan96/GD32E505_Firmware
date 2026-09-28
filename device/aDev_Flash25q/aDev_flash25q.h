/**
 * @file aDev_flash25q.h
 * @brief 基于 QSPI 的 25Q Flash 同步设备接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 仅任务上下文调用；内部 mutex 串行化一次完整操作，锁等待和硬件等待共用总预算。
 * Init/DeInit 必须由调用方与所有访问者互斥，禁止并发销毁或重复初始化活动句柄。
 * 读写采用 24 位地址，容量不超过 16 MiB，编程页 256 字节、擦除扇区 4096 字节。
 * 超时不撤销已经提交的 Flash 命令；写入/擦除可能部分完成，接口不返回部分长度。
 * NO_WAIT 不等于无副作用探测；获得锁后仍可能发出命令。
 */

#ifndef ADEV_FLASH25Q_H
#define ADEV_FLASH25Q_H

#include "aDrv_qspi.h"
#include "aLib.h"

#define ADEV_FLASH_IOCTL_QSPI_FAST_READ 0x01U

/** @brief aDevFlash25qConfig_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    aDrvQspiConfig_t drv_config; /**< 板级 QSPI 引脚和时钟配置。 */
    uint32_t capacity; /**< 配置容量，单位字节，最大 16 MiB；不是自动探测值。 */
} aDevFlash25qConfig_t;

/** @brief aDevFlash25qHandle_t 驱动/设备状态；调用方提供存储，字段仅由所属模块维护。 */
typedef struct {
    aDrvQspiHandle_t qspi;
    void *operation_mutex;
    uint32_t size;
    aBool_t initialized;
    aBool_t fast_read;
} aDevFlash25qHandle_t;

/**
 * @brief 填充默认配置与 QSPI 子配置。
 * @param[out] config 配置对象；NULL 不操作。
 * @note 默认容量 16 MiB；必须按板级连接补齐 QSPI 引脚。
 */
void aDevFlash25qConfigStructInit(aDevFlash25qConfig_t *config);

/**
 * @brief 清空未使用的 Flash 句柄。
 * @param[out] handle 调用方存储；NULL 不操作。
 * @warning 不释放内部 mutex，不能代替 DeInit。
 */
void aDevFlash25qHandleStructInit(aDevFlash25qHandle_t *handle);

/**
 * @brief 初始化 QSPI 并创建操作锁。
 * @param[in] config 初始化期间有效的配置；capacity 为 1..16 MiB。
 * @param[out] handle 调用方句柄，持续有效至 DeInit。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 指针、容量或底层配置无效。
 * @retval A_STATUS_NO_MEMORY 无法创建操作锁。
 * @return 也可能返回底层初始化错误；不读取 JEDEC ID 校验实际容量。
 */
aStatus_t aDevFlash25qInit(const aDevFlash25qConfig_t *config,
                              aDevFlash25qHandle_t *handle);

/**
 * @brief 反初始化 QSPI 并释放操作锁。
 * @param[in,out] handle 已初始化且无并发用户的句柄。
 * @retval A_STATUS_OK 已反初始化。
 * @retval A_STATUS_BUSY 无法立即取得操作锁。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 即使能取得锁，也必须保证其他任务不会再进入 API。
 */
aStatus_t aDevFlash25qDeInit(aDevFlash25qHandle_t *handle);

/**
 * @brief 从 Flash 指定地址同步读取字节。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] address Flash 内的字节偏移。
 * @param[out] data 至少 size 字节的目标数组，持续有效至返回。
 * @param[in] size 大于 0，不得超出配置容量。
 * @param[in] timeout 包含加锁和命令等待的总预算。
 * @retval A_STATUS_OK 整个操作完成。
 * @retval A_STATUS_INVALID_PARAM 指针、范围、长度或超时无效。
 * @retval A_STATUS_NOT_READY 设备未初始化或调度器不支持当前等待。
 * @retval A_STATUS_BUSY NO_WAIT 时无法获得操作锁。
 * @retval A_STATUS_TIMEOUT 等待硬件或总预算到期。
 * @return 也可能透传 aDrv 错误；不设置 aOS errno。
 * @note 映射窗口复制本身没有逐字节超时检查，不承诺硬实时截止。
 */
aStatus_t aDevFlash25qRead(aDevFlash25qHandle_t *handle, uint32_t address,
                           uint8_t *data, uint32_t size,
                           aTimeout_t timeout);

/**
 * @brief 按页拆分并同步编程，不自动擦除。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] address Flash 字节偏移，无需页对齐。
 * @param[in] data 至少 size 字节的源数组，返回前保持不变。
 * @param[in] size 大于 0，不得超出配置容量。
 * @param[in] timeout 所有页编程和加锁共享的总预算。
 * @retval A_STATUS_OK 整个操作完成。
 * @retval A_STATUS_INVALID_PARAM 指针、范围、长度或超时无效。
 * @retval A_STATUS_NOT_READY 设备未初始化或调度器不支持当前等待。
 * @retval A_STATUS_BUSY NO_WAIT 时无法获得操作锁。
 * @retval A_STATUS_TIMEOUT 等待硬件或总预算到期。
 * @return 也可能透传 aDrv 错误；不设置 aOS errno。
 * @warning 调用方须先擦除目标；失败不回滚已编程页面。
 */
aStatus_t aDevFlash25qWrite(aDevFlash25qHandle_t *handle, uint32_t address,
                            const uint8_t *data, uint32_t size,
                            aTimeout_t timeout);

/**
 * @brief 按 4096 字节扇区擦除指定区域。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] address 4096 字节对齐的起始偏移。
 * @param[in] size 非零且为 4096 的倍数，区域不得越界。
 * @param[in] timeout 所有扇区和加锁共享的总预算。
 * @retval A_STATUS_OK 整个操作完成。
 * @retval A_STATUS_INVALID_PARAM 指针、范围、长度或超时无效。
 * @retval A_STATUS_NOT_READY 设备未初始化或调度器不支持当前等待。
 * @retval A_STATUS_BUSY NO_WAIT 时无法获得操作锁。
 * @retval A_STATUS_TIMEOUT 等待硬件或总预算到期。
 * @return 也可能透传 aDrv 错误；不设置 aOS errno。
 * @warning 擦除不可回滚；超时后芯片可能仍在执行最后提交的命令。
 */
aStatus_t aDevFlash25qErase(aDevFlash25qHandle_t *handle, uint32_t address,
                            uint32_t size, aTimeout_t timeout);

/**
 * @brief 擦除整颗 Flash 并等待就绪。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] timeout 总等待预算，应覆盖整片擦除时间。
 * @retval A_STATUS_OK 整个操作完成。
 * @retval A_STATUS_INVALID_PARAM 指针、范围、长度或超时无效。
 * @retval A_STATUS_NOT_READY 设备未初始化或调度器不支持当前等待。
 * @retval A_STATUS_BUSY NO_WAIT 时无法获得操作锁。
 * @retval A_STATUS_TIMEOUT 等待硬件或总预算到期。
 * @return 也可能透传 aDrv 错误；不设置 aOS errno。
 * @warning 删除整颗芯片数据；超时不能取消芯片内部擦除。
 */
aStatus_t aDevFlash25qChipErase(aDevFlash25qHandle_t *handle,
                                aTimeout_t timeout);

/**
 * @brief 查询句柄保存的配置容量。
 * @param[in] handle Flash 句柄。
 * @return 字节数；NULL 返回 0。不探测实际芯片容量。
 */
uint32_t aDevFlash25qGetSize(const aDevFlash25qHandle_t *handle);

/**
 * @brief 检查句柄非空且已初始化。
 * @param[in] handle 待检查的句柄。
 * @retval A_STATUS_OK initialized 标志已设置。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 不验证硬件连通性或 Flash ID。
 */
aStatus_t aDevFlash25qHandleIsValid(const aDevFlash25qHandle_t *handle);

/**
 * @brief 切换普通读/快速读命令。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] command 仅支持 ADEV_FLASH_IOCTL_QSPI_FAST_READ。
 * @param[in] argument 按指针数值解释：NULL 关闭，非零开启；不解引用。
 * @retval A_STATUS_OK 已更新后续读取模式。
 * @retval A_STATUS_BUSY 当前操作锁被占用，不等待。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDevFlash25qIoCtl(aDevFlash25qHandle_t *handle, uint32_t command,
                               void *argument);

#endif
