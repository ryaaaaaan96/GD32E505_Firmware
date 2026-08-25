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
    .shellCommand :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(_shell_command_start = .);
        KEEP(*(shellCommand))
        PROVIDE_HIDDEN(_shell_command_end = .);
        . = ALIGN(4);
    } > FLASH :text
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
