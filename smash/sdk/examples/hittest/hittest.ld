PHDRS
{
    payload PT_LOAD;
    remove_all PT_LOAD;
    hitbox PT_LOAD;
    extended PT_LOAD;
    special PT_LOAD;
    extended_special PT_LOAD;
}

SECTIONS
{
    .hittest_payload 0x00A2B800 : { *(.hittest_payload) } :payload
    ASSERT(SIZEOF(.hittest_payload) <= 0x400,
           "hittest payload exceeds the checked 0x400-byte placement span")

    .hittest_remove_all       0x00C2D214 : { *(.hittest_remove_all) } :remove_all
    .hittest_hitbox           0x00C2D26C : { *(.hittest_hitbox) } :hitbox
    .hittest_extended         0x00C2D278 : { *(.hittest_extended) } :extended
    .hittest_special          0x00C2D280 : { *(.hittest_special) } :special
    .hittest_extended_special 0x00C2D288 : { *(.hittest_extended_special) } :extended_special

    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}

PROVIDE(effect_kill_kind = 0x0055E05C);
PROVIDE(animcmd_remove_all = 0x006F5610);
PROVIDE(animcmd_extended = 0x006F86E8);
PROVIDE(animcmd_extended_special = 0x006FC3F4);
PROVIDE(animcmd_hitbox = 0x007053C8);
PROVIDE(animcmd_special = 0x007069CC);
