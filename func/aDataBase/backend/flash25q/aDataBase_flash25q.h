/**
 * @file aDataBase_flash25q.h
 * @brief Flash25Q 到通用数据库存储接口的可选适配器。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 仅当选择 FLASH25Q 后端时使用；数据库核心仍只依赖通用存储操作表。
 */

#ifndef ADATABASE_FLASH25Q_H
#define ADATABASE_FLASH25Q_H
#include "aDataBase.h"
#include "aDev_flash25q.h"

/**
 * @brief 将已初始化 Flash25Q 句柄绑定为数据库存储。
 * @param[in] handle 应用持有的 Flash 句柄，必须保持有效至解绑完成。
 * @return 句柄校验或 aDataBaseBindStorage() 的状态；成功为 A_STATUS_OK。
 * @note 不接管句柄所有权，不初始化设备；生命周期由应用串行化。
 */
aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle);
#endif
