#ifndef TEST_SHELL_H
#define TEST_SHELL_H
#include <stdint.h>
typedef struct Shell Shell;
struct Shell {
    int (*lock)(Shell *);
    int (*unlock)(Shell *);
    int16_t (*read)(char *, uint16_t);
    int16_t (*write)(char *, uint16_t);
};
void shellInit(Shell *shell, char *buffer, uint16_t size);
void shellRemove(Shell *shell);
void shellHandler(Shell *shell, char data);
void shellWriteString(Shell *shell, const char *text);
#endif
