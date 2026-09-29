/* GCC + GNU ld: augment the product linker script, for host tests. */
SECTIONS
{
    .ashell_commands (READONLY) : ALIGN(8)
    {
        cmd_table = .;
        KEEP(*(SORT_BY_NAME(.ashell_cmd.*)))
        __ashell_commands_end = .;
    }
    .ashell_marks (READONLY) :
    {
        KEEP(*(.ashell_mark.*))
    }
    .ashell_command_count (READONLY) : ALIGN(2)
    {
        cmd_table_size = .;
        SHORT(SIZEOF(.ashell_marks))
    }
    ASSERT(SIZEOF(.ashell_marks) <= 65535,
           "aShell command count exceeds uint16_t")
}
INSERT AFTER .rodata;
