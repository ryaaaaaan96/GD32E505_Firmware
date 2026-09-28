/**
 * @file aDrv_qspi.h
 * @brief QSPI/SQPI 命令配置与数据访问接口。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 同一控制器的 Command 与数据阶段必须由上层串行化，不能交错不同事务。
 * 当前 GD32 port 经映射窗口逐字节访问数据，不是 DMA，也不提供超时。
 * 这些访问会占用调用者执行时间；不能把本接口理解为异步提交或恒定耗时。
 */

#ifndef ADRV_QSPI_H
#define ADRV_QSPI_H

#include "aDrv_gpio.h"

#define ADRV_QSPI_FMODE_INDIRECT_WRITE 0U
#define ADRV_QSPI_FMODE_INDIRECT_READ  1U

#define ADRV_QSPI_INST_NONE    0U
#define ADRV_QSPI_INST_1_LINE  1U
#define ADRV_QSPI_INST_2_LINES 2U
#define ADRV_QSPI_INST_4_LINES 4U

#define ADRV_QSPI_ADDR_NONE    0U
#define ADRV_QSPI_ADDR_1_LINE  1U
#define ADRV_QSPI_ADDR_2_LINES 2U
#define ADRV_QSPI_ADDR_4_LINES 4U

#define ADRV_QSPI_DATA_NONE    0U
#define ADRV_QSPI_DATA_1_LINE  1U
#define ADRV_QSPI_DATA_2_LINES 2U
#define ADRV_QSPI_DATA_4_LINES 4U

typedef enum {
    ADRV_QSPI_1,
} aDrvQspiId_t;

/** @brief aDrvQspiConfig_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    aDrvQspiId_t qspiId; /**< 逻辑 SQPI 实例，当前仅支持实例 1。 */
    uint32_t clockPrescaler; /**< 当前 GD32 clk_div 字段，范围 0..63，不是期望频率。 */
    uint32_t flashSize; /**< 保留的容量/地址字段，当前 GD32 Init 不使用。 */
    aDrvGpioPin_t clkPin; /**< SQPI 时钟引脚。 */
    aDrvGpioPin_t csPin; /**< SQPI 片选引脚。 */
    aDrvGpioPin_t io0Pin; /**< 数据线 0；当前 Init 要求全部 IO 引脚有效。 */
    aDrvGpioPin_t io1Pin; /**< 数据线 1。 */
    aDrvGpioPin_t io2Pin; /**< 数据线 2。 */
    aDrvGpioPin_t io3Pin; /**< 数据线 3。 */
} aDrvQspiConfig_t;

/** @brief aDrvQspiHandle_t 驱动/设备状态；调用方提供存储，字段仅由所属模块维护。 */
typedef struct {
    uintptr_t instance;
    aDrvQspiId_t qspiId;
    aDrvGpioPin_t csPin;
    uint32_t address;
    uint32_t transferLength;
    uint8_t functionalMode;
    aBool_t initialized;
} aDrvQspiHandle_t;

/** @brief aDrvQspiCmd_t 配置描述；初始化/注册时读取，借用对象的生命周期见对应接口。 */
typedef struct {
    uint32_t Instruction; /**< 指令码，通常为 8 位 Flash 命令。 */
    uint32_t InstructionMode; /**< ADRV_QSPI_INST_* 指令线数。 */
    uint32_t Address; /**< Flash 字节地址；无地址阶段时必须为 0。 */
    uint32_t AddressSize; /**< 地址位数，当前允许 0..31，常用 24。 */
    uint32_t AddressMode; /**< ADRV_QSPI_ADDR_* 地址线数。 */
    uint32_t DataMode; /**< ADRV_QSPI_DATA_* 数据线数。 */
    uint32_t NbData; /**< 数据阶段字节数，0 不限制后续数据调用长度。 */
    uint32_t DummyCycles; /**< 空周期数，0..15。 */
    uint32_t FunctionalMode; /**< INDIRECT_READ 或 INDIRECT_WRITE。 */
} aDrvQspiCmd_t;

