#ifndef APP_SIG_TASK_H
#define APP_SIG_TASK_H

#include "aStatus.h"

/* appSigInit 成功后调用一次，创建每秒自增的测试任务。 */
aStatus_t appSigTaskInit(void);

#endif
