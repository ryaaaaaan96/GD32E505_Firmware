#ifndef APP_RS485_CONFIG_H
#define APP_RS485_CONFIG_H

#include "aDev_usart.h"

/* 填充板载串口配置：USART2 PC10/PC11、PA15 手动 DE，115200 / 8N1。
 * TX 缓冲区由本模块静态持有，只供一个活动串口实例使用。
 * 不打开设备；使用方负责调用相应模块的初始化入口。 */
void appRs485ConfigInit(aDevUsartConfig_t *config);

#endif
