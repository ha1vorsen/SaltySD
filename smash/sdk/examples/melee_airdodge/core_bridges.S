    .syntax unified
    .arm

    .section .cave_seed, "ax"
    .global cave_seed
cave_seed:
    push    {r4, r5, r6, lr}
    bl      escape_air_common_tail
    mov     r6, r0
    mov     r0, r5
    bl      melee_seed
    mov     r0, r6
    pop     {r4, r5, r6, pc}

    .section .cave_decay, "ax"
    .global cave_decay
cave_decay:
    push    {r0, r1, r4, lr}
    mov     r0, r4
    bl      melee_decay
    pop     {r0, r1}
    bl      escape_air_main_query
    pop     {r4, pc}

    .section .cave_redodge, "ax"
    .global cave_redodge
cave_redodge:
    push    {r4, r5, lr}
    mov     r4, r0
    bl      melee_block_redodge
    cmp     r0, #0
    beq     cave_redodge_allow
    mov     r0, r4
    pop     {r4, r5, lr}
    ldr     r3, [r0, #0x78]
    bic     r3, r3, #0x400000
    str     r3, [r0, #0x78]
    mov     r0, #0
    add     sp, sp, #0x14
    pop     {r4, r5, pc}
cave_redodge_allow:
    pop     {r4, r5, lr}
    b       change_status_thunk

    .section .cave_velocity, "ax"
    .global cave_velocity
cave_velocity:
    push    {r4, lr}
    sub     sp, sp, #8
    mov     r1, sp
    bl      melee_override_velocity
    cmp     r0, #0
    beq     cave_velocity_vanilla
    vldr    s0, [sp]
    vldr    s1, [sp, #4]
    mov     r2, #0
    vmov    s2, r2
    add     sp, sp, #8
    pop     {r4, pc}
cave_velocity_vanilla:
    add     sp, sp, #8
    pop     {r4, lr}
    mov     r2, #0
    vmov    s0, r2
    b       get_sum_continue

    .section .cave_transition, "ax"
    .global cave_transition
cave_transition:
    push    {r4, r5, lr}
    mov     r4, r0
    bl      melee_landing_only
    cmp     r0, #0
    beq     cave_transition_vanilla
    mov     r0, r4
    pop     {r4, r5, lr}
    push    {r4, lr}
    bl      air_landing_check
    pop     {r4, pc}
cave_transition_vanilla:
    b       air_check_continue

    .section .cave_status, "ax"
    .global cave_status
cave_status:
    push    {r4, lr}
    push    {r0, r1, r2, r3}
    ldr     r2, [sp, #20]
    bl      melee_status_record
    pop     {r0, r1, r2, r3}
    b       change_status_continue
