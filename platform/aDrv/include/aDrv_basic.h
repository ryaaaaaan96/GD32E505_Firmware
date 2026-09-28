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

typedef uint16_t aDrvGpioPin_t;
typedef uint8_t aDrvDmaChannel_t;

#define ADRV_PIN(port_, pin_)                                                 \
    ((aDrvGpioPin_t)(((uint16_t)(port_) * 16U) + (uint16_t)(pin_)))
#define ADRV_PIN_NONE         ((aDrvGpioPin_t)0xFFFFU)
#define ADRV_PINNULL          ADRV_PIN_NONE
#define ADRV_DMA_CHANNEL_NONE ((aDrvDmaChannel_t)0xFFU)

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
