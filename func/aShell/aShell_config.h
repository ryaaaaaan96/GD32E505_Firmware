#ifndef A_SHELL_CONFIG_H
#define A_SHELL_CONFIG_H

/* 包含结尾 NUL；nr_micro_shell 的游标使用 uint8_t，最大 256。 */
#define ASHELL_LINE_SIZE 128U
#define ASHELL_ARGUMENT_COUNT 16U
/* CMake supplies the product value; fallback for standalone host builds. */
#ifndef ASHELL_HISTORY_COUNT
#define ASHELL_HISTORY_COUNT 10U
#endif
/* Queue uses all bytes; formatted message capacity includes the NUL. */
#define ASHELL_OUTPUT_BUFFER_SIZE 1024U
#define ASHELL_PRINT_BUFFER_SIZE 256U
/* Set to "" to hide the welcome text; use CRLF for terminal newlines. */
#define ASHELL_WELCOME \
    "\r\n" \
    "     _        _                \r\n" \
    "    / \\   ___| | __ _ ___ ___  \r\n" \
    "   / _ \\ / __| |/ _` / __/ __| \r\n" \
    "  / ___ \\ (__| | (_| \\__ \\__ \\ \r\n" \
    " /_/   \\_\\___|_|\\__,_|___/___/ \r\n" \
    "\r\n" \
    "Type 'help' to list commands.\r\n\r\n"
/* nr_micro_shell appends ": " to this name. */
#define ASHELL_PROMPT "AIDC"

#endif
