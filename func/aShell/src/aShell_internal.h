#ifndef A_SHELL_INTERNAL_H
#define A_SHELL_INTERNAL_H

#include "aShell.h"
#include "aOS.h"

typedef struct {
    aShellConfig_t config;
    aBool_t ready;
    aBool_t previous_cr;
} aShellContext_t;

extern aShellContext_t aShellContext;

/* Private backend boundary; caller owns lifecycle serialization. */
aBool_t aShellNrCommandsAreValid(void);
aStatus_t aShellNrInit(void);
void aShellNrProcess(char character);
aStatus_t aShellIoError(void);
aStatus_t aShellOutputInit(void);
void aShellOutputDeInit(void);
aStatus_t aShellOutputWrite(const char *data, size_t size);
aStatus_t aShellOutputDrain(void);
void aShellOutputDrop(void);

#endif
