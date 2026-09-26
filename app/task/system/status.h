#ifndef APP_SYSTEM_STATUS_H
#define APP_SYSTEM_STATUS_H

#include "aStatus.h"

/* Internal system startup API. Initializes the LED and creates its task once. */
aStatus_t statusInit(void);

#endif
