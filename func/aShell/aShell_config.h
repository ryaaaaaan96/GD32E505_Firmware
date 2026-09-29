#ifndef A_SHELL_CONFIG_H
#define A_SHELL_CONFIG_H

/* 包含结尾 NUL；nr_micro_shell 的游标使用 uint8_t，最大 256。 */
#define ASHELL_LINE_SIZE 128U
#define ASHELL_ARGUMENT_COUNT 16U
#define ASHELL_HISTORY_COUNT 5U
/* Queue uses all bytes; formatted message capacity includes the NUL. */
#define ASHELL_OUTPUT_BUFFER_SIZE 1024U
#define ASHELL_PRINT_BUFFER_SIZE 256U
#define ASHELL_PROMPT "console"

#endif
