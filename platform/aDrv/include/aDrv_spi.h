/**
 * @file aDrv_spi.h
 * @brief Spi 非阻塞硬件操作接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 配置和生命周期由上层管理，同一实例的操作由调用者串行化。
 * 句柄字段是驱动维护状态，初始化后禁止上层修改；本层不依赖 aOS。
 * TryRead/TryWrite 每次仅处理一帧，不轮询等待就绪，不自动控制事务片选。
 */

#ifndef ADRV_SPI_H
#define ADRV_SPI_H

#include "aDrv_gpio.h"

/** @brief 逻辑实例；编号与 GD32 SPI0/1/2 一致。 */
typedef enum {
    ADRV_SPI_0 = 0,
    ADRV_SPI_1 = 1,
    ADRV_SPI_2 = 2,
} aDrvSpiId_t;

/** @brief SPI 从机/主机角色；引脚适配能力以芯片 port 为准。 */
typedef enum {
    ADRV_SPI_MODE_SLAVE,
    ADRV_SPI_MODE_MASTER,
} aDrvSpiMode_t;

/** @brief 时钟空闲电平。 */
typedef enum {
    ADRV_SPI_POLARITY_LOW,
    ADRV_SPI_POLARITY_HIGH,
} aDrvSpiClockPolarity_t;

/** @brief 相对于空闲电平的第一/第二时钟边沿采样。 */
typedef enum {
    ADRV_SPI_PHASE_1EDGE,
    ADRV_SPI_PHASE_2EDGE,
} aDrvSpiClockPhase_t;

/** @brief 片选控制方式；硬件模式的具体限制见 config.csMode。 */
typedef enum {
    ADRV_SPI_CS_SOFT,
    ADRV_SPI_CS_HARD_INPUT,
    ADRV_SPI_CS_HARD_OUTPUT,
} aDrvSpiCsMode_t;

/** @brief 数据帧内部的发送位序，不表示 CPU 内存字节序。 */
typedef enum {
    ADRV_SPI_BITORDER_MSB,
    ADRV_SPI_BITORDER_LSB,
} aDrvSpiBitOrder_t;

/** @brief aDrvSpiConfig_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    aDrvSpiId_t spiId; /**< 实例编号与 SPI0/1/2 一致。 */
    aDrvSpiMode_t mode; /**< 当前仅支持 MASTER，SLAVE 返回 UNSUPPORTED。 */
    aDrvSpiClockPolarity_t polarity; /**< 时钟空闲极性。 */
    aDrvSpiClockPhase_t phase; /**< 第一或第二边沿采样。 */
    aDrvSpiCsMode_t csMode; /**< 当前仅支持 SOFT，HARD 返回 UNSUPPORTED。 */
    aDrvSpiBitOrder_t bitOrder; /**< 高位或低位先传输。 */
    uint32_t prescaler; /**< 请求分频比，当前 port 向下映射到 2..256 的二次幂档位。 */
    uint8_t dataBits; /**< 每帧 8 或 16 位；TryRead/TryWrite 每次一帧。 */
    aDrvGpioPin_t sckPin; /**< 串行时钟引脚，必须配置。 */
    aDrvGpioPin_t mosiPin; /**< 主发从收引脚，必须配置。 */
    aDrvGpioPin_t misoPin; /**< 主收从发引脚，必须配置。 */
    aDrvGpioPin_t csPin; /**< 软件片选 GPIO；NONE 表示由上层自行管理 CS。 */
} aDrvSpiConfig_t;

/** @brief aDrvSpiHandle_t 驱动/设备状态；调用方提供存储，字段仅由所属模块维护。 */
typedef struct {
    uintptr_t instance; /**< 内部寄存器基址。 */
    aDrvSpiId_t spiId; /**< 已绑定的逻辑实例。 */
    aDrvGpioHandle_t csGpio; /**< 软件片选 GPIO 句柄。 */
    uint8_t dataBytes; /**< 每帧占用 1 或 2 个内存字节。 */
    aBool_t softwareCs; /**< 是否使用 GPIO 软件片选。 */
    aBool_t initialized; /**< 底层实例是否已配置。 */
} aDrvSpiHandle_t;

/**
 * @brief 填充默认配置，不访问硬件。
 * @param[out] config 配置对象；NULL 时不操作。
 * @note 引脚/通道等板级资源需在初始化前由调用者补齐。
 */
void aDrvSpiConfigStructInit(aDrvSpiConfig_t *config);

/**
 * @brief 将句柄重置为未初始化状态。
 * @param[out] handle 调用方存储；NULL 时不操作。
 * @warning 仅用于未使用的句柄；不是反初始化函数，不能清除活动资源的所有权。
 */
void aDrvSpiHandleStructInit(aDrvSpiHandle_t *handle);

/**
 * @brief 配置 SPI 引脚与控制器。
 * @param[in] config 初始化配置，仅调用期间读取。
 * @param[out] handle 调用方分配的句柄。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 参数、引脚或枚举无效。
 * @retval A_STATUS_UNSUPPORTED 当前后端未实现从机或硬件片选。
 * @return 也可能返回 GPIO 初始化错误。
 * @note 软件片选初始化为高电平；本接口不提供总线仲裁。
 */
aStatus_t aDrvSpiInitStatic(const aDrvSpiConfig_t *config,
                            aDrvSpiHandle_t *handle);

/**
 * @brief 关闭 SPI 并释放软件片选 GPIO。
 * @param[in,out] handle 已初始化句柄，调用前停止所有传输。
 * @retval A_STATUS_OK 已反初始化。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvSpiDeInitStatic(aDrvSpiHandle_t *handle);

/**
 * @brief 尝试写入一个 SPI 数据帧。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] data 8 位模式指向 uint8_t；16 位模式指向对齐的 uint16_t。
 * @retval A_STATUS_OK 已写入数据寄存器，不代表线路传输完成。
 * @retval A_STATUS_BUSY 发送寄存器未就绪，未写入。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvSpiTryWrite(aDrvSpiHandle_t *handle, const void *data);

/**
 * @brief 尝试读取一个 SPI 数据帧。
 * @param[in,out] handle 已初始化句柄。
 * @param[out] data 8 位模式指向 uint8_t；16 位模式指向对齐的 uint16_t。
 * @retval A_STATUS_OK 已读取一帧。
 * @retval A_STATUS_BUSY 尚无数据，输出不变。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvSpiTryRead(aDrvSpiHandle_t *handle, void *data);

/**
 * @brief 设置软件片选引脚的物理电平。
 * @param[in,out] handle 已初始化的软件片选 SPI 句柄。
 * @param[in] state 0 为低电平，非 0 为高电平；不是逻辑选中标志。
 * @retval A_STATUS_OK 已设置片选。
 * @retval A_STATUS_UNSUPPORTED 当前不是软件片选模式。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvSpiCsControl(aDrvSpiHandle_t *handle, uint8_t state);

/** 查询线路完成，同时检查配置错误和接收溢出。 */
aStatus_t aDrvSpiIsComplete(aDrvSpiHandle_t *handle, aBool_t *complete);

/** 停止控制器；失败事务后必须 DeInit/Init 才能继续使用。 */
aStatus_t aDrvSpiAbort(aDrvSpiHandle_t *handle);

#endif
