    .syntax unified
    .arm

    .macro call func
        ldr r9, =\func
        blx r9
    .endm

    .section .hittest_payload, "ax"
    .global hittest_main
hittest_main:
    ldr r6, [sp]
    ldr r6, [r6, #-0x4]
    cmp r6, #0x17
    beq remove_display

    push {r0-r12, lr}
    sub sp, sp, #0x38

    ldr r1, [sp, #((14 * 4) + 0x38)]
    ldrb r2, [r3, #0x8]
    cmp r2, #0
    beq nope

    ldr r2, =0x1000013
    str r2, [sp, #0x4]
    ldr r2, [r1, #(2 * 4)]
    str r2, [sp, #0x8]
    ldr r2, [r1, #(9 * 4)]
    str r2, [sp, #0xC]
    ldr r2, [r1, #(10 * 4)]
    str r2, [sp, #0x10]
    ldr r2, [r1, #(11 * 4)]
    str r2, [sp, #0x14]

    mov r2, #0
    str r2, [sp, #0x18]
    str r2, [sp, #0x1C]
    str r2, [sp, #0x20]
    vldr.f32 s0, [r1, #(8 * 4)]
    vldr.f32 s1, ten
    vdiv.f32 s0, s0, s1
    vmov.f32 r2, s0
    str r2, [sp, #0x24]
    mov r2, #1
    str r2, [sp, #0x28]

    add r3, sp, #0x30
    mov r2, #0xB
    str sp, [r3]
    str r2, [r3, #0x4]
    ldr r2, =0x2712
    str r2, [sp]

    ldr r0, [r0, #0xA4]
    ldr r1, [r0]
    ldr r2, [r1, #0x174]
    mov r1, r3
    blx r2

nope:
    add sp, sp, #0x38
    pop {r0-r12, lr}

    ldr r6, [sp]
    ldr r6, [r6, #-0x4]
    cmp r6, #0x2D
    beq hitbox
    cmp r6, #0x30
    beq extended
    cmp r6, #0x32
    beq special
    cmp r6, #0x34
    beq extended_special
    cmp r6, #0x17
    beq remove_all
    bx lr

remove_display:
    push {r0-r12, lr}
    sub sp, sp, #0x38
    ldr r0, [r0, #0xA4]
    ldr r1, =0x1000013
    mov r2, #0
    mov r3, #0
    call effect_kill_kind
    add sp, sp, #0x38
    pop {r0-r12, lr}
    b remove_all

hitbox:
    ldr r7, =animcmd_hitbox
    bx r7

extended:
    ldr r7, =animcmd_extended
    bx r7

special:
    ldr r7, =animcmd_special
    bx r7

extended_special:
    ldr r7, =animcmd_extended_special
    bx r7

remove_all:
    ldr r7, =animcmd_remove_all
    bx r7

    .align 2
ten:
    .word 0x41200000
    .pool

    .section .hittest_remove_all, "a"
    .word hittest_main
    .section .hittest_hitbox, "a"
    .word hittest_main
    .section .hittest_extended, "a"
    .word hittest_main
    .section .hittest_special, "a"
    .word hittest_main
    .section .hittest_extended_special, "a"
    .word hittest_main
