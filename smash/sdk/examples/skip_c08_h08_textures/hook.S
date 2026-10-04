    .syntax unified
    .arm

    .section .hook_bch_texture_object, "ax", %progbits
    .align 2
    .global bch_texture_object_patch
    .type bch_texture_object_patch, %function
bch_texture_object_patch:
    b bch_texture_object_hook
    .size bch_texture_object_patch, . - bch_texture_object_patch

    .section .cave_texture_filter, "ax", %progbits
    .align 2
    .global bch_texture_object_hook
    .type bch_texture_object_hook, %function
bch_texture_object_hook:
    mov r4, r0

    push {r0-r3, r12, lr}
    sub sp, sp, #0x108
    mov r0, sp
    ldr r1, [sp, #0x264]
    bl resource_path_str

    ldr r0, [sp, #0x264]
    bl resource_data_size
    mov r2, r0
    mov r1, r4
    add r0, sp, #4
    bl suppress_c08_h08_bch_textures

    add sp, sp, #0x108
    pop {r0-r3, r12, lr}
    b bch_texture_table_continue
    .size bch_texture_object_hook, . - bch_texture_object_hook
