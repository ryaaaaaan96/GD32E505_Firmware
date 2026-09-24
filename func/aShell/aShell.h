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
    uint16_t task_stack_size;
    uint32_t task_priority;
} aShellConfig_t;

void aShellConfigStructInit(aShellConfig_t *config);
aStatus_t aShellInit(const aShellConfig_t *config);
aStatus_t aShellDeInit(void);
aBool_t aShellIsEnabled(void);
void aShellPrint(const char *format, ...);

#endif
