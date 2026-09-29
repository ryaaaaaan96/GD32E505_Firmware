#include "aShell.h"

static int duplicate_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 0;
}

/* Same name as an export in a different translation unit. */
ASHELL_CMD_EXPORT(capture, duplicate_command, "Duplicate");
