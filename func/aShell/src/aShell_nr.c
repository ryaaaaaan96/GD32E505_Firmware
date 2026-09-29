#include "aShell_internal.h"
#include "nr_micro_shell.h"

#include <stddef.h>
#include <string.h>

/* The linker provides a contiguous read-only array and a uint16_t object.
 * nr's original declarations remain unchanged. Never write to this array.
 */
extern const unsigned char __ashell_commands_end[];

_Static_assert(sizeof(aShellExportDescriptor_t) == sizeof(struct cmd),
               "Command descriptor size mismatch");
_Static_assert(_Alignof(aShellExportDescriptor_t) == _Alignof(struct cmd),
               "Command descriptor alignment mismatch");
_Static_assert(offsetof(aShellExportDescriptor_t, name) ==
                   offsetof(struct cmd, name), "Command name offset");
_Static_assert(offsetof(aShellExportDescriptor_t, function) ==
                   offsetof(struct cmd, func), "Command callback offset");
_Static_assert(offsetof(aShellExportDescriptor_t, description) ==
                   offsetof(struct cmd, desc), "Command description offset");

char *auto_complete_words[] = {NULL};
const uint16_t auto_complete_words_size = 0U;

static int command_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    show_all_cmds();
    return 0;
}
ASHELL_CMD_EXPORT(help, command_help, "List commands");

static int command_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return ASHELL_PRINT("\x1b[2J\x1b[H") == A_STATUS_OK ? 0 : -1;
}
ASHELL_CMD_EXPORT(clear, command_clear, "Clear terminal screen");

static int command_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ASHELL_PRINT("nr_micro_shell %s\r\n", NR_SHELL_VERSION);
    return 0;
}
ASHELL_CMD_EXPORT(version, command_version, "Shell version");

aBool_t aShellNrCommandsAreValid(void)
{
    size_t i;
    size_t j;
    const char *name;
    const unsigned char *character;
    uintptr_t bytes = (uintptr_t)__ashell_commands_end -
                      (uintptr_t)cmd_table;

    if (bytes != (size_t)cmd_table_size * sizeof(struct cmd)) {
        return A_FALSE;
    }
    for (i = 0U; i < cmd_table_size; ++i) {
        name = cmd_table[i].name;
        if (name == NULL || name[0] == '\0' ||
            cmd_table[i].func == NULL || cmd_table[i].desc == NULL ||
            strlen(name) >= ASHELL_LINE_SIZE) {
            return A_FALSE;
        }
        for (character = (const unsigned char *)name; *character; ++character) {
            if (*character < 33U || *character > 126U) return A_FALSE;
        }
        for (j = 0U; j < i; ++j) {
            if (strcmp(name, cmd_table[j].name) == 0) return A_FALSE;
        }
    }
    return A_TRUE;
}

aStatus_t aShellNrInit(void)
{
    shell_init();
    return A_STATUS_OK;
}

void aShellNrProcess(char character)
{
    shell(character);
}

void aShellPortWrite(const char *data, size_t size)
{
    (void)aShellOutputWrite(data, size);
}

void aShellPortPutc(char character)
{
    (void)aShellOutputWrite(&character, 1U);
}
