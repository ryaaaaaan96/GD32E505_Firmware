SECTIONS
{
    .abus_bindings : ALIGN(8)
    {
        __abus_bindings_start = .;
        KEEP(*(.abus_bindings))
        __abus_bindings_end = .;
    }
}
INSERT AFTER .data.rel.ro;
