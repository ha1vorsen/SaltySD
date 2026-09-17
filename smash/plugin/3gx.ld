/* Luma calls 0x07000100 and reads the segments as one block, so they must be contiguous. */

OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
    code   PT_LOAD FLAGS(5); /* R-X */
    rodata PT_LOAD FLAGS(4); /* R-- */
    data   PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
    PROVIDE(__start__ = 0x07000100);
    . = __start__;

    .text ALIGN(4) :
    {
        KEEP( *(.crt0) )
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.glue_7)
        *(.glue_7t)
        *(.gnu.linkonce.t*)
        . = ALIGN(4);
    } : code

    .rodata ALIGN(4) :
    {
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r*)
        . = ALIGN(4);
    } : rodata

    .data ALIGN(4) :
    {
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d*)
        . = ALIGN(4);
    } : data

    __bss_start__ = .;
    .bss ALIGN(4) :
    {
        *(.dynbss)
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b*)
        *(COMMON)
        . = ALIGN(4);
    } : data
    __bss_end__ = .;

    __end__ = ABSOLUTE(.);

    /DISCARD/ : { *(.group .comment .note .ARM.attributes .ARM.exidx* .ARM.extab*) }
}
