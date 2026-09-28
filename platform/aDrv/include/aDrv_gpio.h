/**
 * @file aDrv_gpio.h
 * @brief Gpio 非阻塞硬件操作接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 配置和生命周期由上层管理，同一实例的操作由调用者串行化。
 * 句柄字段是驱动维护状态，初始化后禁止上层修改；本层不依赖 aOS。
 * 读取返回引脚输入电平，不是输出锁存器；Toggle 为读后写，不保证原子性。
 */

#ifndef ADRV_GPIO_H
#define ADRV_GPIO_H

#include "aDrv_basic.h"

/** @brief GPIO 模式；当前输入为浮空，不包含上拉/下拉配置。 */
typedef enum {
    ADRV_GPIO_INPUT,
    ADRV_GPIO_OUTPUT_PUSH_PULL,
    ADRV_GPIO_OUTPUT_OPEN_DRAIN,
    ADRV_GPIO_ALTERNATE_PUSH_PULL,
    ADRV_GPIO_ALTERNATE_OPEN_DRAIN,
    ADRV_GPIO_ANALOG,
} aDrvGpioMode_t;

/** @brief GPIO 物理电平，不包含 LED/DE 等设备有效电平语义。 */
typedef enum {
    ADRV_GPIO_LOW = 0,
    ADRV_GPIO_HIGH = 1,
} aDrvGpioLevel_t;

/** @brief 板级 GPIO 配置，初始化期间读取。 */
typedef struct {
    aDrvGpioPin_t pin; /**< ADRV_PIN 编码，不能为 NONE。 */
    aDrvGpioMode_t mode; /**< 引脚模式。 */
    aDrvGpioLevel_t initial_level; /**< 普通输出模式切换前预置的电平。 */
} aDrvGpioConfig_t;

/** @brief 驱动拥有的状态；调用者提供存储但不直接改字段。 */
typedef struct {
    aDrvGpioPin_t pin; /**< 已配置的逻辑引脚。 */
    aBool_t initialized; /**< 底层初始化状态的唯一来源。 */
} aDrvGpioHandle_t;

/**
 * @brief 填充默认配置，不访问硬件。
 * @param[out] config 配置对象；NULL 时不操作。
 * @note 引脚/通道等板级资源需在初始化前由调用者补齐。
 */
void aDrvGpioConfigStructInit(aDrvGpioConfig_t *config);

/**
 * @brief 将句柄重置为未初始化状态。
 * @param[out] handle 调用方存储；NULL 时不操作。
 * @warning 仅用于未使用的句柄；不是反初始化函数，不能清除活动资源的所有权。
 */
void aDrvGpioHandleStructInit(aDrvGpioHandle_t *handle);

/**
 * @brief 配置 GPIO 并保存调用方句柄。
 * @param[in] config 引脚、模式与初始电平；仅调用期间读取。
 * @param[out] handle 调用方持有的句柄。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 指针、引脚或模式无效。
 * @note 当前输入模式为浮空输入；不自动检查其他设备是否占用该引脚。
 */
aStatus_t aDrvGpioInit(const aDrvGpioConfig_t *config,
                       aDrvGpioHandle_t *handle);

/**
 * @brief 将引脚恢复为浮空输入并清空句柄。
 * @param[in,out] handle 已初始化句柄。
 * @retval A_STATUS_OK 已反初始化。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvGpioDeInit(aDrvGpioHandle_t *handle);

/**
 * @brief 直接设置输出电平，不等待。
 * @param[in] handle 已配置为输出的 GPIO 句柄。
 * @param[in] level ADRV_GPIO_LOW 或 ADRV_GPIO_HIGH。
 * @retval A_STATUS_OK 已写输出寄存器。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvGpioWrite(const aDrvGpioHandle_t *handle,
                        aDrvGpioLevel_t level);

/**
 * @brief 读取引脚实际输入电平。
 * @param[in] handle 已初始化的 GPIO 句柄。
 * @param[out] level 成功时返回电平，不得为 NULL。
 * @retval A_STATUS_OK 读取成功。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvGpioRead(const aDrvGpioHandle_t *handle,
                       aDrvGpioLevel_t *level);

/**
 * @brief 按输入电平执行一次取反输出。
 * @param[in] handle 已配置为输出的 GPIO 句柄。
 * @retval A_STATUS_OK 已写入相反电平。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @warning 读写不是原子操作；同一引脚的并发访问必须外部串行化。
 */
aStatus_t aDrvGpioToggle(const aDrvGpioHandle_t *handle);

#endif
