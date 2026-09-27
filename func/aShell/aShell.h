#ifndef A_SHELL_H
#define A_SHELL_H

#include "aLib.h"

#include <stdint.h>

typedef int16_t (*aShellRead_t)(char *buffer, uint16_t size);
typedef int16_t (*aShellWrite_t)(char *buffer, uint16_t size);

typedef struct {
    aShellRead_t read;
    aShellWrite_t write;
    uint16_t buffer_size;
} aShellConfig_t;

void aShellConfigStructInit(aShellConfig_t *config);
aStatus_t aShellInit(const aShellConfig_t *config);
/* Process at most one input character. Called by one application-owned task
 * or main loop; read may block for its configured timeout. Stop that caller
 * and quiesce all API users before DeInit. No task is created by aShell. */
aStatus_t aShellProcess(void);
aStatus_t aShellDeInit(void);
aBool_t aShellIsEnabled(void);
void aShellPrint(const char *format, ...);

#endif
