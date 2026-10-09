#ifndef ADEV_LED_INSTANCE_H
#define ADEV_LED_INSTANCE_H

#include "aDev_led.h"

/** @brief 静态分配布局，业务只借用句柄；字段由设备层维护。 */
struct aDevLedHandle {
    aDrvGpioHandle_t gpio; /**< 硬件运行状态，不重复保存 initialized。 */
    aDevLedActiveLevel_t active_level; /**< 初始化时复制的配置。 */
};

#endif
