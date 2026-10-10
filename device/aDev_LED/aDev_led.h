/**
 * @file aDev_led.h
 * @brief GPIO 输出型 LED 的硬件无关逻辑接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 配置有效电平后，业务使用亮/灭语义，不直接操作 GPIO。
 * 静态或动态分配由编译配置选择；闪烁周期由 APP 管理。
 * 无内部锁、任务或延时；同一实例的访问与生命周期必须外部串行化。
 * 初始化/销毁在任务或启动阶段调用；亮灭操作可在已串行化的 ISR 中调用。
 */

#ifndef ADEV_LED_H
#define ADEV_LED_H

#include "aDrv_gpio.h"
#include "aLib.h"

#ifndef ADEV_LED_STATIC_ENABLE
#define ADEV_LED_STATIC_ENABLE 0
#endif
#ifndef ADEV_LED_DYNAMIC_ENABLE
#define ADEV_LED_DYNAMIC_ENABLE 0
#endif

/** @brief LED 的电气有效电平；业务始终用逻辑亮/灭操作。 */
typedef enum {
    ADEV_LED_ACTIVE_LOW,
    ADEV_LED_ACTIVE_HIGH,
} aDevLedActiveLevel_t;

/** @brief 初始化时复制配置；调用结束后配置对象可释放。 */
typedef struct {
    aDrvGpioPin_t pin; /**< 应用选择的输出引脚。 */
    aDevLedActiveLevel_t active_level; /**< 有效电平，决定亮灭到电平的映射。 */
    aDrvGpioSpeed_t speed; /**< GPIO 输出速度，默认 HIGH。 */
    aBool_t initially_on; /**< 初始化完成时是否点亮。 */
} aDevLedConfig_t;

/** @brief 不透明设备句柄；静态分配方包含 aDev_led_instance.h。 */
typedef struct aDevLedHandle aDevLedHandle_t;

/**
 * @brief 设置默认 LED 配置。
 * @param[out] config 配置对象；NULL 不操作。
 * @note 默认无引脚、高电平点亮、初始熄灭；初始化前必须设置 pin。
 */
void aDevLedConfigStructInit(aDevLedConfig_t *config);

/**
 * @brief 清空未使用的静态 LED 句柄，不访问硬件。
 * @param[out] handle 调用方存储；NULL 不操作。
 * @warning 不得用于活动句柄或 Create 返回的句柄；不代替 DeInit/Destroy。
 */
void aDevLedHandleStructInit(aDevLedHandle_t *handle);

#if ADEV_LED_STATIC_ENABLE
/**
 * @brief 在调用方存储中初始化推挽输出并设置初始亮灭状态，不分配内存。
 * @param[in] config 本次调用期间有效的板级配置。
 * @param[out] handle 调用方句柄；后续操作期间保持有效。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 空指针、无效引脚或有效电平。
 * @return 也可能返回底层 GPIO 初始化错误。
 * @warning 仅接受未使用或已 DeInit 的静态存储；活动句柄不得重复初始化。
 * 首次调用无需预先清零；失败后须修正配置并重新 InitStatic。
 */
aStatus_t aDevLedInitStatic(const aDevLedConfig_t *config,
                           aDevLedHandle_t *handle);
#endif

#if ADEV_LED_DYNAMIC_ENABLE
/**
 * @brief 通过 aOS 分配并初始化 LED 句柄。
 * @param[in] config 本次调用期间有效的板级配置。
 * @param[out] handle_out 必填；成功返回独占句柄，失败置 NULL。
 * @return OK、INVALID_PARAM、NO_MEMORY 或底层 GPIO 错误。
 * @note 成功后须由 Destroy 释放；创建失败自动回收分配的存储。
 */
aStatus_t aDevLedCreate(const aDevLedConfig_t *config,
                       aDevLedHandle_t **handle_out);

/**
 * @brief 反初始化并释放 Create 返回的句柄。
 * @param[in,out] handle 动态句柄，允许已 DeInit；成功后指针失效。
 * @return OK、INVALID_PARAM（NULL 或静态对象）或底层 GPIO 错误。
 * @note 反初始化失败保留句柄，调用方可处理错误后重试；禁止在 ISR 调用。
 */
aStatus_t aDevLedDestroy(aDevLedHandle_t *handle);
#endif

/**
 * @brief 先设置熄灭电平，再将引脚恢复为浮空输入，不释放句柄存储。
 * @param[in,out] handle 已初始化的静态或动态句柄。
 * @return OK、INVALID_PARAM、NOT_READY 或底层 GPIO 错误。
 * @note 释放引脚后的实际电平由板级电路决定，不保证一直熄灭。
 * 成功后静态对象可重新 InitStatic，动态对象仍须 Destroy。
 */
aStatus_t aDevLedDeInit(aDevLedHandle_t *handle);

/**
 * @brief 设置 LED 的逻辑亮灭状态。
 * @param[in,out] handle 已初始化的 LED。
 * @param[in] on A_TRUE 点亮，A_FALSE 熄灭。
 * @retval A_STATUS_OK 已写输出电平。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDevLedSet(aDevLedHandle_t *handle, aBool_t on);

/**
 * @brief 点亮 LED，相当于 Set(handle, A_TRUE)。
 * @param[in,out] handle 已初始化的 LED。
 * @return aDevLedSet() 的状态，不等待、不分配内存。
 */
aStatus_t aDevLedOn(aDevLedHandle_t *handle);

/**
 * @brief 熄灭 LED，相当于 Set(handle, A_FALSE)。
 * @param[in,out] handle 已初始化的 LED。
 * @return aDevLedSet() 的状态，不等待、不分配内存。
 */
aStatus_t aDevLedOff(aDevLedHandle_t *handle);

/**
 * @brief 根据输出锁存电平翻转逻辑亮灭状态。
 * @param[in,out] handle 已初始化的 LED。
 * @return GPIO 读/写状态；成功为 A_STATUS_OK。
 * @warning 非原子读改写，不与其他写入者并发调用。
 */
aStatus_t aDevLedToggle(aDevLedHandle_t *handle);

/**
 * @brief 按输出锁存电平读取设置的 LED 逻辑状态。
 * @param[in] handle 已初始化的 LED。
 * @param[out] on 成功时返回 A_TRUE（亮）或 A_FALSE（灭）。
 * @retval A_STATUS_OK 已读取。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 不检测实际引脚电平或发光状态，不另存软件状态缓存。
 */
aStatus_t aDevLedGet(const aDevLedHandle_t *handle, aBool_t *on);

#endif
