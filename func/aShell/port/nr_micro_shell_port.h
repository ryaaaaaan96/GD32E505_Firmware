#ifndef A_SHELL_NR_PORT_H
#define A_SHELL_NR_PORT_H

#include "aShell_config.h"
#include <stddef.h>


#define NR_SHELL_MAX_LINE_SZ ASHELL_LINE_SIZE
#define NR_SHELL_MAX_PARAM_NUM ASHELL_ARGUMENT_COUNT
#define NR_SHELL_PROMPT ASHELL_PROMPT
#define NR_SHELL_HISTORY_CMD_SUPPORT
#define NR_SHELL_HISTORY_CMD_NUM ASHELL_HISTORY_COUNT
#define NR_SHELL_HISTORY_CMD_SZ ASHELL_LINE_SIZE
#define NR_SHELL_AUTO_COMPLETE_SUPPORT

#if ASHELL_LINE_SIZE < 16 || ASHELL_LINE_SIZE > 256
#error "Shell line size must be in [16, 256]"
#endif
#if ASHELL_ARGUMENT_COUNT < 1 || ASHELL_ARGUMENT_COUNT > 255
#error "Shell argument count must be in [1, 255]"
#endif
#if ASHELL_HISTORY_COUNT < 1 || ASHELL_HISTORY_COUNT > 255
#error "Shell history count must be in [1, 255]"
#endif

void aShellPortWrite(const char *data, size_t size);
void aShellPortPutc(char character);
#define shell_putc(c) aShellPortPutc((char)(c))
#define shell_write(data, size) aShellPortWrite(data, size)

#endif
