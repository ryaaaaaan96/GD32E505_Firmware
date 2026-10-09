/** @brief 静态 USART RTU 实例；全部字段私有，活动期间禁止复制。 */
#ifndef A_MODBUS_RTU_USART_INSTANCE_H
#define A_MODBUS_RTU_USART_INSTANCE_H

#include "aModbus_rtu_usart.h"
#include "aModbus_rtu_instance.h"
#if !ADEV_USART_DYNAMIC_ENABLE
#include "aDev_usart_instance.h"
#endif

struct aModbusRtuUsartHandle {
    aModbusRtuHandle_t rtu;
    aDevUsartHandle_t *usart;
#if !ADEV_USART_DYNAMIC_ENABLE
    aDevUsartHandle_t usart_instance;
#endif
    aBool_t dynamic;
};

#endif
