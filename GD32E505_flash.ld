ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x400;
_Min_Stack_Size = 0x800;

PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH :text
    .text :
    {
        *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4); _etext = .;
    } > FLASH :text
    /* aShell GCC exports; keep before RAM data load images. */
    .abus_bindings (READONLY) : ALIGN(8)
    {
        __abus_bindings_start = .;
        KEEP(*(.abus_bindings))
        __abus_bindings_end = .;
    } > FLASH :text

    .ashell_commands (READONLY) : ALIGN(8)
    {
        cmd_table = .;
        KEEP(*(SORT_BY_NAME(.ashell_cmd.*)))
        __ashell_commands_end = .;
    } > FLASH :text
    .ashell_marks (READONLY) :
    {
        KEEP(*(.ashell_mark.*))
    } > FLASH :text
    .ashell_command_count (READONLY) : ALIGN(2)
    {
        cmd_table_size = .;
        SHORT(SIZEOF(.ashell_marks))
    } > FLASH :text
    ASSERT(SIZEOF(.ashell_marks) <= 65535,
           "aShell command count exceeds uint16_t")
    .ARM.extab : { *(.ARM.extab*) } > FLASH :text
    .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH :text
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH :text
    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH :text
    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH :text
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH :data
    .bss (NOLOAD) :
    {
        . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
    } > RAM :NONE
    .heap_stack (NOLOAD) :
    {
        . = ALIGN(8); PROVIDE(end = .); PROVIDE(_end = .);
        PROVIDE(_heap_start = .); . += _Min_Heap_Size;
        PROVIDE(_heap_end = .); . += _Min_Stack_Size; . = ALIGN(8);
    } > RAM :NONE
    /DISCARD/ : { *(.note*) *(.comment*) }
    ASSERT(_ebss + _Min_Heap_Size + _Min_Stack_Size <= _estack,
           "RAM overflow: heap and stack collide")
}
