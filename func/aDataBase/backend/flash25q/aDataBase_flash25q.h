#ifndef ADATABASE_FLASH25Q_H
#define ADATABASE_FLASH25Q_H

#include "aDataBase.h"
#include "aDev_flash25q.h"

/** 将已初始化的 Flash 句柄绑定为数据库介质，不接管所有权。
 * 先关闭全部数据库，再解绑存储，最后才能销毁 Flash 句柄。 */
aStatus_t aDataBaseBindFlash25q(aDevFlash25qHandle_t *handle);

#endif