/**
 * @brief 填充默认 QSPI 配置。
 * @param[out] config 配置对象；NULL 不操作。
 * @note 默认实例 1，时钟字段 20，flashSize 字段 24；所有引脚均需补齐。
 */
void aDrvQspiConfigStructInit(aDrvQspiConfig_t *config);

/**
 * @brief 清空尚未使用的 QSPI 句柄。
 * @param[out] handle 调用方存储；NULL 不操作。
 * @warning 不得用于替代活动控制器的 DeInit。
 */
void aDrvQspiHandleStructInit(aDrvQspiHandle_t *handle);

/**
 * @brief 配置引脚与 SQPI 控制器。
 * @param[in] config 调用期间有效的配置；六个引脚均需有效。
 * @param[out] handle 调用方持有的稳定句柄。
 * @retval A_STATUS_OK 初始化成功。
 * @retval A_STATUS_INVALID_PARAM 指针、实例、引脚或时钟字段无效。
 * @note 当前 flashSize 字段不参与硬件初始化；不自动探测 Flash 容量。
 */
aStatus_t aDrvQspiInitStatic(const aDrvQspiConfig_t *config,
                             aDrvQspiHandle_t *handle);

/**
 * @brief 复位并关闭 SQPI 时钟，清空句柄。
 * @param[in,out] handle 已初始化句柄；调用前确保没有事务进行。
 * @retval A_STATUS_OK 已关闭。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvQspiDeInitStatic(aDrvQspiHandle_t *handle);

/**
 * @brief 配置读写命令，必要时触发无数据命令。
 * @param[in,out] handle 已初始化句柄。
 * @param[in] command 命令描述；仅本次调用读取。
 * @retval A_STATUS_OK 已配置/提交，不代表 Flash 编程或擦除完成。
 * @retval A_STATUS_BUSY 无地址单命令仍在进行。
 * @retval A_STATUS_UNSUPPORTED 功能模式或地址长度组合不支持。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvQspiCommand(aDrvQspiHandle_t *handle,
                          const aDrvQspiCmd_t *command);

/**
 * @brief 查询 SQPI 单命令触发位是否清除。
 * @param[in] handle 已初始化句柄。
 * @param[out] complete 成功时返回完成标志，不得为 NULL。
 * @retval A_STATUS_OK 已获取状态。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 * @note 这不是 Flash 内部 WIP 状态，不能替代芯片就绪轮询。
 */
aStatus_t aDrvQspiIsCommandComplete(const aDrvQspiHandle_t *handle,
                                    aBool_t *complete);

/**
 * @brief 按上一次 Command 的地址同步写映射窗口。
 * @param[in,out] handle 已配置 INDIRECT_WRITE 命令的句柄。
 * @param[in] data 源字节数组，不得为 NULL，返回后不再访问。
 * @param[in] length 字节数；命令 NbData 非零时不得超出它。
 * @retval A_STATUS_OK 数据已写窗口；不代表 Flash 内部操作完成。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvQspiTransmit(aDrvQspiHandle_t *handle, const uint8_t *data,
                           uint32_t length);

/**
 * @brief 按上一次 Command 的地址同步读映射窗口。
 * @param[in,out] handle 已配置 INDIRECT_READ 命令的句柄。
 * @param[out] data 至少 length 字节的可写数组，不得为 NULL。
 * @param[in] length 字节数；命令 NbData 非零时不得超出它。
 * @retval A_STATUS_OK 已复制全部数据。
 * @retval A_STATUS_INVALID_PARAM 空指针或参数无效。
 * @retval A_STATUS_NOT_READY 句柄尚未初始化。
 */
aStatus_t aDrvQspiReceive(aDrvQspiHandle_t *handle, uint8_t *data,
                          uint32_t length);

#endif
