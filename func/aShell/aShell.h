#ifndef A_SHELL_H
#define A_SHELL_H

#include "aStatus.h"
#include <stdbool.h>
#include <stdint.h>

struct shell_def;

typedef int16_t (*aShellRead_t)(char *buffer, uint16_t size);
typedef int16_t (*aShellWrite_t)(char *buffer, uint16_t size);

typedef struct {
    aShellRead_t read;
    aShellWrite_t write;
    uint16_t buffer_size;
    uint16_t task_stack_size;
    uint32_t task_priority;
} aShellConfig_t;

typedef struct {
    struct shell_def *shell_obj;
    char *buffer;
    void *task_handle;
} aShellHandle_t;

void aShellConfigStructInit(aShellConfig_t *config);
void aShellHandleStructInit(aShellHandle_t *handle);
aStatus_t aShellInit(aShellHandle_t *handle,
                     const aShellConfig_t *config);
aStatus_t aShellDeInit(aShellHandle_t *handle);
bool aShellIsEnabled(void);
void aShellPrint(aShellHandle_t *handle, const char *format, ...);

#endif
