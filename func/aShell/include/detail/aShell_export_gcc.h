#ifndef A_SHELL_EXPORT_GCC_H
#define A_SHELL_EXPORT_GCC_H

/* Private wire layout, checked against nr's struct cmd in aShell_nr.c. */
typedef struct {
    const char *name;
    int (*function)(uint8_t argc, char **argv);
    const char *description;
} aShellExportDescriptor_t;

#define ASHELL_CMD_EXPORT(name, callback, description)                     \
    static int ashell_adapter_##name(uint8_t argc, char **argv)            \
    {                                                                    \
        aShellCommandFn_t function = (callback);                          \
        return function((int)argc, argv);                                \
    }                                                                    \
    static const unsigned char ashell_marker_##name                       \
        __attribute__((used, section(".ashell_mark." #name), aligned(1))) \
        = 0U;                                                            \
    static const aShellExportDescriptor_t ashell_command_##name           \
        __attribute__((used, section(".ashell_cmd." #name),               \
                       aligned(__alignof__(aShellExportDescriptor_t))))  \
        = {#name, ashell_adapter_##name, description}

#endif
