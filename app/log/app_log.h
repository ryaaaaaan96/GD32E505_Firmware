#ifndef APP_LOG_H
#define APP_LOG_H

#include "aStatus.h"

/* Shell 初始化完成后调用；应用负责选择日志后端。 */
aStatus_t appLogInit(void);

#endif
