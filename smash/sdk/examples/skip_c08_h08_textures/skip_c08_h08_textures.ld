PHDRS
{
    hook PT_LOAD;
    cave PT_LOAD;
}

SECTIONS
{
    .hook_bch_texture_object 0x0017C230 :
    {
        KEEP(*(.hook_bch_texture_object))
    } :hook

    .cave_texture_filter 0x00B6110C :
    {
        KEEP(*(.cave_texture_filter))
        *(.text .text.*)
        *(.rodata .rodata.*)
    } :cave

    ASSERT(SIZEOF(.hook_bch_texture_object) == 4,
           "BCH texture-object hook must replace exactly one ARM instruction")
    ASSERT(ADDR(.cave_texture_filter) + SIZEOF(.cave_texture_filter) <= 0x00B62000,
           "texture filter exceeds its documented executable cave")

    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}

PROVIDE(bch_texture_table_continue = 0x0017C234);
PROVIDE(resource_path_str = 0x00181854);
PROVIDE(resource_data_size = 0x0016F0D0);
