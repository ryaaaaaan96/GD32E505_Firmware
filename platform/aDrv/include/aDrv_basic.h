/**
 * @file aDrv_basic.h
 * @brief 芯片信息及逻辑引脚/通道标识。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 引脚编号为逻辑端口号乘以 16 加引脚号；不是寄存器地址。
 * 宏只编码，不校验芯片封装是否引出该引脚。NONE 表示未配置资源。
 */

#ifndef ADRV_BASIC_H
#define ADRV_BASIC_H

#include "aDrv.h"

/** @brief 编码后的逻辑 GPIO 引脚，不是位掩码或寄存器地址。 */
typedef uint16_t aDrvGpioPin_t;
/** @brief 由芯片 port 解释的逻辑 DMA 通道标识。 */
typedef uint8_t aDrvDmaChannel_t;

/** @brief 编码逻辑端口及 0..15 引脚号；参数范围由调用方保证。 */
#define ADRV_PIN(port_, pin_)                                                 \
    ((aDrvGpioPin_t)(((uint16_t)(port_) * 16U) + (uint16_t)(pin_)))
/** @brief 未选择引脚的哨兵值，不能直接用于硬件初始化。 */
#define ADRV_PIN_NONE         ((aDrvGpioPin_t)0xFFFFU)
/** @brief 现存的未选择引脚别名，新代码使用 ADRV_PIN_NONE。 */
#define ADRV_PINNULL          ADRV_PIN_NONE
/** @brief 未选择 DMA 通道的哨兵值。 */
#define ADRV_DMA_CHANNEL_NONE ((aDrvDmaChannel_t)0xFFU)

/** @brief 逻辑 GPIO 端口；枚举存在不代表当前封装有对应引脚。 */
typedef enum {
    ADRV_GPIO_PORT_A,
    ADRV_GPIO_PORT_B,
    ADRV_GPIO_PORT_C,
    ADRV_GPIO_PORT_D,
    ADRV_GPIO_PORT_E,
    ADRV_GPIO_PORT_F,
    ADRV_GPIO_PORT_G,
} aDrvGpioPort_t;

/**
 * @brief 读取芯片调试标识。
 * @return 当前 GD32 port 返回完整 DBG_ID 寄存器，不单独提取型号字段。
 */
uint32_t aDrvGetChipId(void);

/**
 * @brief 读取芯片修订信息原始值。
 * @return 当前 GD32 port 同样返回完整 DBG_ID；不是已拆分的修订编号。
 */
uint32_t aDrvGetRevisionId(void);

/**
 * @brief 根据当前时钟寄存器更新并获取内核时钟。
 * @return 内核频率，单位 Hz。
 * @note 会更新 SystemCoreClock；不改变硬件时钟配置。
 */
uint32_t aDrvGetCoreClockHz(void);

/** 开启自由运行的内核周期计数器，不清零已有计数；不支持则返回错误。
 * 用于短间隔测量，计数自然回绕；测量期间内核时钟必须保持不变。
 * 休眠或调试暂停可能停止计数，不能用作墙上时钟。 */
aStatus_t aDrvCycleCounterEnable(void);
uint32_t aDrvCycleCounterRead(void);

/**
 * @brief 读取芯片出厂 Flash 容量信息。
 * @return Flash 容量，单位 KiB；不是字节数。
 */
uint16_t aDrvGetFlashSize(void);

/**
 * @brief 复制芯片 96 位唯一标识。
 * @param[out] uid 至少 12 字节的可写存储，按硬件地址顺序填充。
 * @retval A_STATUS_OK 读取成功。
 * @retval A_STATUS_INVALID_PARAM uid 为 NULL。
 */
aStatus_t aDrvGetUniqueId(uint8_t uid[12]);

#endif
